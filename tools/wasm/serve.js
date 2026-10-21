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
 * - /disc.z?o=&n=: one file of it, whole and gzipped, cacheable for ever --
 *   what the page reads a file through once it knows the disc's layout, so
 *   that each file crosses the host's upload once per browser.
 * - The page's own files compressed (brotli, else gzip), with ETags, so a
 *   reload revalidates the 17 MB engine instead of downloading it again.
 * - /net: a WebSocket relay that carries netplay between the players' pages
 *   (src/port/pc_web_net.c).
 * - /report: where a page sends its log, crash and frame profile
 *   (tools/wasm/lobby.js, "reports"). Each one is a JSON file in --reports
 *   (default ./reports); `node tools/wasm/report.js <file>` reads one.
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
const zlib = require('zlib');
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
const REPORTS = path.resolve(opt('--reports', process.env.MELEE_REPORTS || path.join(ROOT, 'reports')));
const REPORT_MAX_BYTES = 32 << 20, REPORT_MAX_FILES = 2000;
const relay = require('./relay.js');

if (!ISO || !fs.existsSync(ISO)) {
    console.error('usage: node tools/wasm/serve.js --iso /path/to/Melee.iso [--port N] [--http]');
    process.exit(2);
}
const isoStat = fs.statSync(ISO);
const isoSize = isoStat.size;
// Names this disc image to the page's cache (iso_fs.js): new image, new files.
const ISO_TAG = `"${isoSize.toString(36)}-${Math.floor(isoStat.mtimeMs).toString(36)}"`;

const TYPES = {
    '.html': 'text/html; charset=utf-8', '.js': 'text/javascript',
    '.wasm': 'application/wasm', '.json': 'application/json',
    '.png': 'image/png', '.jpg': 'image/jpeg', '.jpeg': 'image/jpeg',
    '.webp': 'image/webp', '.css': 'text/css', '.ico': 'image/x-icon',
};

function serveDisc(req, res) {
    const base = { 'Accept-Ranges': 'bytes', 'Content-Type': 'application/octet-stream',
                   'Cache-Control': 'no-store', ETag: ISO_TAG };
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

/* A page's report: gzip or plain JSON, kept as a file named for when, which
 * match, who and why. Anyone who can open the page can send one, so it is
 * bounded: 32 MB a report, 2000 reports in the directory. */
function receiveReport(req, res) {
    const done = (code, text) => { res.writeHead(code, { 'Content-Type': 'text/plain' }); res.end(text); };
    if (req.method !== 'POST') return done(405, 'POST a report');
    const chunks = [];
    let size = 0;
    req.on('data', (c) => {
        size += c.length;
        if (size > REPORT_MAX_BYTES) { done(413, 'too large'); req.destroy(); return; }
        chunks.push(c);
    });
    req.on('end', () => {
        if (size > REPORT_MAX_BYTES) return;
        let report;
        try {
            let body = Buffer.concat(chunks);
            if ((req.headers['content-encoding'] || '') === 'gzip') {
                body = zlib.gunzipSync(body, { maxOutputLength: 8 * REPORT_MAX_BYTES });
            }
            report = JSON.parse(body.toString('utf8'));
            if (!report || typeof report !== 'object') throw new Error('not an object');
        } catch (e) {
            return done(400, `bad report: ${e.message}`);
        }
        try {
            fs.mkdirSync(REPORTS, { recursive: true });
            if (fs.readdirSync(REPORTS).length >= REPORT_MAX_FILES) return done(507, 'the reports directory is full');
            const clean = (v, n) => String(v == null ? '' : v).replace(/[^A-Za-z0-9_-]+/g, '-').slice(0, n) || 'x';
            const when = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19);
            const seed = report.match && report.match.seed != null ? (report.match.seed >>> 0).toString(16) : 'lobby';
            const file = path.join(REPORTS, `${when}_${seed}_${clean(report.name || req.socket.remoteAddress, 24)}_` +
                                            `${clean(report.kind, 12)}_${crypto.randomBytes(2).toString('hex')}.json`);
            report.received = { at: new Date().toISOString(), from: req.socket.remoteAddress };
            fs.writeFileSync(file, JSON.stringify(report));
            const sum = report.prof && report.prof.summary;
            console.log(`[report] ${report.name || '?'} (${report.kind || '?'})` +
                        (sum ? `: ${sum.fps} fps over ${sum.seconds} s, ${sum.over20ms} frames over 20 ms` : '') +
                        (report.crash ? `: CRASH ${String(report.crash.message).slice(0, 120)}` : '') +
                        ` -> ${path.relative(process.cwd(), file)}`);
            done(200, 'ok');
        } catch (e) {
            console.error(`[report] could not save one: ${e.message}`);
            done(500, 'could not save');
        }
    });
}

