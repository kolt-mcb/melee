'use strict';
/* The lobby and the netplay relay (tools/wasm/serve.js mounts it at /net).
 *
 * One room: everyone on the home network who opens the page. In the lobby
 * each player picks a fighter and a colour; the host (whoever has been here
 * longest) picks the stage and rules and starts the match. Start assigns
 * slots in join order and a fresh seed, and every page boots the same match
 * (MELEE_BOOT_MODE=14 with the lineup) in lockstep (MELEE_NET=web).
 *
 * Messages are WebSocket binary frames. A lobby message is JSON (it starts
 * with '{'); a game packet is the port's own (magic "MNP1",
 * src/port/netplay/pc_netplay.c), forwarded unchanged to every other player
 * in the match. The relay reads only one thing from them: the newest frame
 * of each slot's MSG_INPUT, so that when a player leaves it can tell the
 * rest exactly which frame that player's input stops after (MSG_BYE).
 *
 * A page can seat more than one player: two pads on one computer are two
 * players. The page's own player is its member; the others are its `guests`,
 * each with a fighter and colour of its own, and each gets a slot of its own
 * at Start, right after the member's. The page sends one input stream per
 * slot, and every other page sees them as separate players.
 *
 * Input packets can also travel as datagrams: each page may open a WebRTC
 * data channel to the relay (tools/wasm/rtc.js), unordered and never
 * retransmitted, and MSG_INPUT goes over it both ways while it is up. One
 * lost TCP segment on a player's Wi-Fi used to freeze everybody for a
 * retransmit; a lost datagram is covered by the next packet, which repeats
 * the last twelve frames. Everything else -- the lobby, hashes, MSG_BYE --
 * stays on the WebSocket, and so do the inputs of a page whose channel is
 * not up, so pages on either path play one match.
 *
 * Over datagrams the pages may each hold a different set of a player's
 * frames, so the relay keeps every slot's recent pads and, when a player
 * leaves, sends each remaining page that player's last frames over its
 * WebSocket before the MSG_BYE: every page then has exactly the frames up to
 * the one the BYE names, the same guarantee one ordered stream gave.
 *
 * The input delay comes from the players' measured round trips. The relay
 * pings every page twice a second while it is in the lobby, and at Start
 * picks the frames of delay that cover the slowest pair's path through it:
 * a player's input reaches another in about half of each one's round trip,
 * and the delay is what hides that. MELEE_NET_DELAY=<n> on the server fixes
 * it instead (3 was the old constant).
 */

const crypto = require('crypto');
const rtc = require('./rtc.js');

const MAX_PLAYERS = 8; // the PC build's fighter slots
const MAX_LOCAL = 4;   // players at one page: the four pads the engine reads
const PING_MS = 500, PING_KEEP = 20;
const FRAME_MS = 1000 / 60;
const FIXED_DELAY = process.env.MELEE_NET_DELAY ? parseInt(process.env.MELEE_NET_DELAY, 10) : 0;
const MAGIC = 0x4D4E5031, MSG_INPUT = 4, MSG_HASH = 5, MSG_BYE = 6, HDR = 8;
const PAD_BYTES = 10, WINDOW = 12;   // pc_netplay.c NET_PAD_BYTES, NET_WINDOW
const KEEP_PADS = 256;               // each slot's newest pads, for the leaver's last frames
const MAX_MEMBERS = 64;              // the whole room; a home lobby is a handful
const PER_IDENT_MAX = 8;             // sockets one SSO identity may hold at once
const OFFER_MIN_MS = 1000;           // least time between a page's datagram-channel offers
console.log(`[relay] datagram channel: ${rtc.describe()}`);
/* How long a seat is held for a page whose socket dropped mid-match. The
 * engine stalls on the missing input and resends what the peer has not
 * acknowledged, so a page that is back inside this window picks the match
 * up where it stopped; lobby.js retries for a little less than this. */
