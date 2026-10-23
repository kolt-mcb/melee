#!/usr/bin/env node
/* Read a report a player's page sent the host (tools/wasm/serve.js keeps them
 * in reports/; tools/wasm/lobby.js "reports" says what is in one).
 *
 *   node tools/wasm/report.js reports/<file>.json [--logs N|all] [--csv out.csv]
 *   node tools/wasm/report.js reports/            # one line per report
 *
 * The frame profile's columns (src/port/render.c, web_prof_frame), in ms:
 *   wall     page clock between frames: everything, the browser's own work too
 *   game     simulation + GX capture, outside render_present (less net)
 *   net      waiting on other players' input (pc_netplay.c net_poll)
 *   walk     the render callbacks' GX walk
 *   flush    gx_frame_end: the GL submission
 *   wait     the 60 Hz pacer's wait for requestAnimationFrame
 *   late     the part of `wait` after the frame was due: the browser handing
 *            the frame back late -- its main thread busy (GC, another tab),
 *            or Chrome holding requestAnimationFrame while the GPU catches
 *            up, which is how a GPU-bound page looks from in here
 *   shader   shader warm-up and deferred variant compiles
 *   swap     upscale + swap
 *   draws, uploads, io (ms), rewinds (rollback corrections) per frame
 *   base     draws on the base program: the eight-stage TEV shader driven by
 *            uniforms, standing in for a variant still compiling -- several
 *            times a variant's cost per pixel
 *   pend     variants still compiling (each one's draws are on `base`)
 *   efb      EFB copies (GXCopyTex) this frame
 *   gpu      the frame's GPU time (timer query), where the browser has one,
 *            from the frame's first draw to its last (not the pacer's wait);
 *            idle gaps between the browser's commands count too, so on a
 *            GPU that keeps up it reads about `walk`
 *   gpuoff   the part of gpu in off-screen passes (the fighters' shadows)
 *   gpucopy  the part of gpu in EFB copies and their clears
 *   drain    every 8th frame: how long after the frame's last draw the GPU
 *            finished it (a fence). Small: the GPU keeps up. Large: the GPU
 *            -- or the browser's GPU process, or its compositor on the same
 *            GPU -- is behind, and `late` will follow.
 * So a slow frame is this machine's CPU when game/walk/shader are big, the
 * other players when net is, and the GPU or the browser when late is;
 * gpu/drain/base say whether the GPU is the one.
 *
 * Reports from before 2026-10-23 (no `drain` column) have a `gpu` that ran
 * through the pacer's wait: it is the frame's wall time less the game's,
 * not GPU time, and reads 12-14 ms on any GPU.
 */
'use strict';
const fs = require('fs');
const path = require('path');

const args = process.argv.slice(2);
const opt = (name, dflt) => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : dflt; };
const target = args.find((a, i) => !a.startsWith('--') && !(i > 0 && args[i - 1].startsWith('--')));
if (!target) {
    console.error('usage: node tools/wasm/report.js <report.json | reports-dir> [--logs N|all] [--csv out.csv]');
    process.exit(2);
}

const load = (f) => JSON.parse(fs.readFileSync(f, 'utf8'));

/* The GPU's share, and the shader variants' (columns base/pend/gpu*,
 * src/port/render.c web_prof_frame), over the match's frames. */
function gpuSummary(prof) {
    const f = prof.fields || [];
    const st = prof.summary.stats;
    if (!f.includes('drain')) {
        if (f.includes('gpu')) {
            console.log('  (this report\'s gpu column predates 2026-10-23: it ran through the pacer\'s wait, ' +
                        'so it is wall time less game time, not GPU time)');
        }
        return;
    }
    const m = (c) => (st[c] ? st[c].mean : NaN);
    if (Number.isFinite(m('gpu')) && st.gpu.max > 0) {
        console.log(`  gpu: ${m('gpu')} ms a frame (p95 ${st.gpu.p95}): main scene ${(m('gpu') - m('gpuoff') - m('gpucopy')).toFixed(2)}, ` +
                    `shadows ${m('gpuoff')}, EFB copies ${m('gpucopy')}; drain ${m('drain')} ms (p95 ${st.drain.p95}) ` +
                    `against walk ${m('walk')} -- ` +
                    (m('drain') > 8 || m('gpu') > m('walk') + 4 ? 'the GPU side is behind' : 'the GPU keeps up'));
    } else {
        console.log('  gpu: no timer query in this browser');
    }
    if (!prof.data) return;
    const buf = Buffer.from(prof.data, 'base64');
    const fl = new Float32Array(buf.buffer, buf.byteOffset, buf.length / 4);
    const F = f.length, I = (c) => f.indexOf(c);
    let rows = 0, draws = 0, base = 0, withBase = 0, maxPend = 0, lastPend = -1, lastBase = -1, gBase = 0, nBase = 0, gNo = 0, nNo = 0;
    for (let i = 0; i + F <= fl.length; i += F) {
        const mf = fl[i + I('mframe')];
        if (!(mf > 0)) continue;
        rows++;
        const b = fl[i + I('base')], g = fl[i + I('gpu')];
        draws += fl[i + I('draws')];
        base += b;
        if (b > 0) { withBase++; lastBase = mf; }
        if (fl[i + I('pend')] > 0) lastPend = mf;
        maxPend = Math.max(maxPend, fl[i + I('pend')]);
        if (Number.isFinite(g)) { if (b > 20) { gBase += g; nBase++; } else if (b === 0) { gNo += g; nNo++; } }
    }
    if (!rows) return;
    console.log(`  shaders: ${(100 * base / (draws || 1)).toFixed(2)}% of the match's draws on the base program, ` +
                `in ${withBase} of ${rows} frames (last at match frame ${lastBase}); up to ${maxPend} variants compiling, ` +
                `the last at match frame ${lastPend}` +
                (nBase && nNo ? `; gpu ${(gBase / nBase).toFixed(2)} ms in frames with over 20 base draws, ${(gNo / nNo).toFixed(2)} with none` : ''));
}
const s = (sum) => (sum ? `${sum.fps} fps, ${sum.over20ms}/${sum.frames} frames >20ms, ${sum.over50ms} >50ms` : 'no profile');

