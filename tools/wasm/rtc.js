'use strict';
/* The relay's end of each page's datagram channel: a WebRTC data channel,
 * unordered and never retransmitted, that carries the game's input packets
 * (relay.js forward). Over the page's WebSocket one lost TCP segment holds
 * back every packet behind it until it is retransmitted -- 300 ms and more on
 * Windows -- and in a lockstep match everybody waits on the slowest stream.
 * The engine already repeats its last twelve frames in every packet
 * (src/port/netplay/pc_netplay.c send_inputs), so over datagrams a lost one
 * costs about one packet interval instead.
 *
 * Optional, per page, and never required: the WebSocket stays, carries the
 * lobby, the hashes and MSG_BYE, and carries the inputs too whenever this
 * channel is not up -- no node-datachannel installed, MELEE_RTC=0, the UDP
 * port not reachable from the player, an old page, or a channel gone quiet.
 *
 * Signalling is over the page's WebSocket, JSON:
 *   page -> relay  { t:'rtc', type:'offer', sdp }   then { t:'rtc-ice', cand, mid }...
 *   relay -> page  { t:'rtc', type:'answer', sdp }  then { t:'rtc-ice', cand, mid }...
 * The page creates the channel; the relay answers.
 *
 * One UDP port for every page (ICE UDP mux):
 *   MELEE_RTC_PORT  the port, default 9400. Publish it as UDP, same number
 *                   inside and out, and bound to the host's one LAN address
 *                   (docker: "${MELEE_RTC_BIND}:9400:9400/udp" -- see
 *                   deploy/web/compose-melee.block), not 0.0.0.0.
 *   MELEE_RTC_HOST  the address the players reach that port on, when it is
 *                   not one of this machine's own (in a container, the host's
 *                   LAN address). The relay then announces exactly that
 *                   candidate. A comma list announces each, in order of
 *                   preference -- "192.168.1.10,203.0.113.7": players on the
 *                   LAN reach the first, players outside it the forwarded
 *                   public one, and neither needs the router's NAT reflection.
 *   MELEE_STUN      a STUN server (or comma list) the players' browsers use
 *                   to learn their own public address -- "stun:host:3478".
 *                   Handed to each page in the lobby state (lobby.js builds
 *                   its RTCPeerConnection with it). With it, a player outside
 *                   the LAN needs NO inbound port forward: the relay sends its
 *                   ICE check toward the browser's public candidate, which
 *                   opens an outbound state on the router, and the browser's
 *                   packets come in on that state. The router's outbound NAT
 *                   must then keep this port stable (pfSense: "static port"
 *                   on an outbound NAT rule for this host, UDP MELEE_RTC_PORT;
 *                   its default picks a fresh random port per destination,
 *                   which nothing can punch through). Unset, the browser
 *                   offers only its LAN address, and a remote player reaches
 *                   the relay only through a forward. A player whose own
 *                   router will not cooperate simply stays on the WebSocket.
 *   MELEE_RTC=0     off.
 *
 * Liveness: each side sends a one-byte keepalive every 250 ms, and a channel
 * that has delivered nothing for 1.5 s is not used until it does again, so a
 * path that died quietly (Wi-Fi roaming, a NAT forgetting the mapping) costs
 * at most that long before the WebSocket takes over.
 */

const KEEPALIVE_MS = 250;
const QUIET_MS = 1500;
const PORT = parseInt(process.env.MELEE_RTC_PORT || '9400', 10);
const HOSTS = (process.env.MELEE_RTC_HOST || '').split(',').map((h) => h.trim()).filter(Boolean);
const HOST = HOSTS.join(',');
// Only stun: / stuns: URLs: a turn: server would need credentials this does
// not carry, and the page must not be handed arbitrary URLs to dial.
const STUN = (process.env.MELEE_STUN || '').split(',').map((u) => u.trim())
    .filter((u) => /^stuns?:[A-Za-z0-9.-]+(:\d{1,5})?$/.test(u));

let ndc = null;
let why = '';
if (process.env.MELEE_RTC === '0') why = 'off (MELEE_RTC=0)';
else {
    try { ndc = require('node-datachannel'); }
    catch (e) { why = `off (node-datachannel not installed: ${e.code || e.message})`; }
}
if (ndc) {
    try { ndc.initLogger('Error'); } catch (e) { /* older versions */ }
}
exports.available = !!ndc;
exports.iceServers = STUN;   // what the pages build their RTCPeerConnection with
exports.describe = () => (ndc ? `on, UDP ${PORT}${HOST ? ` announced as ${HOST}` : ''}` +
                                `${STUN.length ? `, pages use STUN ${STUN.join(',')}` : ', no STUN (LAN or a forward only)'}` : why);

const KEEPALIVE = Buffer.from([0]);

/* Answer a page's offer on `conn` (serve.js's WebSocket connection). When the
 * channel opens, conn.rtc.usable() says whether to send through it, and what
 * arrives on it is handed to conn.onmessage like a WebSocket message. */