const RESUME_MS = +process.env.MELEE_RESUME_MS || 30000;
/* A member that has sent nothing -- not even the pong its browser's network
 * process answers a WebSocket ping with -- for this long is gone, whatever
 * its socket says: a lid closed on a laptop, a phone off Wi-Fi, a NAT entry
 * expired, none of them send a FIN. Found by the sweep (liveness) below; the
 * socket is closed as unclean, so a page in a match keeps its RESUME_MS. A
 * page in a 20 s shader compile still answers these, which the JSON ping
 * (a round trip the page's own thread has to run) does not promise. */
const LIVENESS_MS = +process.env.MELEE_LIVENESS_MS || 15000;
const SWEEP_MS = +process.env.MELEE_SWEEP_MS || 5000;
/* A player whose input has not moved for longer than the engine's own wait
 * (lobby.js MELEE_NET_TIMEOUT, 15 min) while its socket answers pings is a
 * page whose engine hung: every other page has by then given up waiting on
 * it and played on, or finished. It is kicked out of the match so the match
 * can end; the page sees a lost connection and the way back to the lobby. */
const MATCH_STUCK_MS = +process.env.MELEE_MATCH_STUCK_MS || 16 * 60000;
const WATCH_MS = +process.env.MELEE_WATCH_MS || 5000;

const room = {
    members: [],         // in join order: { conn, id, name, ckind, color, guests, slots, lastFrame }
    rules: { stage: 31, kind: 'stock', stocks: 4, minutes: 8, items: -1, cpus: 0, cpulevel: 9 },
    match: null,         // { players: [member], started }
};
let nextId = 1;

/* The engine every page should be running: serve.js hands over a function
 * that stats the deployed melee.wasm (the same tag the page is stamped with).
 * Pages on different engines cannot play one match -- 2026-10-23 one page
 * loaded before a deploy played a three-way against two loaded after it, the
 * two builds read the third pad differently, and the match desynced at frame
 * 50. A page says which engine it runs in its hello; one that says nothing is
 * older than this check, and so stale too. */
let currentWasm = () => null;
exports.setWasmVersion = (fn) => { currentWasm = fn; };
// Not before the page's hello: the socket is a member, and is broadcast to,
// a moment before the hello saying which engine it runs arrives.
const stale = (m) => { const cur = currentWasm(); return !!cur && m.said && m.wasm !== cur; };

const json = (conn, obj) => conn.send(Buffer.from(JSON.stringify(obj)));

/* A member's players: its own, then its guests'. */
const seatsOf = (m) => [{ name: m.name, ckind: m.ckind, color: m.color },
    ...m.guests.map((g, i) => ({ name: `${m.name} ${i + 2}`, ckind: g.ckind, color: g.color }))];

/* The members that fit in the match, in join order. A page comes in with all
 * of its players or not at all: half of a page's players in the match
 * would leave the others holding pads that drive nobody. */
function seated() {
    const out = [];
    let n = 0;
    for (const m of room.members) {
        const k = 1 + m.guests.length;
        if (n + k > MAX_PLAYERS) continue;
        out.push(m);
        n += k;
    }
    return out;
}

function lobbyState(forId) {
    const host = room.members[0];
    const in_ = seated();
    return {
        t: 'lobby',
        you: forId,
        host: host ? host.id : null,
        players: in_.flatMap((m) => seatsOf(m).map((p, seat) => ({
            // The name says it too: a stale page runs a lobby that predates
            // `stale`, and its player has to see it somewhere.
            id: m.id, seat, name: stale(m) ? `${p.name} (old version: reload)` : p.name, stale: stale(m), ckind: p.ckind, color: p.color, ping: pingOf(m), pre: m.pre, loaded: m.loaded,
            inMatch: !!(room.match && room.match.players.includes(m)),
        }))),
        waiting: room.members.length - in_.length,
        rules: room.rules,
        inMatch: !!room.match,
        rtc: rtc.available,
        ice: rtc.iceServers,   // STUN for the page's RTCPeerConnection (rtc.js MELEE_STUN)
        // This page's own secret for reclaiming its seat after a drop (parked).
        // The state is built per member, so it never carries anyone else's.
        token: (room.members.find((m) => m.id === forId) || {}).token || null,
    };
}