if (fs.statSync(target).isDirectory()) {
    for (const f of fs.readdirSync(target).filter((x) => x.endsWith('.json')).sort()) {
        let r;
        try { r = load(path.join(target, f)); } catch (e) { console.log(`${f}  (unreadable: ${e.message})`); continue; }
        console.log(`${f}  ${r.crash ? 'CRASH ' : ''}${s(r.prof && r.prof.summary)}${r.note ? `  "${r.note}"` : ''}`);
    }
    process.exit(0);
}

const r = load(target);
const p = r.page || {};
console.log(`${r.name || '?'}: ${r.kind} report at ${r.time}${r.note ? `\n  note: "${r.note}"` : ''}`);
console.log(`  browser   ${p.ua}`);
console.log(`  machine   ${p.platform}, ${p.cores} cores, ${p.memoryGB || '?'} GB, GPU ${p.gpu || '(hidden)'}`);
console.log(`  display   screen ${p.screen && p.screen.join('x')} dpr ${p.dpr}, viewport ${p.viewport && p.viewport.join('x')}, ` +
            `canvas ${p.canvas && p.canvas.join('x')}${p.fullscreen ? ', fullscreen' : ''}` +
            (p.gfxLines != null ? `, graphics cap ${p.gfxLines} lines` : ''));
console.log(`  build     server's: ${p.build ? `${p.build.modified}, ${p.build.size} bytes` : '?'}; ` +
            `running: ${p.running ? `lobby ${p.running.lobby}, wasm ${p.running.wasm}` : 'unknown (a page from before version stamps)'}` +
            `${p.jspi ? '; JSPI available' : ''}`);
if (r.match) {
    console.log(`  match     slot ${r.match.slot} of ${(r.match.players || []).map((x) => x.name).join(', ')}; ` +
                `stage ${r.match.rules && r.match.rules.stage}, delay ${r.match.delay} (${r.match.why || ''})`);
}
if (r.net) console.log(`  network   ping ${r.net.ping == null ? '?' : r.net.ping + ' ms'} to the host`);
if (r.disc && r.disc.net) {
    const d = r.disc.net, pf = r.disc.prefetch, mb = (n) => (n / 2 ** 20).toFixed(1);
    console.log(`  disc      ${d.files} files fetched (${mb(d.raw)} MB, ${mb(d.wire)} MB on the wire), ` +
                `${mb(d.cached)} MB from the browser's cache, ${d.ranges} range reads` +
                (pf && pf.want ? `; prefetch ${mb(pf.have)} of ${mb(pf.want)} MB${pf.failed ? `, ${pf.failed} failed` : ''}` : ''));
}
if (r.hidden && r.hidden.count) console.log(`  hidden    tab hidden ${r.hidden.count}x for ${(r.hidden.ms / 1000).toFixed(1)} s (timers, not vsync, while hidden)`);
if (r.longTasks && r.longTasks.count) {
    console.log(`  longtasks ${r.longTasks.count} over 50 ms, ${(r.longTasks.ms / 1000).toFixed(1)} s total; worst ` +
                r.longTasks.worst.slice(0, 5).map(([t, d]) => `${d} ms @${(t / 1000).toFixed(1)}s`).join(', '));
}
if (r.crash) {
    console.log(`\nCRASH (${r.crash.source}) at match frame ${r.crash.matchFrame}: ${r.crash.message}`);
    if (r.crash.stack) console.log(r.crash.stack.split('\n').slice(0, 40).map((l) => '    ' + l).join('\n'));
    console.log('  (wasm offsets: a WASM_DEBUG=1 build names them with emsymbolizer -t file build/wasm/melee.wasm 0x<offset>)');
}
if (r.errors && r.errors.length > (r.crash ? 1 : 0)) {
    console.log(`\nerrors (${r.errors.length}):`);
    for (const e of r.errors.slice(-10)) console.log(`  [${e.source} @${e.matchFrame}] ${e.message}`);
}