/* One file of the disc, gzipped: [o, o+n) as the page's FST says. Answers
 * are immutable (the page puts the disc's tag in the URL), so they are kept
 * compressed here too, up to 256 MB, and three friends loading the same
 * stage gzip it once. */
const Z_MAX = 32 << 20, Z_KEEP = 256 << 20;
const zCache = new Map();
let zKept = 0;
function discFile(o, n) {
    const key = `${o}:${n}`;
    let p = zCache.get(key);
    if (p) { zCache.delete(key); zCache.set(key, p); return p; }
    p = new Promise((resolve, reject) => {
        const buf = Buffer.alloc(n);
        fs.open(ISO, 'r', (err, fd) => {
            if (err) return reject(err);
            fs.read(fd, buf, 0, n, o, (err2) => {
                fs.close(fd, () => {});
                if (err2) return reject(err2);
                zlib.gzip(buf, { level: 6 }, (err3, z) => (err3 ? reject(err3) : resolve({ raw: buf, z })));
            });
        });
    });
    zCache.set(key, p);
    p.then((v) => {
        zKept += v.z.length;
        for (const [k, q] of zCache) {
            if (zKept <= Z_KEEP || k === key) break;
            zCache.delete(k);
            q.then((w) => { zKept -= w.z.length; }, () => {});
        }
    }, () => zCache.delete(key));
    return p;
}
function serveDiscFile(req, res, url) {
    const o = Number(url.searchParams.get('o')), n = Number(url.searchParams.get('n'));
    if (!Number.isInteger(o) || !Number.isInteger(n) || o < 0 || n <= 0 || n > Z_MAX || o + n > isoSize) {
        res.writeHead(400); return res.end('bad range');
    }
    discFile(o, n).then(({ raw, z }) => {
        const gz = /\bgzip\b/.test(req.headers['accept-encoding'] || '');
        const body = gz ? z : raw;
        const h = { 'Content-Type': 'application/octet-stream', 'Content-Length': body.length,
                    'Cache-Control': 'public, max-age=31536000, immutable', Vary: 'Accept-Encoding' };
        if (gz) h['Content-Encoding'] = 'gzip';
        res.writeHead(200, h);
        res.end(req.method === 'HEAD' ? undefined : body);
    }, (err) => { res.writeHead(500); res.end(err.message); });
}

/* The page's files, compressed once per version and kept: brotli for the
 * engine (17 MB -> 5 MB), which is most of a first visit. */
const COMPRESS = new Set(['.wasm', '.js', '.html', '.json', '.css']);
const packed = new Map(); // file -> { mtime, br: Promise<Buffer>, gz: Promise<Buffer> }
function packedOf(file, st, enc) {
    let e = packed.get(file);
    if (!e || e.mtime !== st.mtimeMs) {
        const raw = fs.promises.readFile(file);
        e = {
            mtime: st.mtimeMs,
            br: raw.then((b) => new Promise((ok, no) => zlib.brotliCompress(b, {
                params: { [zlib.constants.BROTLI_PARAM_QUALITY]: 9,
                          [zlib.constants.BROTLI_PARAM_SIZE_HINT]: b.length } },
                (err, z) => (err ? no(err) : ok(z))))),
            gz: raw.then((b) => new Promise((ok, no) => zlib.gzip(b, { level: 9 }, (err, z) => (err ? no(err) : ok(z))))),
        };
        e.br.catch(() => {}); e.gz.catch(() => {});
        packed.set(file, e);
    }
    return e[enc];
}

/* The page, stamped with its files' versions. A browser can rebuild a tab
 * from its cache without asking (Chrome restoring a tab it put to sleep), and
 * one player's ran last night's engine after a deploy for exactly that
 * reason. So the page is never cached, and it names lobby.js, melee.js and
 * (through lobby.js, Module.locateFile) melee.wasm with ?v=<their ETag>: a
 * new build is a new URL, which no cache can answer with an old one. */
const tagOf = (st) => `${st.size.toString(36)}-${Math.floor(st.mtimeMs).toString(36)}`;
function servePage(req, res, file) {
    let html;
    try { html = fs.readFileSync(file, 'utf8'); } catch (e) { res.writeHead(404); return res.end('not found'); }
    const v = (name) => {
        let f = path.join(WEB, name);
        if (!fs.existsSync(f)) f = path.join(ROOT, 'tools', 'wasm', name);   // the dev fallback below
        try { return tagOf(fs.statSync(f)); } catch (e) { return '0'; }
    };
    html = html.replace('<script src="lobby.js"></script>',
                        `<script src="lobby.js?v=${v('lobby.js')}" data-wasm-v="${v('melee.wasm')}"></script>`)
               .replace('src="melee.js"', `src="melee.js?v=${v('melee.js')}"`);
    const body = Buffer.from(html);
    res.writeHead(200, { 'Content-Type': TYPES['.html'], 'Content-Length': body.length,
                         'Cache-Control': 'no-store' });
    res.end(req.method === 'HEAD' ? undefined : body);
}

