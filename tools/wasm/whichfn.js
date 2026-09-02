/* Name a wasm function-table index in the running page.
 *
 * A render callback shows up in C as a bare integer -- the index emscripten
 * stores in place of a function pointer. The module's name section knows what
 * that index is, and the page can ask it directly: wasmTable.get(i).name.
 *
 * usage: node whichfn.js <url> <waitSeconds> <index> [index...]
 */
const { spawn } = require('child_process');

const url = process.argv[2];
const waitS = parseInt(process.argv[3], 10);
const idxs = process.argv.slice(4).map(Number);
const PORT = 9223;

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
    const send = (method, params = {}) => new Promise(res => {
        const n = ++id;
        pending.set(n, res);
        ws.send(JSON.stringify({ id: n, method, params }));
    });
    await new Promise(r => ws.onopen = r);
    ws.onmessage = (ev) => {
        const m = JSON.parse(ev.data);
        if (m.id && pending.has(m.id)) { pending.get(m.id)(m.result); pending.delete(m.id); }
    };
    await send('Runtime.enable');
    await send('Page.enable');
    await send('Page.navigate', { url });
    await sleep(waitS * 1000);

    const expr = `(() => { const t = (typeof wasmTable !== 'undefined') ? wasmTable
        : (typeof Module !== 'undefined' && Module.wasmTable) ? Module.wasmTable : null;
        if (!t) return 'no wasmTable';
        return ${JSON.stringify(idxs)}.map(i => {
            try { const f = t.get(i); return i + ' -> ' + (f && f.name ? f.name : '(unnamed)'); }
            catch (e) { return i + ' -> ' + e.message; }
        }).join('\\n'); })()`;
    const r = await Promise.race([
        send('Runtime.evaluate', { expression: expr, returnByValue: true }),
        sleep(20000).then(() => null),
    ]);
    console.log(r && r.result ? r.result.value : 'no answer (page blocked?)');
    ws.close();
    chrome.kill();
    process.exit(0);
})().catch(e => { console.error(e); chrome.kill(); process.exit(1); });