const prof = r.prof;
if (prof && prof.summary) {
    const sum = prof.summary;
    console.log(`\nframes: ${s(sum)}, ${sum.over33ms} >33ms, over ${sum.seconds} s (${prof.matchRows} in the match)`);
    const cols = Object.keys(sum.stats);
    const w = 9;
    console.log('  ' + 'ms'.padEnd(8) + cols.map((c) => c.padStart(w)).join(''));
    for (const k of ['mean', 'p50', 'p95', 'p99', 'max']) {
        console.log('  ' + k.padEnd(8) + cols.map((c) => String(sum.stats[c][k]).padStart(w)).join(''));
    }
    if (sum.over20ms) {
        const a = sum.slowFramesAverage;
        console.log('  ' + 'slow avg'.padEnd(8) + cols.map((c) => String(c === 'wall' ? '' : a[c]).padStart(w)).join(''));
        const own = ['game', 'net', 'walk', 'flush', 'late', 'shader', 'swap'].map((c) => [c, a[c] || 0]).sort((x, y) => y[1] - x[1]);
        console.log(`  slow frames go mostly to: ${own.slice(0, 3).map(([c, v]) => `${c} ${v} ms`).join(', ')}`);
    }
    gpuSummary(prof);
    if (prof.worst && prof.worst.length) {
        console.log('\nworst frames:');
        const f = prof.fields;
        console.log('  ' + f.map((c) => c.padStart(8)).join(''));
        for (const row of prof.worst.slice(0, 15)) console.log('  ' + f.map((c) => String(row[c]).padStart(8)).join(''));
    }
    const csv = opt('--csv');
    if (csv && prof.data) {
        const buf = Buffer.from(prof.data, 'base64');
        const fl = new Float32Array(buf.buffer, buf.byteOffset, buf.length / 4);
        const F = prof.fields.length;
        const lines = [prof.fields.join(',')];
        for (let i = 0; i + F <= fl.length; i += F) lines.push(Array.from(fl.subarray(i, i + F), (v) => +v.toFixed(3)).join(','));
        fs.writeFileSync(csv, lines.join('\n') + '\n');
        console.log(`\n${lines.length - 1} rows -> ${csv}`);
    }
}

const gi = r.glInfo;
if (gi && !gi.error) {
    const a = gi.got || {};
    console.log(`\nWebGL: ${gi.renderer} (${gi.vendor}); GPU timer ${gi.timerQuery ? 'yes' : 'NO'}, ` +
                `antialias ${a.antialias}, depth ${a.depth}, stencil ${a.stencil}, alpha ${a.alpha}, ` +
                `preserveDrawingBuffer ${a.preserveDrawingBuffer}, desynchronized ${a.desynchronized}, ` +
                `power ${a.powerPreference}, max samples ${gi.maxSamples}`);
}
const gp = r.glProf;
if (gp && gp.top) {
    console.log(`\nWebGL calls over ${gp.frames} frames from match frame ${gp.fromMatchFrame} (${gp.why}): ` +
                `${gp.msPerFrame} ms/frame, ${gp.glMsPerFrame} ms of it inside ${gp.callsPerFrame} calls`);
    console.log('  ' + 'call'.padEnd(28) + 'calls/f'.padStart(9) + 'ms/f'.padStart(9) + 'worst'.padStart(9) + 'KB/f'.padStart(9));
    for (const c of gp.top.slice(0, 25)) {
        console.log('  ' + c.name.padEnd(28) + String(c.callsPerFrame).padStart(9) + String(c.msPerFrame).padStart(9) +
                    String(c.worstMs).padStart(9) + String(c.kbPerFrame).padStart(9));
    }
} else if (gp && gp.running != null) console.log(`\nWebGL call profile still running (${gp.running} frames in)`);

const nlog = opt('--logs', '40');
const logs = r.logs || [];
const tail = nlog === 'all' ? logs : parseInt(nlog, 10) > 0 ? logs.slice(-parseInt(nlog, 10)) : [];
if (tail.length) {
    console.log(`\nlog (${tail.length} of ${logs.length} lines):`);
    for (const [t, lvl, text] of tail) console.log(`  ${(t / 1000).toFixed(2).padStart(8)} ${lvl} ${text}`);
}
