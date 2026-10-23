'use strict';
/* How a match ends, on the relay alone: fake sockets, no browser, no engine.
 *
 *   node --test tools/wasm/relay_test.js
 *
 * The timings the relay waits on are read from the environment, so the test
 * sets them to a few hundred milliseconds before loading it. Each scenario
 * drives relay.join with a conn shaped like the one serve.js wsAccept makes
 * (send, close, ping, lastSeen, onmessage, onclose) and reads what the relay
 * sent back: JSON for the lobby, game packets by their magic. */
process.env.MELEE_RESUME_MS = '200';
process.env.MELEE_LIVENESS_MS = '300';
process.env.MELEE_SWEEP_MS = '50';
process.env.MELEE_MATCH_STUCK_MS = '2000';
process.env.MELEE_WATCH_MS = '50';

const test = require('node:test');
const assert = require('node:assert/strict');
const relay = require('./relay.js');

const MAGIC = 0x4D4E5031, MSG_INPUT = 4, MSG_BYE = 6, HDR = 8, PAD_BYTES = 10;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

let addr = 0;
function page(name) {
    const conn = {
        socket: { writableLength: 0, remoteAddress: `10.0.0.${++addr}` },
        out: [], closed: false, lastSeen: Date.now(), onmessage: null, onclose: null,
        send(buf) { conn.out.push(Buffer.from(buf)); },
        ping() { /* the browser would pong; the test moves lastSeen itself */ },
        close(clean) { if (conn.closed) return; conn.closed = true; if (conn.onclose) conn.onclose(!!clean); },
    };
    relay.join(conn, { headers: {} });
    const p = {
        conn, name,
        say: (obj) => conn.onmessage(Buffer.from(JSON.stringify(obj))),
        jsons: () => conn.out.filter((b) => b[0] === 0x7B).map((b) => JSON.parse(b.toString())),
        last: (t) => p.jsons().filter((m) => m.t === t).pop(),
        byes: () => conn.out.filter((b) => b.length >= HDR + 4 && b.readUInt32BE(0) === MAGIC && b[4] === MSG_BYE)
            .map((b) => ({ slot: b[5], frame: b.readInt32BE(HDR) })),
        // A page's input for frames [base, base+count) on one of its slots.
        input(slot, base, count) {
            const b = Buffer.alloc(HDR + 5 + count * PAD_BYTES);
            b.writeUInt32BE(MAGIC, 0); b[4] = MSG_INPUT; b[5] = slot;
            b.writeUInt32BE(base, HDR); b[HDR + 4] = count;
            conn.onmessage(b);
        },
        // A fresh socket claiming this page's seat (lobby.js connect, hello.resume).
        resume() {
            const lobby = p.last('lobby'), start = p.last('start');
            const q = page(name);
            q.say({ t: 'hello', name, resume: { id: lobby.you, seed: start.seed, token: lobby.token } });
            return q;
        },
    };
    p.say({ t: 'hello', name });
    return p;
}

/* Tick every socket's lastSeen except the ones named, as pongs would. */
function alive(...pages) { for (const p of pages) p.conn.lastSeen = Date.now(); }
async function run(ms, ...pages) { for (let t = 0; t < ms; t += 20) { alive(...pages); await sleep(20); } }
/* Run until cond() holds, keeping the named pages alive; the time it took. */
async function until(cond, ms, ...pages) {
    const t0 = Date.now();
    while (!cond() && Date.now() - t0 < ms) { alive(...pages); await sleep(20); }
    assert.ok(cond(), `happened within ${ms} ms`);
    return Date.now() - t0;
}

/* A match between a and b, and the slot each got. */
function start(a, b) {
    a.say({ t: 'start' });
    const sa = a.last('start'), sb = b.last('start');
    assert.ok(sa && sb, 'both pages got the start message');
    assert.equal(relay.status().match, true);
    return { a: sa.slot, b: sb.slot, seed: sa.seed };
}

async function empty() {
    // Each scenario starts from an empty room: close whatever the last one left.
    for (;;) {
        const s = relay.status();
        if (!s.members) break;
        await sleep(400);   // the sweep finds every un-pinged socket; holds expire
    }
    assert.equal(relay.status().match, false);
}