function broadcastLobby() {
    for (const m of room.members) {
        json(m.conn, lobbyState(m.id));
        // A page in a match plays it out on the engine it started with.
        if (stale(m) && !(room.match && room.match.players.includes(m))) json(m.conn, { t: 'stale' });
    }
}

/* A player's round trip to the relay: the 90th percentile of the recent
 * samples, so the delay covers the jitter and not just the average. */
function pingOf(m) {
    if (m.rtts.length < 3) return null;
    const s = [...m.rtts].sort((a, b) => a - b);
    return Math.round(s[Math.min(s.length - 1, Math.floor(s.length * 0.9))]);
}

/* One player's input reaches another through the relay in about half of each
 * one's round trip; the slowest such pair sets the delay. Plus one frame,
 * because the pages' frames are not in phase: an input can leave just after
 * the peer's frame began, and one more for the path's jitter. Two at
 * least -- one already failed on a clean local link -- and at most 12. */
function chooseDelay(players) {
    if (FIXED_DELAY > 0) return { delay: FIXED_DELAY, why: 'fixed (MELEE_NET_DELAY)' };
    if (players.length < 2) return { delay: 3, why: 'one player' };
    const pings = players.map(pingOf);
    if (pings.some((p) => p === null)) return { delay: 3, why: 'not enough ping samples' };
    const [a, b] = [...pings].sort((x, y) => y - x);
    const oneWay = (a + b) / 2;
    // One frame more than the path needs, for its jitter: with the delay
    // just covering the 90th-percentile path, a three-way on 2026-10-23
    // (Starlink at 170 ms among them) spent 7-13% of the match waiting on
    // 50-250 ms swings. And the cap leaves room for that margin.
    const want = Math.ceil(oneWay / FRAME_MS) + 2;
    const delay = Math.max(2, Math.min(12, want));
    // Past the cap the faster page stalls on the slower one's input about
    // once a second (a Mac against a phone on LTE at 288 ms, 2026-10-23: 29
    // stalls in 29 s). More delay would not play better; say so instead.
    return { delay, why: `pings ${pings.join('/')} ms, slowest path ~${oneWay.toFixed(0)} ms` +
             (want > 12 ? `; the path wants ${want} frames, capped at 12: expect stalls` : '') };
}

function startMatch() {
    const players = seated();
    if (players.length < 1 || room.match) return;
    const old = players.filter(stale);
    if (old.length) {
        console.log(`[relay] not starting: ${old.map((m) => m.name).join(', ')} on an old engine (${old.map((m) => m.wasm || 'unstamped').join(', ')}, current ${currentWasm()})`);
        broadcastLobby();
        return;
    }
    const seed = crypto.randomBytes(4).readUInt32BE(0);
    const lineup = [];
    for (const m of players) {
        m.slots = [];
        m.lastFrames = {};
        m.pads = {};
        m.solid = {};
        for (const p of seatsOf(m)) {
            m.slots.push(lineup.length);
            m.lastFrames[lineup.length] = -1;
            m.pads[lineup.length] = new Map();
            m.solid[lineup.length] = -1;
            lineup.push({ slot: lineup.length, name: p.name, ckind: p.ckind, color: p.color });
        }
        m.slot = m.slots[0];
        m.lastFrame = -1;
        m.loaded = false;
    }
    const { delay, why } = chooseDelay(players);
    room.match = { players, seed, delay, watch: setInterval(watchMatch, WATCH_MS).unref() };
    players.forEach((m) => { m.fwdIn = 0; m.fwdOut = 0; m.dgIn = 0; m.dgOut = 0; m.seenFrame = -1; m.stuckSince = 0; m.done = false; m.bye = false; });
    for (const m of players) {
        // `pages`: with one, the match is local and needs no netplay at all,
        // however many players that page seats.
        json(m.conn, { t: 'start', slot: m.slot, slots: m.slots, pages: players.length,
                       players: lineup, seed, rules: room.rules, delay, why });
    }
    console.log(`[relay] match: ${lineup.map((p) => `${p.name}(${p.ckind})`).join(' vs ')}, ` +
                `stage ${room.rules.stage}, seed ${seed.toString(16)}, delay ${delay} (${why})`);
    broadcastLobby();
}

