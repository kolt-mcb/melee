#!/usr/bin/env node
/* Host the web build on a home network: the page, the game, and the netplay
 * relay, from one process with no dependencies.
 *
 *   node tools/wasm/serve.js --iso "/path/to/Melee.iso" [--port 8443] [--http]
 *
 * - The page and its files, from build/wasm.
 * - /disc.iso: the host's own disc image, by byte range. The page reads the
 *   game from it as it needs it (tools/wasm/iso_fs.js); nothing is copied.
 *   Serve it only on your own network: it is your copy of the game.
 * - /net: a WebSocket relay that carries netplay between the players' pages
 *   (src/port/pc_web_net.c).
 *
 * HTTPS by default, with a self-signed certificate made on first run in
 * build/wasm/cert: browsers expose gamepads only to secure contexts, and a
 * LAN address over plain HTTP is not one. Each device accepts the
 * certificate warning once. --http serves plain HTTP (fine on localhost).
 */
'use strict';
const fs = require('fs');
const path = require('path');
const http = require('http');
const https = require('https');
const crypto = require('crypto');
const os = require('os');
const { execFileSync } = require('child_process');

const ROOT = path.resolve(__dirname, '..', '..');
const args = process.argv.slice(2);
const opt = (name, dflt) => {
    const i = args.indexOf(name);
    return i >= 0 && i + 1 < args.length ? args[i + 1] : dflt;
};
const PLAIN = args.includes('--http');
const PORT = parseInt(opt('--port', PLAIN ? '8000' : '8443'), 10);
const WEB = path.resolve(opt('--dir', path.join(ROOT, 'build', 'wasm')));
const ISO = opt('--iso', process.env.MELEE_ISO || '');
const relay = require('./relay.js');

if (!ISO || !fs.existsSync(ISO)) {
    console.error('usage: node tools/wasm/serve.js --iso /path/to/Melee.iso [--port N] [--http]');
    process.exit(2);
}
const isoSize = fs.statSync(ISO).size;

const TYPES = {
    '.html': 'text/html; charset=utf-8', '.js': 'text/javascript',
    '.wasm': 'application/wasm', '.json': 'application/json',
    '.png': 'image/png', '.css': 'text/css', '.ico': 'image/x-icon',
};

function serveDisc(req, res) {
    const base = { 'Accept-Ranges': 'bytes', 'Content-Type': 'application/octet-stream',
                   'Cache-Control': 'no-store' };
    const m = /^bytes=(\d+)-(\d*)$/.exec(req.headers.range || '');
    if (!m) {
        res.writeHead(req.method === 'HEAD' ? 200 : 416,
                      { ...base, 'Content-Length': req.method === 'HEAD' ? isoSize : 0 });
        return res.end();
    }
    const start = parseInt(m[1], 10);
    const end = Math.min(m[2] ? parseInt(m[2], 10) : isoSize - 1, isoSize - 1);
    if (start > end) {
        res.writeHead(416, { ...base, 'Content-Range': `bytes */${isoSize}` });
        return res.end();
    }
    res.writeHead(206, { ...base, 'Content-Length': end - start + 1,
                         'Content-Range': `bytes ${start}-${end}/${isoSize}` });
    if (req.method === 'HEAD') return res.end();
    fs.createReadStream(ISO, { start, end }).pipe(res);
}

function handler(req, res) {
    let p = decodeURIComponent(new URL(req.url, 'http://x').pathname);
    // Container healthcheck: answers without touching the disc, so a missing or
    // unreadable ISO still lets the page load and report its own error.
    if (p === '/healthz') {
        res.writeHead(200, { 'Content-Type': 'text/plain', 'Cache-Control': 'no-store' });
        return res.end('ok');
    }
    if (p === '/disc.iso') return serveDisc(req, res);
    if (p === '/') p = '/melee.html';
    const file = path.join(WEB, path.normalize(p).replace(/^(\.\.[/\\])+/, ''));
    if (!file.startsWith(WEB + path.sep)) { res.writeHead(403); return res.end(); }
    fs.stat(file, (err, st) => {
        if (err || !st.isFile()) { res.writeHead(404); return res.end('not found'); }
        res.writeHead(200, { 'Content-Type': TYPES[path.extname(file)] || 'application/octet-stream',
                             'Content-Length': st.size, 'Cache-Control': 'no-cache' });
        if (req.method === 'HEAD') return res.end();
        fs.createReadStream(file).pipe(res);
    });
}

