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
 * The input delay comes from the players' measured round trips. The relay
 * pings every page twice a second while it is in the lobby, and at Start
 * picks the frames of delay that cover the slowest pair's path through it:
 * a player's input reaches another in about half of each one's round trip,
 * and the delay is what hides that. MELEE_NET_DELAY=<n> on the server fixes
 * it instead (3 was the old constant).
 */

const crypto = require('crypto');

const MAX_PLAYERS = 8; // the PC build's fighter slots
const MAX_LOCAL = 4;   // players at one page: the four pads the engine reads
const PING_MS = 500, PING_KEEP = 20;
const FRAME_MS = 1000 / 60;
const FIXED_DELAY = process.env.MELEE_NET_DELAY ? parseInt(process.env.MELEE_NET_DELAY, 10) : 0;
const MAGIC = 0x4D4E5031, MSG_INPUT = 4, MSG_BYE = 6, HDR = 8;
/* How long a seat is held for a page whose socket dropped mid-match. The
 * engine stalls on the missing input and resends what the peer has not
 * acknowledged, so a page that is back inside this window picks the match
 * up where it stopped; lobby.js retries for a little less than this. */
const RESUME_MS = 30000;

const room = {
    members: [],         // in join order: { conn, id, name, ckind, color, guests, slots, lastFrame }
    rules: { stage: 31, kind: 'stock', stocks: 4, minutes: 8, items: -1, cpus: 0, cpulevel: 9 },
    match: null,         // { players: [member], started }
};
let nextId = 1;

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
            id: m.id, seat, name: p.name, ckind: p.ckind, color: p.color, ping: pingOf(m), pre: m.pre, loaded: m.loaded,
            inMatch: !!(room.match && room.match.players.includes(m)),
        }))),
        waiting: room.members.length - in_.length,
        rules: room.rules,
        inMatch: !!room.match,
    };
}