/* Every five seconds of a match: where each player's input has reached, and
 * whether anything is stuck on its way to them. A player whose newest input
 * has not moved is being waited on, or is waiting; bytes queued unsent on a
 * socket mean the path to that page is not draining. Logged when anything
 * looks stuck, and once a minute otherwise. One match froze with two pages
 * waiting on each other's input and nothing in either page's log said where
 * the packets went. */
let watchTick = 0;
function watchMatch() {
    if (!room.match) return;
    const now = Date.now(), ps = room.match.players;
    let stuck = false;
    for (const m of ps) {
        if (m.lastFrame !== m.seenFrame) { m.seenFrame = m.lastFrame; m.stuckSince = now; }
        else if (m.lastFrame >= 0 && now - m.stuckSince >= 4000 && room.members.includes(m) && !m.done) stuck = true;
        if ((m.conn.socket && m.conn.socket.writableLength) > 64 * 1024) stuck = true;
        // stuckSince is 0 until the first input: a page still loading is
        // waited on, not kicked (the engine's timeout starts the same way).
        if (m.stuckSince && now - m.stuckSince >= MATCH_STUCK_MS && room.members.includes(m) && !m.done) {
            console.log(`[relay] ${m.name}: no input for ${((now - m.stuckSince) / 60000).toFixed(0)} min; dropped from the match`);
            m.done = true;
            m.conn.close(false);   // -> onclose -> leave(): its BYE, and the match can end
        }
    }
    if (!stuck && ++watchTick % 12 !== 0) return;
    console.log(`[relay] match ${stuck ? 'STUCK' : 'ok'}: ` + ps.map((m) =>
        `${m.name}(slot ${m.slots.join('+')}) input to ${m.lastFrame}` +
        (now - m.stuckSince >= 4000 && m.lastFrame >= 0 ? ` [still ${((now - m.stuckSince) / 1000).toFixed(0)} s]` : '') +
        `, in ${m.fwdIn} pkts (${m.dgIn} dgram), out ${m.fwdOut} pkts (${m.dgOut} dgram), unsent ${m.conn.socket ? m.conn.socket.writableLength : '?'} B` +
        (room.members.includes(m) ? '' : ' (gone)')).join(' | '));
}

function endMatchIfEmpty() {
    if (room.match && !room.match.players.some((m) => room.members.includes(m) && !m.done)) {
        clearInterval(room.match.watch);
        room.match = null;
        console.log('[relay] match over');
        broadcastLobby();
    }
}

/* A slot's pads as they arrive, in any order and any number of times: the
 * newest KEEP_PADS of them, and `solid`, the newest frame with every frame
 * before it here. Only solid frames are ones every other page can be given. */
function keepPads(from, slot, base, count, buf) {
    const pads = from.pads[slot];
    if (!pads) return;
    for (let i = 0; i < count; i++) {
        const f = base + i;
        if (f <= from.solid[slot] - KEEP_PADS || pads.has(f)) continue;
        pads.set(f, Buffer.from(buf.subarray(HDR + 5 + i * PAD_BYTES, HDR + 5 + (i + 1) * PAD_BYTES)));
    }
    while (pads.has(from.solid[slot] + 1)) from.solid[slot]++;
    for (const f of pads.keys()) {
        if (f > from.solid[slot] - KEEP_PADS) break;  // a Map iterates in insertion order: roughly oldest first
        pads.delete(f);
    }
}