function certificate() {
    const dir = path.join(WEB, 'cert');
    const key = path.join(dir, 'key.pem'), crt = path.join(dir, 'cert.pem');
    if (!fs.existsSync(key) || !fs.existsSync(crt)) {
        fs.mkdirSync(dir, { recursive: true });
        const ips = lanAddresses();
        const san = ['DNS:localhost', 'IP:127.0.0.1', ...ips.map((a) => `IP:${a}`),
                     `DNS:${os.hostname()}`, `DNS:${os.hostname()}.local`].join(',');
        execFileSync('openssl', ['req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '3650',
                                 '-keyout', key, '-out', crt, '-subj', '/CN=melee-web',
                                 '-addext', `subjectAltName=${san}`], { stdio: 'ignore' });
    }
    return { key: fs.readFileSync(key), cert: fs.readFileSync(crt) };
}

function lanAddresses() {
    return Object.values(os.networkInterfaces()).flat()
        .filter((a) => a && a.family === 'IPv4' && !a.internal).map((a) => a.address);
}

/* ---- WebSocket (RFC 6455), binary messages only ---------------------- */

function wsAccept(req, socket) {
    const key = req.headers['sec-websocket-key'];
    if (!key || (req.headers.upgrade || '').toLowerCase() !== 'websocket') {
        socket.destroy();
        return null;
    }
    const accept = crypto.createHash('sha1')
        .update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
    socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n' +
                 `Connection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
    socket.setNoDelay(true);
    const conn = { socket, onmessage: null, onclose: null, closed: false };
    let buf = Buffer.alloc(0);
    let frag = [];
    conn.send = (data) => {
        if (conn.closed) return;
        const n = data.length;
        const head = n < 126 ? Buffer.from([0x82, n])
            : n < 65536 ? Buffer.from([0x82, 126, n >> 8, n & 255])
            : (() => { const h = Buffer.alloc(10); h[0] = 0x82; h[1] = 127; h.writeBigUInt64BE(BigInt(n), 2); return h; })();
        socket.write(Buffer.concat([head, data]));
    };
    const close = () => {
        if (conn.closed) return;
        conn.closed = true;
        socket.destroy();
        if (conn.onclose) conn.onclose();
    };
    socket.on('data', (chunk) => {
        buf = Buffer.concat([buf, chunk]);
        for (;;) {
            if (buf.length < 2) return;
            const fin = buf[0] & 0x80, op = buf[0] & 0x0f, masked = buf[1] & 0x80;
            let len = buf[1] & 0x7f, off = 2;
            if (len === 126) { if (buf.length < 4) return; len = buf.readUInt16BE(2); off = 4; }
            else if (len === 127) { if (buf.length < 10) return; len = Number(buf.readBigUInt64BE(2)); off = 10; }
            const mask = masked ? buf.slice(off, off + 4) : null;
            if (masked) off += 4;
            if (buf.length < off + len) return;
            const payload = Buffer.from(buf.slice(off, off + len));
            buf = buf.slice(off + len);
            if (mask) for (let i = 0; i < payload.length; i++) payload[i] ^= mask[i & 3];
            if (op === 0x8) return close();
            if (op === 0x9) { socket.write(Buffer.concat([Buffer.from([0x8a, payload.length]), payload])); continue; }
            if (op === 0xA) continue;
            frag.push(payload);
            if (fin) {
                const msg = Buffer.concat(frag);
                frag = [];
                if (conn.onmessage) conn.onmessage(msg);
            }
        }
    });
    socket.on('close', close);
    socket.on('error', close);
    return conn;
}

const server = PLAIN ? http.createServer(handler) : https.createServer(certificate(), handler);
server.on('upgrade', (req, socket) => {
    const p = new URL(req.url, 'http://x').pathname;
    if (p === '/probe') {
        // Echo endpoint: proves the socket survives whatever proxy sits in
        // front (Caddy + the SSO gate) without touching the netplay relay.
        const conn = wsAccept(req, socket);
        if (conn) conn.onmessage = (msg) => conn.send(Buffer.concat([Buffer.from('echo:'), msg]));
        return;
    }
    if (p !== '/net') return socket.destroy();
    const conn = wsAccept(req, socket);
    if (conn) relay.join(conn, req);
});
server.listen(PORT, '0.0.0.0', () => {
    const scheme = PLAIN ? 'http' : 'https';
    console.log(`Melee web: serving ${path.basename(ISO)} (${(isoSize / 2 ** 20).toFixed(0)} MB)`);
    for (const a of ['localhost', ...lanAddresses()]) console.log(`  ${scheme}://${a}:${PORT}/`);
    if (!PLAIN) console.log('  (self-signed certificate: accept the browser warning once per device)');
});