function broadcastLobby() {
    for (const m of room.members) json(m.conn, lobbyState(m.id));
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
 * the peer's frame began. Two at least -- one already failed on a clean
 * local link -- and at most 10. */
function chooseDelay(players) {
    if (FIXED_DELAY > 0) return { delay: FIXED_DELAY, why: 'fixed (MELEE_NET_DELAY)' };
    if (players.length < 2) return { delay: 3, why: 'one player' };
    const pings = players.map(pingOf);
    if (pings.some((p) => p === null)) return { delay: 3, why: 'not enough ping samples' };
    const [a, b] = [...pings].sort((x, y) => y - x);
    const oneWay = (a + b) / 2;
    const delay = Math.max(2, Math.min(10, Math.ceil(oneWay / FRAME_MS) + 1));
    return { delay, why: `pings ${pings.join('/')} ms, slowest path ~${oneWay.toFixed(0)} ms` };
}

function startMatch() {
    const players = seated();
    if (players.length < 1 || room.match) return;
    const seed = crypto.randomBytes(4).readUInt32BE(0);
    const lineup = [];
    for (const m of players) {
        m.slots = [];
        m.lastFrames = {};
        for (const p of seatsOf(m)) {
            m.slots.push(lineup.length);
            m.lastFrames[lineup.length] = -1;
            lineup.push({ slot: lineup.length, name: p.name, ckind: p.ckind, color: p.color });
        }
        m.slot = m.slots[0];
        m.lastFrame = -1;
        m.loaded = false;
    }
    room.match = { players, seed, watch: setInterval(watchMatch, 5000) };
    players.forEach((m) => { m.fwdIn = 0; m.fwdOut = 0; m.seenFrame = -1; m.stuckSince = 0; });
    const { delay, why } = chooseDelay(players);
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
    }
    if (!stuck && ++watchTick % 12 !== 0) return;
    console.log(`[relay] match ${stuck ? 'STUCK' : 'ok'}: ` + ps.map((m) =>
        `${m.name}(slot ${m.slots.join('+')}) input to ${m.lastFrame}` +
        (now - m.stuckSince >= 4000 && m.lastFrame >= 0 ? ` [still ${((now - m.stuckSince) / 1000).toFixed(0)} s]` : '') +
        `, in ${m.fwdIn} pkts, out ${m.fwdOut} pkts, unsent ${m.conn.socket ? m.conn.socket.writableLength : '?'} B` +
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

function forward(from, buf) {
    if (!room.match || !room.match.players.includes(from)) return;
    if (buf.length >= HDR + 5 && buf.readUInt32BE(0) === MAGIC && buf[4] === MSG_INPUT) {
        const last = buf.readUInt32BE(HDR) + buf[HDR + 4] - 1;
        if (last > from.lastFrame) from.lastFrame = last;
        const slot = buf[5];
        if (slot in from.lastFrames && last > from.lastFrames[slot]) from.lastFrames[slot] = last;
    }
    from.fwdIn = (from.fwdIn || 0) + 1;
    for (const m of room.match.players) {
        if (m !== from && room.members.includes(m)) { m.conn.send(buf); m.fwdOut = (m.fwdOut || 0) + 1; }
    }
}

function leave(m) {
    const i = room.members.indexOf(m);
    if (i < 0) return;
    room.members.splice(i, 1);
    if (room.match && room.match.players.includes(m) && !m.done) {
        // One per slot the page played, each with that slot's own last
        // frame: the streams are separate packets and need not end together.
        for (const slot of m.slots) {
            const bye = Buffer.alloc(HDR + 4);
            bye.writeUInt32BE(MAGIC, 0);
            bye[4] = MSG_BYE;
            bye[5] = slot;
            bye.writeUInt32BE(m.lastFrames[slot] >>> 0, HDR);
            for (const o of room.match.players) {
                if (o !== m && room.members.includes(o)) o.conn.send(bye);
            }
        }
        console.log(`[relay] ${m.name} left the match after frame ${m.lastFrame}`);
    }
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
function parked(id, seed) {
    return room.members.find((m) => m.id === id && m.away && room.match &&
                                    room.match.seed === seed && room.match.players.includes(m)) || null;
}

exports.join = (conn) => {
    const m = { conn, id: nextId++, name: `Player ${nextId - 1}`, ckind: 8, color: 0, guests: [],
                slot: -1, slots: [], lastFrame: -1, lastFrames: {}, done: false, rtts: [], pingAt: 0, pingSeq: 0, pre: null,
                away: null };
    room.members.push(m);
    attach(m, conn);
    broadcastLobby();
};

/* Wire a socket to a member: at join, and again when a page whose socket
 * dropped mid-match comes back on a new one. */
function attach(m, conn) {
    m.conn = conn;
    /* Lobby only: in a match the page's thread is busy and a reply would
     * measure its frame time, not the network. */
    clearInterval(m.pinger);
    m.pinger = setInterval(() => {
        if (room.match && room.match.players.includes(m)) return;
        m.pingSeq++;
        m.pingAt = performance.now();
        json(m.conn, { t: 'ping', s: m.pingSeq });
    }, PING_MS);
    conn.onclose = () => {
        clearInterval(m.pinger);
        if (room.match && room.match.players.includes(m) && !m.done) {
            // Hold the seat: the page may be back before anyone notices.
            m.away = setTimeout(() => { m.away = null; leave(m); }, RESUME_MS);
            console.log(`[relay] ${m.name} dropped at frame ${m.lastFrame}; holding the seat ${RESUME_MS / 1000} s`);
            return;
        }
        leave(m);
    };
    conn.onmessage = (buf) => {
        if (buf[0] !== 0x7B /* '{' */) return forward(m, buf);
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
                const p = parked(msg.resume.id, msg.resume.seed);
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
        case 'done': // this page's match is over; it is going back to the lobby
            m.done = true;
            endMatchIfEmpty();
            break;
        }
    };
}