/* A game packet from a page, by either path, to every other page in the
 * match: inputs by datagram where that page's channel is up, the rest (and
 * everything, where it is not) by WebSocket. */
function forward(from, buf, viaDgram) {
    if (!room.match || !room.match.players.includes(from)) return;
    /* A page may send exactly two things into a match, MSG_INPUT and MSG_HASH,
     * and only for the seats it was given (from.slots): the slot lives in the
     * packet, not in who sent it, so without this a page could forge another
     * player's inputs (a desync, or moves in their slot). Everything else is
     * dropped here, an allowlist and not a denylist. MSG_BYE is the relay's to
     * send when a socket drops (leave()); a BYE from a page would freeze that
     * slot on every other page. MSG_HELLO/SETUP/READY are the UDP build's
     * handshake, which the web lobby replaces -- yet a page still ACCEPTS a
     * MSG_SETUP (pc_netplay.c net_handle refuses it only on the UDP host), so
     * one forged packet would rewrite every other page's slot, delay and seed
     * mid-match. A packet without the magic is nothing the engine reads. */
    if (buf.length < HDR || buf.readUInt32BE(0) !== MAGIC) return;
    const type = buf[4], slot = buf[5];
    if (type !== MSG_INPUT && type !== MSG_HASH) return;
    if (!(from.slots && from.slots.includes(slot))) return;
    if (type === MSG_INPUT && buf.length < HDR + 5) return;
    const input = type === MSG_INPUT;
    if (input) {
        const base = buf.readUInt32BE(HDR), count = buf[HDR + 4];
        if (count > WINDOW || buf.length < HDR + 5 + count * PAD_BYTES) return;
        const last = base + count - 1;
        if (last > from.lastFrame) from.lastFrame = last;
        const slot = buf[5];
        if (slot in from.lastFrames && last > from.lastFrames[slot]) from.lastFrames[slot] = last;
        keepPads(from, slot, base, count, buf);
    }
    from.fwdIn = (from.fwdIn || 0) + 1;
    if (viaDgram) from.dgIn = (from.dgIn || 0) + 1;
    for (const m of room.match.players) {
        if (m === from || !room.members.includes(m)) continue;
        m.fwdOut = (m.fwdOut || 0) + 1;
        if (input && m.conn.rtc && m.conn.rtc.usable() && m.conn.rtc.send(buf)) { m.dgOut = (m.dgOut || 0) + 1; continue; }
        m.conn.send(buf);
    }
}

/* A slot's pads [from, to] as MSG_INPUT packets of at most WINDOW frames. */
function padPackets(m, slot, from, to) {
    const out = [];
    for (let base = from; base <= to; base += WINDOW) {
        const n = Math.min(WINDOW, to - base + 1);
        const b = Buffer.alloc(HDR + 5 + n * PAD_BYTES);
        b.writeUInt32BE(MAGIC, 0);
        b[4] = MSG_INPUT;
        b[5] = slot;
        b.writeUInt32BE(base >>> 0, HDR);
        b[HDR + 4] = n;
        for (let i = 0; i < n; i++) {
            const p = m.pads[slot].get(base + i);
            if (p) p.copy(b, HDR + 5 + i * PAD_BYTES);
        }
        out.push(b);
    }
    return out;
}

/* A page is out of the match: the other pages get its last frames and a
 * MSG_BYE per slot, once. Whether its match ended or it abandoned one makes
 * no difference here -- a BYE reaching a page whose own match is over is
 * nothing (the engine only records the frame), and a page that left a stuck
 * load is one the others are still waiting on. */