function offer(conn, msg, json) {
    if (!ndc) { json(conn, { t: 'rtc', type: 'none' }); return; }
    close(conn);
    let pc;
    try {
        pc = new ndc.PeerConnection('melee', {
            iceServers: [],
            portRangeBegin: PORT, portRangeEnd: PORT,
            enableIceUdpMux: true,
            maxMessageSize: 4096,
        });
    } catch (e) {
        console.log(`[rtc] could not make a peer connection: ${e.message}`);
        json(conn, { t: 'rtc', type: 'none' });
        return;
    }
    const r = { pc, dc: null, lastRx: 0, rx: 0, tx: 0, timer: null, closed: false, announced: false,
                t0: performance.now(), cands: [], probe: null };
    conn.rtc = r;
    /* What the page offered and what came of it, in the log: a channel that
     * never opens is otherwise silent on this side, and the page's report
     * says only "did not open". The page's candidates say where it is (a
     * private host address, a srflx one from STUN = its router's mapping),
     * and the outcome says whether the path through the routers worked. */
    const who = () => conn.who || 'a page';
    const offered = () => r.cands.length ? r.cands.map((c) => `${c.typ} ${c.addr}:${c.port}`).join(', ') : 'none yet';
    r.probe = setTimeout(() => {
        if (r.closed || (r.dc && r.dc.isOpen())) return;
        console.log(`[rtc] no channel for ${who()} after 10 s; the page's candidates: ${offered()}`);
    }, 10000);
    r.usable = () => !!(r.dc && !r.closed && r.dc.isOpen() && performance.now() - r.lastRx < QUIET_MS);
    r.send = (buf) => {
        try { if (r.dc.sendMessageBinary(buf)) { r.tx++; return true; } } catch (e) { /* closed under us */ }
        return false;
    };
    const candidates = (c) => {
        if (!HOSTS.length) return [c];
        // "candidate:<f> <comp> <proto> <prio> <addr> <port> typ host ..." -- the
        // addresses the players can reach, once, and only the UDP port we publish.
        // Each its own foundation, and the earlier ones preferred.
        const f = c.replace(/^a=/, '').split(' ');
        if (f.length < 8 || f[2].toLowerCase() !== 'udp' || +f[5] !== PORT || r.announced) return [];
        r.announced = true;
        return HOSTS.map((h, i) => {
            const g = f.slice();
            g[0] = `candidate:${9400 + i}`;
            g[3] = String(2130706431 - i * 256);
            g[4] = h;
            return g.join(' ');
        });
    };
    pc.onLocalDescription((sdp, type) => {
        if (HOST) sdp = sdp.split(/\r?\n/).filter((l) => !/^a=candidate:/.test(l)).join('\r\n');
        json(conn, { t: 'rtc', type: String(type).toLowerCase(), sdp });
    });
    pc.onLocalCandidate((cand, mid) => {
        for (const c of candidates(cand)) json(conn, { t: 'rtc-ice', cand: c, mid });
    });
    pc.onStateChange((state) => {
        const dt = ((performance.now() - r.t0) / 1000).toFixed(1);
        if (state === 'connected') {
            let pair = '';
            try {
                const p = pc.getSelectedCandidatePair();
                if (p && p.remote) pair = ` via ${p.remote.type} ${p.remote.address}:${p.remote.port} (ours ${p.local ? p.local.address + ':' + p.local.port : '?'})`;
            } catch (e) { /* older binding */ }
            console.log(`[rtc] ICE connected for ${who()} in ${dt} s${pair}; the page offered: ${offered()}`);
        }
        if (state === 'failed') console.log(`[rtc] ICE failed for ${who()} after ${dt} s; the page offered: ${offered()}`);
        if (state === 'failed' || state === 'closed') close(conn);
    });
    pc.onDataChannel((dc) => {
        if (r.closed) { try { dc.close(); } catch (e) { /* */ } return; }
        r.dc = dc;
        dc.onOpen(() => {});
        dc.onMessage((m) => {
            if (typeof m === 'string') return;
            const buf = Buffer.isBuffer(m) ? m : Buffer.from(m);
            r.lastRx = performance.now();
            if (buf.length <= 1) return;                       // a keepalive
            r.rx++;
            if (buf[0] === 0x7B) return;                       // JSON belongs on the WebSocket
            if (conn.onmessage) conn.onmessage(buf, true);
        });
        dc.onClosed(() => close(conn));
        r.timer = setInterval(() => {
            try { if (dc.isOpen()) dc.sendMessageBinary(KEEPALIVE); } catch (e) { /* */ }
        }, KEEPALIVE_MS);
        console.log(`[rtc] datagram channel up for ${conn.who || 'a page'}`);
    });
    try {
        pc.setRemoteDescription(String(msg.sdp || ''), 'offer');
    } catch (e) {
        console.log(`[rtc] bad offer: ${e.message}`);
        close(conn);
        json(conn, { t: 'rtc', type: 'none' });
    }
}

function candidate(conn, msg) {
    const r = conn.rtc;
    if (!r || r.closed || typeof msg.cand !== 'string' || !msg.cand) return;
    // "candidate:<f> <comp> <proto> <prio> <addr> <port> typ <type> ..."
    const f = msg.cand.replace(/^a=/, '').split(' ');
    if (f.length >= 8 && f[6] === 'typ' && r.cands.length < 32) r.cands.push({ typ: f[7], addr: f[4], port: f[5] });
    try { r.pc.addRemoteCandidate(msg.cand, typeof msg.mid === 'string' ? msg.mid : '0'); }
    catch (e) { /* an mDNS name, or a candidate for a closed connection */ }
}

function close(conn) {
    const r = conn.rtc;
    if (!r || r.closed) return;
    r.closed = true;
    clearInterval(r.timer);
    clearTimeout(r.probe);
    try { if (r.dc) r.dc.close(); } catch (e) { /* */ }
    try { r.pc.close(); } catch (e) { /* */ }
}

exports.offer = offer;
exports.candidate = candidate;
exports.close = close;
