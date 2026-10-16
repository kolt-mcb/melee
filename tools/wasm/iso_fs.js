/* The player's disc image, as src/port/pc_web_iso.c reads it.
 *
 * Module.meleeIsoOpen(file) validates a File as Melee NTSC-U 1.02 and parses
 * its FST into Module.meleeIso: { file, entries: [{path, offset, size}],
 * lookup(path) }. Paths are the extraction's (tools/pc/extract_game.py):
 * the FST's own, plus boot.dol for the executable. Lookup ignores case -- the
 * disc does, and the game's callers are not consistent about it.
 *
 * If the page sets Module.meleeIsoReady (a Promise resolving once
 * Module.meleeIso is set), main() waits for it. A page packaged with
 * WASM_ASSETS sets nothing and reads its bundle instead.
 */
if (typeof Module === 'undefined') { var Module = {}; }

Module.meleeIsoOpen = async function (file) {
    const GAME_ID = 'GALE01', REVISION = 2;
    if (file.size < 0x440) throw new Error('This file is too small to be a GameCube disc image.');
    const hdr = new DataView(await file.slice(0, 0x440).arrayBuffer());
    let id = '';
    for (let i = 0; i < 6; i++) id += String.fromCharCode(hdr.getUint8(i));
    if (hdr.getUint32(0x1C) !== 0xC2339F3D) {
        throw new Error('This is not a GameCube disc image. Compressed images ' +
                        '(.rvz, .ciso, .gcz) need converting to .iso first -- ' +
                        "Dolphin's Convert File can do it.");
    }
    if (id !== GAME_ID) {
        throw new Error(`This disc is ${id}, not ${GAME_ID} (Super Smash Bros. Melee, NTSC-U).`);
    }
    if (hdr.getUint8(7) !== REVISION) {
        throw new Error(`This is revision 1.0${hdr.getUint8(7)}; the port needs 1.02.`);
    }

    const dolOff = hdr.getUint32(0x420);
    const fstOff = hdr.getUint32(0x424);
    const fstSize = hdr.getUint32(0x428);

    /* DOL: 18 section file offsets at 0x00 and sizes at 0x90. */
    const dh = new DataView(await file.slice(dolOff, dolOff + 0x100).arrayBuffer());
    let dolEnd = 0x100;
    for (let i = 0; i < 18; i++) {
        const o = dh.getUint32(4 * i), n = dh.getUint32(0x90 + 4 * i);
        if (n && o + n > dolEnd) dolEnd = o + n;
    }

    const fst = new DataView(await file.slice(fstOff, fstOff + fstSize).arrayBuffer());
    const count = fst.getUint32(8);
    const strings = count * 12;
    const name = (i) => {
        let p = strings + (fst.getUint32(i * 12) & 0xFFFFFF), s = '';
        for (let c; (c = fst.getUint8(p)) !== 0; p++) s += String.fromCharCode(c);
        return s;
    };

    const entries = [{ path: 'boot.dol', offset: dolOff, size: dolEnd }];
    /* A directory records the index one past its last child. */
    const stack = [[count, '']];
    for (let i = 1; i < count; i++) {
        while (stack.length && i >= stack[stack.length - 1][0]) stack.pop();
        const prefix = stack.length ? stack[stack.length - 1][1] : '';
        if (fst.getUint8(i * 12) === 1) {
            stack.push([fst.getUint32(i * 12 + 8), prefix + name(i) + '/']);
        } else {
            entries.push({ path: prefix + name(i),
                           offset: fst.getUint32(i * 12 + 4),
                           size: fst.getUint32(i * 12 + 8) });
        }
    }

    const byPath = new Map();
    entries.forEach((e, i) => { e.index = i; byPath.set(e.path.toLowerCase(), e); });

    /* Whole files, most recently used last. The game reopens files often --
     * every load opens an archive three times, and a music stream is reopened
     * as it plays -- and a hit here is a synchronous copy instead of a disc
     * read and an ASYNCIFY suspension. */
    const CACHE_BYTES = 128 * 1024 * 1024;
    const cache = new Map();
    let cached = 0;
    Module.meleeIso = {
        file, entries, files: entries.length,
        lookup: (p) => byPath.get(p.replace(/^\/+/, '').toLowerCase()) || null,
        cacheGet(idx) {
            const b = cache.get(idx);
            if (b) { cache.delete(idx); cache.set(idx, b); }
            return b || null;
        },
        cachePut(idx, bytes) {
            if (bytes.byteLength > CACHE_BYTES / 4 || cache.has(idx)) return;
            cache.set(idx, bytes);
            cached += bytes.byteLength;
            for (const [k, v] of cache) {
                if (cached <= CACHE_BYTES) break;
                cache.delete(k);
                cached -= v.byteLength;
            }
        },
    };
    console.log(`[ISO] ${file.name}: ${entries.length} files`);
    return Module.meleeIso;
};

/* The host's disc image over HTTP (tools/wasm/serve.js serves /disc.iso by
 * byte range), presented as the File the reader above already speaks:
 * size, name and slice(a, b).arrayBuffer(). */
Module.meleeIsoOpenUrl = async function (url) {
    const head = await fetch(url, { method: 'HEAD' });
    const size = parseInt(head.headers.get('Content-Length') || '0', 10);
    if (!head.ok || !size) throw new Error(`The server has no disc image at ${url}.`);
    const remote = {
        name: url, size,
        slice(a, b) {
            return {
                async arrayBuffer() {
                    if (b <= a) return new ArrayBuffer(0);
                    const r = await fetch(url, { headers: { Range: `bytes=${a}-${b - 1}` } });
                    if (r.status !== 206) throw new Error(`disc read ${a}-${b}: HTTP ${r.status}`);
                    const buf = await r.arrayBuffer();
                    // Bytes fetched so far, for the page's loading screen.
                    Module.meleeIsoBytes = (Module.meleeIsoBytes || 0) + buf.byteLength;
                    return buf;
                },
            };
        },
    };
    return Module.meleeIsoOpen(remote);
};

Module.preRun = Module.preRun || [];
Module.preRun.push(function () {
    if (!Module.meleeIsoReady) return;
    addRunDependency('melee-iso');
    Module.meleeIsoReady.then(() => removeRunDependency('melee-iso'));
});