function byeFor(m) {
    if (m.bye || !room.match || !room.match.players.includes(m)) return;
    m.bye = true;
    // One per slot the page played, each with that slot's own last
    // frame: the streams are separate packets and need not end together.
    //
    // The last frame is the newest the relay holds with nothing missing
    // before it (on a WebSocket-only match, simply the newest), and
    // before the BYE every page is sent that slot's frames leading up to
    // it, on its WebSocket: datagrams may have lost some of them on the
    // way to any one page. A page can be at most about two delays behind
    // the leaver (each waits on the other's input), so twice the delay
    // and two windows back is enough; the frames are ones it has
    // received already, or needs.
    const back = 2 * (room.match.delay || 10) + 2 * WINDOW;
    for (const slot of m.slots) {
        const last = m.solid[slot] != null ? m.solid[slot] : m.lastFrames[slot];
        const bye = Buffer.alloc(HDR + 4);
        bye.writeUInt32BE(MAGIC, 0);
        bye[4] = MSG_BYE;
        bye[5] = slot;
        bye.writeUInt32BE(last >>> 0, HDR);
        const tail = last >= 0 && m.pads[slot] ? padPackets(m, slot, Math.max(0, last - back + 1), last) : [];
        for (const o of room.match.players) {
            if (o === m || !room.members.includes(o)) continue;
            for (const b of tail) o.conn.send(b);
            o.conn.send(bye);
        }
    }
    console.log(`[relay] ${m.name} left the match after frame ${m.slots.map((k) => m.solid[k]).join('/')}` +
                ` (newest seen ${m.lastFrame})`);
}

/* The one way out of the room. A page that is done with its match leaves by
 * this too: it is then neither seated in the next match nor the host, and it
 * reloads to come back (lobby.js backToLobby). A page parked on the "Match
 * over" banner used to stay a member, hold the host seat with a lobby it no
 * longer painted, and get seated in a match its engine ignored. */
function leave(m) {
    const i = room.members.indexOf(m);
    if (i < 0) return;
    room.members.splice(i, 1);
    byeFor(m);
    endMatchIfEmpty();
    broadcastLobby();
}

/* A deploy wants to know when nobody is playing (serve.js /healthz). */
exports.status = () => ({
    members: room.members.length,
    match: !!room.match,
    away: room.members.filter((m) => m.away).length,
});

/* The seat a dropped page may come back to: the match it was in is still
 * running, and its grace has not run out. */
function parked(id, seed, token) {
    // The id is in every lobby broadcast and the seed in every start message,
    // so on their own any page in the room could claim a dropped player's seat
    // during the hold. The token went to that page alone (lobbyState).
    if (typeof token !== 'string' || !token) return null;
    return room.members.find((m) => m.id === id && m.away && m.token === token && room.match &&
                                    room.match.seed === seed && room.match.players.includes(m)) || null;
}

/* Liveness: ping every socket, and close the ones that have answered nothing
 * for LIVENESS_MS. The browser answers in its network process, so the only
 * page this drops is one that is really unreachable; that page is then the
 * unclean-close case above (held, or gone). Off while the room is empty. */
let sweeper = null;
function sweep() {
    if (!room.members.length) { clearInterval(sweeper); sweeper = null; return; }
    const now = Date.now();
    for (const m of [...room.members]) {
        if (m.away) continue;   // its socket is already gone; the hold decides
        if (now - m.conn.lastSeen > LIVENESS_MS) {
            console.log(`[relay] ${m.name}: nothing heard for ${((now - m.conn.lastSeen) / 1000).toFixed(0)} s; dropping the socket`);
            m.conn.close(false);
            continue;
        }
        m.conn.ping();
    }
}

