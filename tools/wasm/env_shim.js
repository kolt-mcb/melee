/* Bridge the port's MELEE_* environment knobs to the page's query string.
 *
 * Every diagnostic and boot option in this port is a getenv() -- MELEE_FPS,
 * MELEE_BOOT_MODE, MELEE_UNCAP, and a hundred others. A browser has no
 * environment to set them in, which would leave the wasm build the one
 * target that cannot be driven or instrumented without a rebuild.
 *
 * So: melee.html?MELEE_BOOT_MODE=14&MELEE_UNCAP=1 sets them. Emscripten's
 * ENV object is module-internal and read lazily on the first getenv(), so
 * this has to run in preRun (before main) and needs ENV in
 * EXPORTED_RUNTIME_METHODS -- see configure_pc.py.
 *
 * Only MELEE_-prefixed keys are copied: the query string is attacker-supplied
 * in any hosted deployment, and PATH or LD_PRELOAD are not things a URL
 * should be able to set.
 */
if (typeof Module === 'undefined') { var Module = {}; }
Module.preRun = Module.preRun || [];
Module.preRun.push(function () {
    try {
        if (typeof location === 'undefined' || !location.search) return;
        if (!Module.ENV) {
            console.warn('[env_shim] Module.ENV missing; MELEE_* ignored');
            return;
        }
        var applied = [];
        new URLSearchParams(location.search).forEach(function (value, key) {
            if (/^MELEE_[A-Z0-9_]*$/.test(key)) {
                Module.ENV[key] = value;
                applied.push(key + '=' + value);
            }
        });
        if (applied.length) {
            console.log('[env_shim] ' + applied.join(' '));
        }
    } catch (e) {
        console.warn('[env_shim] ' + e);
    }
});
