/* Interrupt a wedged page and print the wasm stack it was spinning in.
 *
 * A frame loop that never yields holds the page's only thread, so every other
 * diagnostic (console, screenshot, Runtime.evaluate) is unavailable exactly
 * when it is wanted. Debugger.pause still lands, because V8 checks its
 * interrupt flag at wasm loop back-edges -- which is precisely where an
 * infinite loop spends its time.
 *
 * The frames come back as function indices; tools/wasm/fnname.py turns those
 * into names via the module's name section.
 *
 * usage: node pause_stack.js <url> <waitSeconds>
 */
const { spawn } = require('child_process');

const url = process.argv[2];
const waitS = parseInt(process.argv[3] || '120', 10);
const PORT = 9224;

const chrome = spawn('chromium', [
    '--headless=new', '--no-sandbox', '--disable-gpu',
    '--enable-unsafe-swiftshader', '--use-gl=angle', '--use-angle=swiftshader',
    '--window-size=1280,720', `--remote-debugging-port=${PORT}`, 'about:blank',
], { stdio: ['ignore', 'ignore', 'ignore'] });

const sleep = (ms) => new Promise(r => setTimeout(r, ms));

async function targetWs() {
    for (let i = 0; i < 60; i++) {
        try {
            const r = await fetch(`http://127.0.0.1:${PORT}/json/list`);
            const page = (await r.json()).find(t => t.type === 'page');
            if (page && page.webSocketDebuggerUrl) return page.webSocketDebuggerUrl;
        } catch (e) { /* not up yet */ }
        await sleep(500);
    }
    throw new Error('devtools never came up');
}

(async () => {
    const ws = new WebSocket(await targetWs());
    let id = 0;
    const pending = new Map();
    let paused = null;
    const send = (method, params = {}) => new Promise(res => {
        const n = ++id;
        pending.set(n, res);
        ws.send(JSON.stringify({ id: n, method, params }));
    });

    await new Promise(r => ws.onopen = r);
    ws.onmessage = (ev) => {
        const m = JSON.parse(ev.data);
        if (m.id && pending.has(m.id)) { pending.get(m.id)(m.result); pending.delete(m.id); return; }
        if (m.method === 'Debugger.paused' && !paused) paused = m.params;
    };

    await send('Runtime.enable');
    await send('Debugger.enable');
    await send('Page.enable');
    await send('Page.navigate', { url });
    await sleep(waitS * 1000);

    console.log(`--- pausing after ${waitS}s ---`);
    send('Debugger.pause');

    for (let i = 0; i < 40 && !paused; i++) await sleep(500);
    if (!paused) {
        console.log('NOT PAUSED: the thread never reached an interrupt check');
        chrome.kill();
        process.exit(2);
    }

    for (const f of (paused.callFrames || []).slice(0, 25)) {
        const loc = f.location || {};
        const url_ = (f.url || '').split('/').pop();
        console.log(`${f.functionName || '(anon)'}  [${url_}:${loc.lineNumber}:${loc.columnNumber}]`);
    }
    ws.close();
    chrome.kill();
    process.exit(0);
})().catch(e => { console.error(e); chrome.kill(); process.exit(1); });