exports.join = (conn, req) => {
    /* One SSO'd user, one room, no limit on sockets used to mean a single page
     * (or a buggy tab, or an abuser) could open thousands: each is a member
     * with its own 500 ms pinger and is walked on every lobby broadcast. Cap
     * the room, and cap how many sockets one identity may hold at once. Behind
     * the gateway every socket arrives from Caddy, so the per-IP address is the
     * proxy's; the SSO email it forwards (X-Forwarded-Email) is the real
     * identity, with the socket address as the fallback off-gateway. Counted
     * from room.members itself, so the resume path (which splices a member)
     * needs no separate bookkeeping. */
    const ident = (req && req.headers && req.headers['x-forwarded-email']) ||
                  (conn.socket && conn.socket.remoteAddress) || '?';
    if (room.members.length >= MAX_MEMBERS ||
        room.members.filter((o) => o.ident === ident).length >= PER_IDENT_MAX) {
        console.log(`[relay] refusing a socket from ${ident}: ` +
                    `${room.members.length}/${MAX_MEMBERS} in the room`);
        try { conn.socket && conn.socket.destroy(); } catch (e) { /* */ }
        return;
    }
    const m = { conn, id: nextId++, ident, token: crypto.randomBytes(16).toString('hex'),
                name: `Player ${nextId - 1}`, ckind: 8, color: 0, guests: [],
                slot: -1, slots: [], lastFrame: -1, lastFrames: {}, pads: {}, solid: {}, done: false, rtts: [], pingAt: 0, pingSeq: 0, pre: null,
                lastOfferAt: 0, away: null };
    room.members.push(m);
    attach(m, conn);
    if (!sweeper) sweeper = setInterval(sweep, SWEEP_MS).unref();
    broadcastLobby();
};

/* Wire a socket to a member: at join, and again when a page whose socket
 * dropped mid-match comes back on a new one. */