function handler(req, res) {
    /* The request line is whatever the client sent, and two ordinary-looking
     * shapes of it are unparseable: `GET //` (scheme-relative, no host) and
     * `GET /%zz` (a bad percent-escape). Throwing here is an uncaught exception
     * inside the request handler, which takes the page, the disc stream, the
     * reports and the netplay relay down with it -- one curl ends everybody's
     * match. Answer 400 and stay up. */
    let p;
    try { p = decodeURIComponent(new URL(req.url, 'http://x').pathname); }
    catch (e) {
        res.writeHead(400, { 'Content-Type': 'text/plain', 'Cache-Control': 'no-store' });
        return res.end('bad request');
    }
    // Container healthcheck: answers without touching the disc, so a missing or
    // unreadable ISO still lets the page load and report its own error.
    if (p === '/healthz') {
        // Also who is here, so a deploy can wait until nobody is playing
        // before it restarts this process (deploy/web/node1-apply.sh).
        res.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' });
        return res.end(JSON.stringify({ ok: true, ...relay.status() }));
    }
    if (p === '/disc.iso') return serveDisc(req, res);
    if (p === '/disc.z') return serveDiscFile(req, res, new URL(req.url, 'http://x'));
    if (p === '/report' || p.endsWith('/report')) return receiveReport(req, res);
    if (p === '/' || p === '/melee.html') return servePage(req, res, path.join(WEB, 'melee.html'));
    let file = path.join(WEB, path.normalize(p).replace(/^(\.\.[/\\])+/, ''));
    if (!file.startsWith(WEB + path.sep)) { res.writeHead(403); return res.end(); }
    /* The lobby is a file, not inline script (the gateway's CSP blocks inline),
     * and the build does not copy it next to melee.html. So a page served
     * straight out of build/wasm would 404 on it: fall back to where it lives.
     * A deploy copies it into the web root, so this is the dev path. */
    if (['lobby.js', 'load_manifest.json'].includes(path.basename(file)) && !fs.existsSync(file)) {
        file = path.join(ROOT, 'tools', 'wasm', path.basename(file));
    }
    fs.stat(file, (err, st) => {
        if (err || !st.isFile()) { res.writeHead(404); return res.end('not found'); }
        const tag = `"${st.size.toString(36)}-${Math.floor(st.mtimeMs).toString(36)}"`;
        const h = { 'Content-Type': TYPES[path.extname(file)] || 'application/octet-stream',
                    'Cache-Control': 'no-cache', ETag: tag, Vary: 'Accept-Encoding',
                    // Names the build in players' reports (lobby.js).
                    'Last-Modified': st.mtime.toUTCString() };
        // no-cache means "ask first": a reload is a 304, not the engine again.
        if ((req.headers['if-none-match'] || '').split(/\s*,\s*/).includes(tag)) {
            res.writeHead(304, h);
            return res.end();
        }
        const accept = req.headers['accept-encoding'] || '';
        const enc = !COMPRESS.has(path.extname(file)) ? null
            : /\bbr\b/.test(accept) ? 'br' : /\bgzip\b/.test(accept) ? 'gz' : null;
        if (!enc) {
            res.writeHead(200, { ...h, 'Content-Length': st.size });
            if (req.method === 'HEAD') return res.end();
            return fs.createReadStream(file).pipe(res);
        }
        packedOf(file, st, enc).then((z) => {
            res.writeHead(200, { ...h, 'Content-Length': z.length,
                                 'Content-Encoding': enc === 'br' ? 'br' : 'gzip' });
            res.end(req.method === 'HEAD' ? undefined : z);
        }, (e) => { res.writeHead(500); res.end(e.message); });
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
    // Same rule on the websocket handshake: a URL we cannot parse is a client to
    // drop, not an exception to die on.
    let p;
    try { p = new URL(req.url, 'http://x').pathname; }
    catch (e) { return socket.destroy(); }
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
// Compress the engine now, so the first player does not wait on it.
for (const f of ['melee.wasm', 'melee.js', 'melee.html']) {
    const file = path.join(WEB, f);
    fs.stat(file, (err, st) => { if (!err) packedOf(file, st, 'br'); });
}
server.listen(PORT, '0.0.0.0', () => {
    const scheme = PLAIN ? 'http' : 'https';
    console.log(`Melee web: serving ${path.basename(ISO)} (${(isoSize / 2 ** 20).toFixed(0)} MB)`);
    for (const a of ['localhost', ...lanAddresses()]) console.log(`  ${scheme}://${a}:${PORT}/`);
    if (!PLAIN) console.log('  (self-signed certificate: accept the browser warning once per device)');
    console.log(`  reports from the players' pages go to ${REPORTS}`);
});
