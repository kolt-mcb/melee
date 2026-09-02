/* Drive headless Chromium over CDP: load the wasm page, collect its console
 * output, screenshot the canvas after a settling period.
 *
 * Exists because --screenshot fires on load, which for this page is while the
 * 300 MB asset bundle is still downloading -- every such shot is of the
 * emscripten progress bar. Here the wait is explicit and the console comes
 * back with it, which is the half that actually says what went wrong.
 *
 * usage: node shot.js <url> <out.png> [waitSeconds]
 */
const { spawn } = require('child_process');
const fs = require('fs');

const url = process.argv[2];
const out = process.argv[3];
const waitS = parseInt(process.argv[4] || '90', 10);
const PORT = 9222;

const chrome = spawn('chromium', [
    '--headless=new', '--no-sandbox', '--disable-gpu',
    '--enable-unsafe-swiftshader', '--use-gl=angle', '--use-angle=swiftshader',
    '--window-size=1280,720', '--hide-scrollbars',
    `--remote-debugging-port=${PORT}`, 'about:blank',
], { stdio: ['ignore', 'ignore', 'pipe'] });

const sleep = (ms) => new Promise(r => setTimeout(r, ms));

async function targetWs() {
    for (let i = 0; i < 60; i++) {
        try {
            const r = await fetch(`http://127.0.0.1:${PORT}/json/list`);
            const list = await r.json();
            const page = list.find(t => t.type === 'page');
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
    const send = (method, params = {}) => new Promise(res => {
        const n = ++id;
        pending.set(n, res);
        ws.send(JSON.stringify({ id: n, method, params }));
    });

    const logLines = [];
    await new Promise(r => ws.onopen = r);
    ws.onmessage = (ev) => {
        const m = JSON.parse(ev.data);
        if (m.id && pending.has(m.id)) { pending.get(m.id)(m.result); pending.delete(m.id); return; }
        if (m.method === 'Runtime.consoleAPICalled') {
            const text = (m.params.args || [])
                .map(a => a.value !== undefined ? a.value : (a.description || '')).join(' ');
            logLines.push(`[${m.params.type}] ${text}`);
        } else if (m.method === 'Runtime.exceptionThrown') {
            const d = m.params.exceptionDetails;
            logLines.push(`[exception] ${d.text} ${d.exception ? d.exception.description : ''}`);
        }
    };

    await send('Runtime.enable');
    await send('Page.enable');
    await send('Page.navigate', { url });

    /* Fixed wait, not a poll. Runtime.evaluate needs the page's JS thread,
     * and a wasm frame loop that fails to yield holds exactly that thread --
     * so polling hangs precisely in the case worth diagnosing. Console
     * events still arrive whenever the thread does breathe, and they are the
     * useful half. */
    await sleep(waitS * 1000);

    /* Clip to the canvas when the page is responsive enough to report where
     * it is: the emscripten shell's header and letterboxing are not the
     * thing being looked at. Falls back to the whole viewport. */
    let clip;
    const rect = await Promise.race([
        send('Runtime.evaluate', {
            expression: '(() => { const c = document.getElementById("canvas");'
                + ' if (!c) return null; const r = c.getBoundingClientRect();'
                + ' return JSON.stringify({x:r.x,y:r.y,w:r.width,h:r.height}); })()',
            returnByValue: true,
        }),
        sleep(10000).then(() => null),
    ]);
    if (rect && rect.result && rect.result.value) {
        const r = JSON.parse(rect.result.value);
        if (r.w > 0 && r.h > 0) {
            clip = { x: r.x, y: r.y, width: r.w, height: r.h, scale: 1 };
        }
    }

    /* A blocked thread never answers captureScreenshot, so give it a
     * deadline and report the console either way. */
    const shot = await Promise.race([
        send('Page.captureScreenshot', clip ? { format: 'png', clip } : { format: 'png' }),
        sleep(20000).then(() => null),
    ]);
    if (shot && shot.data) {
        fs.writeFileSync(out, Buffer.from(shot.data, 'base64'));
    } else {
        console.log('NO SCREENSHOT: page thread did not respond (blocked?)');
    }
    fs.writeFileSync(out + '.console.txt', logLines.join('\n'));
    console.log(`console lines: ${logLines.length}`);
    ws.close();
    chrome.kill();
    process.exit(0);
})().catch(e => { console.error(e); chrome.kill(); process.exit(1); });