test('a page gone silent mid-match is dropped, held, then its BYE goes out and the match ends', async () => {
    await empty();
    const a = page('A'), b = page('B');
    const slots = start(a, b);
    a.input(slots.a, 0, 12);
    await run(150, a, b);
    assert.deepEqual(b.byes(), [], 'nothing while both answer');
    // A stops answering; B keeps on.
    const took = await until(() => b.byes().length, 2000, b);
    assert.equal(a.conn.closed, true, 'the sweep closed the silent socket');
    assert.deepEqual(b.byes(), [{ slot: slots.a, frame: 11 }], 'B heard where A stopped');
    assert.ok(took >= 450 && took < 900, `liveness then the hold, not the stuck kick: ${took} ms`);
    assert.equal(relay.status().match, true, 'B is still playing');
    b.say({ t: 'done' });
    assert.equal(relay.status().match, false);
    assert.equal(relay.status().members, 0, 'a page that is done is out of the room');
});

test('pages that finish leave the room, so the host seat and Start pass on', async () => {
    await empty();
    const a = page('A'), b = page('B');
    start(a, b);
    const c = page('C');
    assert.equal(c.last('lobby').host, a.last('lobby').you, 'A is host while it plays');
    assert.equal(c.last('lobby').inMatch, true);
    a.say({ t: 'done' });
    b.say({ t: 'done' });
    const seen = c.last('lobby');
    assert.equal(seen.host, seen.you, 'C is now the host');
    assert.equal(seen.inMatch, false, 'and can start');
    assert.equal(relay.status().members, 1);
    c.conn.close(true);
});

test('a page that leaves from the loading screen frees the slot from frame 0', async () => {
    await empty();
    const a = page('A'), b = page('B');
    const slots = start(a, b);
    a.say({ t: 'done' });   // lobby.js backToLobby, before any input
    assert.deepEqual(b.byes(), [{ slot: slots.a, frame: -1 }], 'a BYE at -1: gone from frame 0');
    assert.equal(relay.status().match, true, 'B has not finished');
    b.conn.close(true);
    assert.equal(relay.status().match, false);
});

test('a clean close leaves at once; an unclean one is held and can resume', async () => {
    await empty();
    let a = page('A'), b = page('B');
    let slots = start(a, b);
    a.input(slots.a, 0, 3);
    a.conn.close(true);   // a reload, a closed tab, Leave match
    assert.deepEqual(b.byes(), [{ slot: slots.a, frame: 2 }], 'no hold on a goodbye');
    b.conn.close(true);
    await empty();

    a = page('A'); b = page('B');
    slots = start(a, b);
    a.input(slots.a, 0, 3);
    a.conn.close(false);  // the link died
    assert.deepEqual(b.byes(), [], 'held');
    assert.equal(relay.status().away, 1);
    const a2 = a.resume();
    assert.equal(a2.last('resumed').ok, true);
    assert.equal(relay.status().away, 0);
    assert.equal(relay.status().members, 2, 'the new socket is A, not a newcomer');
    a2.input(slots.a, 3, 3);
    await run(300, a2, b);
    assert.deepEqual(b.byes(), [], 'the match goes on');
    // A quiet link: the page says `away` and closes cleanly; the seat is held.
    a2.say({ t: 'away' });
    a2.conn.close(true);
    assert.deepEqual(b.byes(), []);
    await run(400, b);
    assert.deepEqual(b.byes(), [{ slot: slots.a, frame: 5 }], 'the hold ran out');
    b.conn.close(true);
});

test('a page whose engine hangs is kicked once the others would have given up', async () => {
    await empty();
    const a = page('A'), b = page('B');
    const slots = start(a, b);
    a.input(slots.a, 0, 2);
    b.input(slots.b, 0, 2);
    // Both answer pings; neither moves. (A page still loading has no input
    // and is not kicked: stuckSince stays 0 until its first packet.)
    await until(() => a.conn.closed && b.conn.closed, 3000, a, b);
    assert.equal(relay.status().match, false, 'the match ended');
    assert.equal(relay.status().members, 0);
});

test('a host gone silent in the lobby loses the seat', async () => {
    await empty();
    const a = page('A'), b = page('B');
    assert.equal(b.last('lobby').host, a.last('lobby').you);
    await run(600, b);
    assert.equal(a.conn.closed, true);
    assert.equal(b.last('lobby').host, b.last('lobby').you);
    b.conn.close(true);
});