function attach(m, conn) {
    m.conn = conn;
    m.wantsHold = false;
    /* Measured in the lobby only: in a match the page's thread is busy and
     * a reply would measure its frame time, not the network. */
    clearInterval(m.pinger);
    let tick = 0;
    m.pinger = setInterval(() => {
        if (room.match && room.match.players.includes(m)) {
            // Every few seconds, unmeasured: something for the page to hear
            // while nobody is sending input (lobby.js watches for a relay
            // gone quiet). pingAt stays 0, so the pong is not a sample.
            if (++tick % 10 === 0) json(m.conn, { t: 'ping', s: m.pingSeq });
            return;
        }
        m.pingSeq++;
        m.pingAt = performance.now();
        json(m.conn, { t: 'ping', s: m.pingSeq });
    }, PING_MS).unref();
    conn.who = m.name;
    conn.onclose = (clean) => {
        clearInterval(m.pinger);
        rtc.close(conn);
        if (room.match && room.match.players.includes(m) && !m.done && (!clean || m.wantsHold)) {
            // The link died under the page: hold the seat, it may be back
            // before anyone notices. A clean close is the page saying
            // goodbye (a reload, a closed tab, Leave match) -- holding that
            // seat only stalls the others for the whole hold.
            m.away = setTimeout(() => { m.away = null; leave(m); }, RESUME_MS);
            console.log(`[relay] ${m.name} dropped at frame ${m.lastFrame}; holding the seat ${RESUME_MS / 1000} s`);
            return;
        }
        leave(m);
    };
    conn.onmessage = (buf, viaDgram) => {
        if (buf[0] !== 0x7B /* '{' */) return forward(m, buf, viaDgram);
        if (viaDgram) return;
        let msg;
        try { msg = JSON.parse(buf.toString('utf8')); } catch (e) { return; }
        const isHost = room.members[0] === m;
        switch (msg.t) {
        case 'pong':
            if (msg.s === m.pingSeq && m.pingAt) {
                m.rtts.push(performance.now() - m.pingAt);
                if (m.rtts.length > PING_KEEP) m.rtts.shift();
                m.pingAt = 0;
                // Refresh the lobby's ping readout every few samples.
                if (m.pingSeq % 4 === 0) broadcastLobby();
            }
            break;
        case 'hello':
            if (msg.resume && typeof msg.resume === 'object') {
                const p = parked(msg.resume.id, msg.resume.seed, msg.resume.token);
                if (!p) { json(conn, { t: 'resumed', ok: false }); break; }
                // This socket is that player's, not a newcomer's: hand it over.
                clearTimeout(p.away);
                p.away = null;
                clearInterval(m.pinger);
                room.members.splice(room.members.indexOf(m), 1);
                attach(p, conn);
                json(conn, { t: 'resumed', ok: true });
                console.log(`[relay] ${p.name} is back; the match goes on from frame ${p.lastFrame}`);
                broadcastLobby();
                break;
            }
            // fallthrough: a first hello is a pick
        case 'pick':
            m.said = true;
            if (typeof msg.wasm === 'string') m.wasm = msg.wasm.slice(0, 64);
            if (typeof msg.name === 'string' && msg.name.trim())
                m.name = msg.name.trim().slice(0, 16);
            if (Number.isInteger(msg.ckind) && msg.ckind >= 0 && msg.ckind < 26) m.ckind = msg.ckind;
            if (Number.isInteger(msg.color) && msg.color >= 0 && msg.color < 6) m.color = msg.color;
            // Not while this page is in a match: its slots are fixed.
            if (Array.isArray(msg.guests) && !(room.match && room.match.players.includes(m))) {
                m.guests = msg.guests.slice(0, MAX_LOCAL - 1).map((g) => ({
                    ckind: g && Number.isInteger(g.ckind) && g.ckind >= 0 && g.ckind < 26 ? g.ckind : 8,
                    color: g && Number.isInteger(g.color) && g.color >= 0 && g.color < 6 ? g.color : 0,
                }));
            }
            broadcastLobby();
            break;
        case 'rules':
            if (!isHost || room.match) break;
            if (Number.isInteger(msg.stage) && msg.stage >= 2 && msg.stage <= 32) room.rules.stage = msg.stage;
            if (msg.kind === 'stock' || msg.kind === 'time') room.rules.kind = msg.kind;
            if (Number.isInteger(msg.items) && msg.items >= -1 && msg.items <= 4) room.rules.items = msg.items;
            if (Number.isInteger(msg.stocks) && msg.stocks >= 1 && msg.stocks <= 99) room.rules.stocks = msg.stocks;
            if (Number.isInteger(msg.minutes) && msg.minutes >= 0 && msg.minutes <= 99) room.rules.minutes = msg.minutes;
            if (Number.isInteger(msg.cpus) && msg.cpus >= 0 && msg.cpus < MAX_PLAYERS) room.rules.cpus = msg.cpus;
            if (Number.isInteger(msg.cpulevel) && msg.cpulevel >= 1 && msg.cpulevel <= 9) room.rules.cpulevel = msg.cpulevel;
            broadcastLobby();
            break;
        case 'start':
            if (isHost) startMatch();
            break;
        case 'pre': // how much of the match's data this page has downloaded
            if (Number.isInteger(msg.pct) && msg.pct >= 0 && msg.pct <= 100 && msg.pct !== m.pre) {
                m.pre = msg.pct;
                broadcastLobby();
            }
            break;
        case 'loaded': // this page has drawn the match's first frame
            if (!m.loaded) { m.loaded = true; broadcastLobby(); }
            break;
        case 'rtc': // the page's offer of a datagram channel (rtc.js)
            // A page offers once per WebSocket; each offer tears down and
            // rebuilds a node-datachannel PeerConnection (ICE, DTLS, a UDP
            // socket), so a burst of them is pure churn. Ignore offers that
            // come faster than one a second.
            if (msg.type === 'offer') {
                const now = performance.now();
                if (now - m.lastOfferAt < OFFER_MIN_MS) break;
                m.lastOfferAt = now;
                conn.who = m.name;
                rtc.offer(conn, msg, json);
            }
            break;
        case 'rtc-ice':
            rtc.candidate(conn, msg);
            break;
        case 'away': // the page is closing this socket to open another (a quiet link): hold the seat
            m.wantsHold = true;
            break;
        case 'done': // this page is out of the match: it ended, or the page left it
            m.done = true;
            leave(m);
            break;
        }
    };
}
