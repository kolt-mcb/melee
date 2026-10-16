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
 * of each player's MSG_INPUT, so that when a player leaves it can tell the
 * rest exactly which frame that player's input stops after (MSG_BYE).
 */

const crypto = require('crypto');

const MAX_PLAYERS = 4;
const MAGIC = 0x4D4E5031, MSG_INPUT = 4, MSG_BYE = 6, HDR = 8;

const room = {
    members: [],         // in join order: { conn, id, name, ckind, color, slot, lastFrame }
    rules: { stage: 31, stocks: 4, minutes: 8, cpus: 0, cpulevel: 9 },
    match: null,         // { players: [member], started }
};
let nextId = 1;

const json = (conn, obj) => conn.send(Buffer.from(JSON.stringify(obj)));

function lobbyState(forId) {
    const host = room.members[0];
    return {
        t: 'lobby',
        you: forId,
        host: host ? host.id : null,
        players: room.members.slice(0, MAX_PLAYERS).map((m) => ({
            id: m.id, name: m.name, ckind: m.ckind, color: m.color,
            inMatch: !!(room.match && room.match.players.includes(m)),
        })),
        waiting: Math.max(0, room.members.length - MAX_PLAYERS),
        rules: room.rules,
        inMatch: !!room.match,
    };
}

function broadcastLobby() {
    for (const m of room.members) json(m.conn, lobbyState(m.id));
}

function startMatch() {
    const players = room.members.slice(0, MAX_PLAYERS);
    if (players.length < 1 || room.match) return;
    const seed = crypto.randomBytes(4).readUInt32BE(0);
    players.forEach((m, i) => { m.slot = i; m.lastFrame = -1; });
    room.match = { players };
    const lineup = players.map((m) => ({ slot: m.slot, name: m.name, ckind: m.ckind, color: m.color }));
    for (const m of players) {
        json(m.conn, { t: 'start', slot: m.slot, players: lineup, seed,
                       rules: room.rules, delay: 3 });
    }
    console.log(`[relay] match: ${lineup.map((p) => `${p.name}(${p.ckind})`).join(' vs ')}, ` +
                `stage ${room.rules.stage}, seed ${seed.toString(16)}`);
    broadcastLobby();
}

function endMatchIfEmpty() {
    if (room.match && !room.match.players.some((m) => room.members.includes(m) && !m.done)) {
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
    }
    for (const m of room.match.players) {
        if (m !== from && room.members.includes(m)) m.conn.send(buf);
    }
}

function leave(m) {
    const i = room.members.indexOf(m);
    if (i < 0) return;
    room.members.splice(i, 1);
    if (room.match && room.match.players.includes(m) && !m.done) {
        const bye = Buffer.alloc(HDR + 4);
        bye.writeUInt32BE(MAGIC, 0);
        bye[4] = MSG_BYE;
        bye[5] = m.slot;
        bye.writeUInt32BE(m.lastFrame >>> 0, HDR);
        for (const o of room.match.players) {
            if (o !== m && room.members.includes(o)) o.conn.send(bye);
        }
        console.log(`[relay] ${m.name} left the match after frame ${m.lastFrame}`);
    }
    endMatchIfEmpty();
    broadcastLobby();
}

exports.join = (conn) => {
    const m = { conn, id: nextId++, name: `Player ${nextId - 1}`, ckind: 8, color: 0,
                slot: -1, lastFrame: -1, done: false };
    room.members.push(m);
    conn.onclose = () => leave(m);
    conn.onmessage = (buf) => {
        if (buf[0] !== 0x7B /* '{' */) return forward(m, buf);
        let msg;
        try { msg = JSON.parse(buf.toString('utf8')); } catch (e) { return; }
        const isHost = room.members[0] === m;
        switch (msg.t) {
        case 'hello':
        case 'pick':
            if (typeof msg.name === 'string' && msg.name.trim())
                m.name = msg.name.trim().slice(0, 16);
            if (Number.isInteger(msg.ckind) && msg.ckind >= 0 && msg.ckind < 26) m.ckind = msg.ckind;
            if (Number.isInteger(msg.color) && msg.color >= 0 && msg.color < 6) m.color = msg.color;
            broadcastLobby();
            break;
        case 'rules':
            if (!isHost || room.match) break;
            if (Number.isInteger(msg.stage) && msg.stage >= 2 && msg.stage <= 32) room.rules.stage = msg.stage;
            if (Number.isInteger(msg.stocks) && msg.stocks >= 1 && msg.stocks <= 99) room.rules.stocks = msg.stocks;
            if (Number.isInteger(msg.minutes) && msg.minutes >= 0 && msg.minutes <= 99) room.rules.minutes = msg.minutes;
            if (Number.isInteger(msg.cpus) && msg.cpus >= 0 && msg.cpus <= 3) room.rules.cpus = msg.cpus;
            if (Number.isInteger(msg.cpulevel) && msg.cpulevel >= 1 && msg.cpulevel <= 9) room.rules.cpulevel = msg.cpulevel;
            broadcastLobby();
            break;
        case 'start':
            if (isHost) startMatch();
            break;
        case 'done': // this page's match is over; it is going back to the lobby
            m.done = true;
            endMatchIfEmpty();
            break;
        }
    };
    broadcastLobby();
};
