/* Headless node harness for the wasm build (WASM_NODE=1).
 *
 * Purpose: run the port far enough to exercise boot, archive loading and the
 * frame loop without a browser, so the file set and the early crashes can be
 * found from the terminal. Rendering is not the point here -- WebGL does not
 * exist under node -- the point is everything up to it.
 *
 * SDL's emscripten backend calls emscripten_get_screen_size(), which reads
 * the DOM `screen` object. Under node there isn't one, and the port dies in
 * window_init() before it has opened a single file. A fixed size is exactly
 * as truthful here as a real display would be. */
if (typeof globalThis.screen === 'undefined') {
    globalThis.screen = { width: 1280, height: 720 };
}
