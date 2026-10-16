/* Drive the wasm page through a scripted session in headless Chromium: wait,
 * press keys, screenshot, and collect the console. shot.js's sibling for
 * anything past the boot -- menus, character select, a match.
 *
 * usage: node drive.js <url> <outdir> <step>...
 *   wait:S          sleep S seconds (fractions allowed)
 *   key:NAME[:MS]   press and release a key, held MS ms (default 120)
 *   hold:NAME       press a key and keep it down
 *   up:NAME         release a held key
 *   shot:LABEL      screenshot the canvas to <outdir>/LABEL.png
 *   eval:JS         evaluate JS in the page (its result goes to the console log)
 *   file:PATH       give the page's disc-image input this file (the shell's
 *                   picker, without a dialog)
 *
 * NAME is a DOM key code: Enter, KeyZ, KeyX, ArrowLeft, Space, ...
 * The console goes to <outdir>/console.txt.
 */
const { spawn } = require('child_process');
const fs = require('fs');
const path = require('path');

const [url, outdir, ...steps] = process.argv.slice(2);
const os = require('os');
/* DRIVE_PORT lets several drives run at once, each with its own profile. */
const PORT = parseInt(process.env.DRIVE_PORT || '9223', 10);
fs.mkdirSync(outdir, { recursive: true });
const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'melee-drive-'));

const chrome = spawn(process.env.CHROME || 'chromium', [
    '--headless=new', '--no-sandbox', '--disable-gpu',
    '--enable-unsafe-swiftshader', '--use-gl=angle', '--use-angle=swiftshader',
    '--autoplay-policy=no-user-gesture-required',
    '--window-size=1280,800', '--hide-scrollbars',
    `--remote-debugging-port=${PORT}`, `--user-data-dir=${profile}`, 'about:blank',
], { stdio: ['ignore', 'ignore', 'pipe'] });

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

/* What SDL's emscripten backend reads: code for the scancode, keyCode for
 * the keysym. */
const KEYCODES = {
    Enter: 13, Space: 32, Escape: 27, ShiftLeft: 16, AltLeft: 18, Slash: 191,
    ArrowLeft: 37, ArrowUp: 38, ArrowRight: 39, ArrowDown: 40,
};
function keyParams(code) {
    let keyCode = KEYCODES[code];
    let key = code;
    if (code.startsWith('Key')) { key = code.slice(3).toLowerCase(); keyCode = key.toUpperCase().charCodeAt(0); }
    if (code.startsWith('Digit')) { key = code.slice(5); keyCode = key.charCodeAt(0); }
    if (code === 'Space') key = ' ';
    return { code, key, windowsVirtualKeyCode: keyCode, nativeVirtualKeyCode: keyCode };
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
    /* A blocked page thread never answers Runtime/Page calls; never let one
     * stall the script past its own schedule. */
    const sendT = (method, params, ms = 5000) =>
        Promise.race([send(method, params), sleep(ms).then(() => null)]);

    const log = [];
    await new Promise(r => ws.onopen = r);
    ws.onmessage = (ev) => {
        const m = JSON.parse(ev.data);
        if (m.id && pending.has(m.id)) { pending.get(m.id)(m.result); pending.delete(m.id); return; }
        if (m.method === 'Runtime.consoleAPICalled') {
            const text = (m.params.args || [])
                .map(a => a.value !== undefined ? a.value : (a.description || '')).join(' ');
            log.push(`[${m.params.type}] ${text}`);
        } else if (m.method === 'Log.entryAdded') {
            /* The browser's own messages: WebGL errors arrive here. */
            log.push(`[${m.params.entry.source}] ${m.params.entry.text}`);
        } else if (m.method === 'Runtime.exceptionThrown') {
            const d = m.params.exceptionDetails;
            log.push(`[exception] ${d.text} ${d.exception ? d.exception.description : ''}`);
        }
    };

    await send('Runtime.enable');
    await send('Log.enable');
    await send('Page.enable');
    await send('Page.navigate', { url });

    const t0 = Date.now();
    const stamp = (s) => { log.push(`[drive] t=${((Date.now() - t0) / 1000).toFixed(1)}s ${s}`); };

    async function canvasClip() {
        const r = await sendT('Runtime.evaluate', {
            expression: `(() => { const c = document.getElementById('canvas');
                if (!c) return null; const b = c.getBoundingClientRect();
                return [b.x, b.y, b.width, b.height]; })()`,
            returnByValue: true,
        });
        const v = r && r.result && r.result.value;
        return v && v[2] > 0 ? { x: v[0], y: v[1], width: v[2], height: v[3], scale: 1 } : undefined;
    }

    for (const step of steps) {
        const [op, arg, arg2] = step.split(':');
        stamp(step);
        if (op === 'file') {
            const doc = await sendT('DOM.getDocument', {});
            const q = await sendT('DOM.querySelector',
                { nodeId: doc.root.nodeId, selector: 'input[type=file]' });
            await sendT('DOM.setFileInputFiles', { nodeId: q.nodeId, files: [step.slice(5)] });
        } else if (op === 'eval') {
            const r = await sendT('Runtime.evaluate',
                { expression: step.slice(5), returnByValue: true });
            log.push(`[drive] eval -> ${JSON.stringify(r && r.result && r.result.value)}`);
        } else if (op === 'wait') {
            await sleep(parseFloat(arg) * 1000);
        } else if (op === 'key' || op === 'hold' || op === 'up') {
            const p = keyParams(arg);
            if (op !== 'up') await sendT('Input.dispatchKeyEvent', { type: 'rawKeyDown', ...p });
            if (op === 'key') await sleep(parseInt(arg2 || '120', 10));
            if (op !== 'hold') await sendT('Input.dispatchKeyEvent', { type: 'keyUp', ...p });
        } else if (op === 'shot') {
            const shot = await sendT('Page.captureScreenshot',
                { format: 'png', clip: await canvasClip() }, 15000);
            if (shot) fs.writeFileSync(path.join(outdir, `${arg}.png`), Buffer.from(shot.data, 'base64'));
            else log.push(`[drive] NO SCREENSHOT for ${arg}: page thread did not respond`);
        } else {
            throw new Error(`unknown step ${step}`);
        }
    }

    fs.writeFileSync(path.join(outdir, 'console.txt'), log.join('\n'));
    console.log(`console lines: ${log.length}`);
    chrome.on('exit', () => {
        fs.rmSync(profile, { recursive: true, force: true, maxRetries: 5 });
        process.exit(0);
    });
    chrome.kill();
})().catch(e => { console.error(e); chrome.kill(); process.exit(1); });
