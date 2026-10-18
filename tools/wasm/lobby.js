/* The melee web lobby: picks, the /net relay, and starting the engine.
 *
 * This is tools/wasm/shell.html's inline script, moved out verbatim. It is a
 * plain classic script (no async, no defer) and it must stay that way and stay
 * BEFORE emscripten's own tag: it defines `var Module`, and emscripten reads
 * Module.canvas while booting. Load it after melee.js and SDL cannot create a
 * GL context. It is a file rather than inline because the gateway's CSP is
 * `script-src 'self' 'wasm-unsafe-eval'` and inline script is not allowed.
 *
 * Served from the same directory as melee.html (deploy.sh copies it there).
 */
  'use strict';
  /* ---- the page's own log, for reports (see "reports" below) ----
   * First, so it holds everything: the engine's output reaches the console
   * through Module.print/printErr, and the lobby's own lines go there too. */
  const LOG_KEEP = 4000;
  const pageLog = [];
  const pageErrors = [];
  let gpuRenderer = null;        // checkGpu's probe, for reports
  (function captureConsole() {
    const fmt = (a) => {
      if (typeof a === 'string') return a;
      if (a instanceof Error) return a.stack || String(a);
      try { return JSON.stringify(a); } catch (e) { return String(a); }
    };
    for (const level of ['log', 'info', 'warn', 'error', 'debug']) {
      const orig = console[level].bind(console);
      console[level] = (...args) => {
        pageLog.push([Math.round(performance.now()), level[0], args.map(fmt).join(' ').slice(0, 2000)]);
        if (pageLog.length > LOG_KEEP) pageLog.splice(0, pageLog.length - LOG_KEEP);
        orig(...args);
      };
    }
  })();
  const FIGHTERS = ['Captain Falcon', 'Donkey Kong', 'Fox', 'Mr. Game & Watch', 'Kirby', 'Bowser',
    'Link', 'Luigi', 'Mario', 'Marth', 'Mewtwo', 'Ness', 'Peach', 'Pikachu', 'Ice Climbers',
    'Jigglypuff', 'Samus', 'Yoshi', 'Zelda', 'Sheik', 'Falco', 'Young Link', 'Dr. Mario', 'Roy',
    'Pichu', 'Ganondorf'];                                   // CharacterKind order
  const STAGES = [[31, 'Battlefield'], [32, 'Final Destination'], [3, 'Pokémon Stadium'],
    [16, "Yoshi's Island"], [8, "Yoshi's Story"], [2, 'Fountain of Dreams'], [28, 'Dream Land'],
    [4, "Peach's Castle"], [5, 'Kongo Jungle'], [12, 'Jungle Japes'], [13, 'Great Bay'],
    [14, 'Hyrule Temple'], [6, 'Brinstar'], [15, 'Brinstar Depths'], [17, 'Green Greens'],
    [7, 'Corneria'], [22, 'Venom'], [9, 'Onett'], [18, 'Fourside'], [10, 'Mute City'],
    [24, 'Big Blue'], [11, 'Rainbow Cruise'], [19, 'Mushroom Kingdom'], [20, 'Mushroom Kingdom II'],
    [25, 'Icicle Mountain'], [23, 'Poké Floats'], [27, 'Flat Zone'], [29, "Yoshi's Island (N64)"],
    [30, 'Kongo Jungle (N64)']];                            // StKind values
  const COSTUMES = 4;            // every fighter has at least four
  const SLOTS = 8;               // fighters in a match (the PC build's GM_MAX_PLAYERS)
  /* ---- keyboard: the game's actions, their keys, and two presets ----
   * Keys are KeyboardEvent.code values (a physical position, whatever the
   * layout prints on it), passed to the game as MELEE_KEYS with SDL
   * scancodes -- USB HID usages, which is what SDL's web backend turns
   * .code into. The 'box' preset is the game's own default
   * (poll_keyboard_to_pad in src/pc_stub/undef_stubs.c); keep them in step. */
  const KEY_ACTIONS = [
    ['up', 'Up'], ['left', 'Left'], ['down', 'Down'], ['right', 'Right'],
    ['modx', 'Mod X — walk, tilt'], ['mody', 'Mod Y — slow drift'],
    ['a', 'A'], ['b', 'B'], ['jump', 'Jump (X)'], ['jump2', 'Jump (Y)'],
    ['shield', 'Shield (R)'], ['shield2', 'Shield (L)'], ['lightshield', 'Light shield'], ['grab', 'Grab (Z)'],
    ['cup', 'C-stick up'], ['cleft', 'C-stick left'], ['cdown', 'C-stick down'], ['cright', 'C-stick right'],
    ['start', 'Start (pause)'],
    ['dup', 'D-pad up (taunt)'], ['dleft', 'D-pad left'], ['ddown', 'D-pad down'], ['dright', 'D-pad right'],
  ];
  const KEY_PRESETS = {
    box: {
      up: ['KeyW'], left: ['KeyA'], down: ['KeyS'], right: ['KeyD'], modx: ['ShiftLeft'], mody: ['Tab'],
      a: ['KeyJ'], b: ['KeyK'], jump: ['Space'], jump2: ['KeyH'], shield: ['KeyL'], shield2: [],
      lightshield: ['Quote'], grab: ['Semicolon'],
      cleft: ['KeyU'], cdown: ['KeyI'], cup: ['KeyO'], cright: ['KeyP'], start: ['Enter'],
      dup: ['Digit1'], ddown: ['Digit2'], dleft: ['Digit3'], dright: ['Digit4'],
    },
    // The port's first layout, for anyone already used to it (minus Alt,
    // which opens Firefox's menu bar).
    classic: {
      up: ['KeyW', 'ArrowUp'], left: ['KeyA', 'ArrowLeft'], down: ['KeyS', 'ArrowDown'], right: ['KeyD', 'ArrowRight'],
      modx: [], mody: [], a: ['KeyZ'], b: ['KeyX'], jump: ['Space'], jump2: ['KeyV'],
      shield: ['ShiftLeft', 'ShiftRight'], shield2: [], lightshield: [], grab: ['KeyC'],
      cup: ['KeyI'], cleft: ['KeyJ'], cdown: ['KeyK'], cright: ['KeyL'], start: ['Enter'],
      dup: [], ddown: [], dleft: [], dright: [],
    },
  };
  const SCANCODE = (() => {
    const t = { Enter: 40, Escape: 41, Backspace: 42, Tab: 43, Space: 44, Minus: 45, Equal: 46,
      BracketLeft: 47, BracketRight: 48, Backslash: 49, Semicolon: 51, Quote: 52, Backquote: 53,
      Comma: 54, Period: 55, Slash: 56, CapsLock: 57, Insert: 73, Home: 74, PageUp: 75, Delete: 76,
      End: 77, PageDown: 78, ArrowRight: 79, ArrowLeft: 80, ArrowDown: 81, ArrowUp: 82,
      NumpadDivide: 84, NumpadMultiply: 85, NumpadSubtract: 86, NumpadAdd: 87, NumpadEnter: 88,
      Numpad0: 98, NumpadDecimal: 99, IntlBackslash: 100,
      ShiftLeft: 225, ShiftRight: 229 };
    for (let i = 0; i < 26; i++) t['Key' + String.fromCharCode(65 + i)] = 4 + i;
    for (let i = 1; i <= 9; i++) { t['Digit' + i] = 29 + i; t['Numpad' + i] = 88 + i; }
    t.Digit0 = 39;
    for (let i = 1; i <= 12; i++) t['F' + i] = 57 + i;
    return t;
  })();
  const KEY_NAMES = { Space: 'Space', Tab: 'Tab', Enter: 'Enter', ShiftLeft: 'L Shift', ShiftRight: 'R Shift',
    ArrowUp: '↑', ArrowDown: '↓', ArrowLeft: '←', ArrowRight: '→', Semicolon: ';', Quote: "'",
    BracketLeft: '[', BracketRight: ']', Backslash: '\\', Comma: ',', Period: '.', Slash: '/',
    Minus: '-', Equal: '=', Backquote: '`', CapsLock: 'Caps', Backspace: 'Bksp' };
  let layoutMap = null;   // what this keyboard prints on each key, where the browser says (Chrome)
  const keyName = (code) => (layoutMap && layoutMap.get(code) && layoutMap.get(code) !== ' '
    ? layoutMap.get(code).toUpperCase() : null)
    || KEY_NAMES[code] || code.replace(/^Key|^Digit/, '').replace(/^Numpad/, 'Num ');

  const $ = (id) => document.getElementById(id);
  const escapeHtml = (s) => s.replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  const store = {
    get(k, d) { try { const v = localStorage.getItem('melee.' + k); return v === null ? d : JSON.parse(v); } catch (e) { return d; } },
    set(k, v) { try { localStorage.setItem('melee.' + k, JSON.stringify(v)); } catch (e) { /* private mode */ } },
  };
  const setStatus = (t, bad) => { $('status').textContent = t; $('status').classList.toggle('bad', !!bad); };

  const wasmVersion = document.currentScript && document.currentScript.dataset.wasmV;
  const lobbyVersion = document.currentScript ? (/[?&]v=([^&]+)/.exec(document.currentScript.src) || [])[1] || 'unversioned' : null;
  /* ---- the engine: loads now, runs main() only when a match starts ---- */
  let resolveStart;
  const startReady = new Promise((r) => { resolveStart = r; });
  const inbox = [];
  let ws = null;
  var Module = {
    canvas: $('canvas'),
    meleeIsoReady: startReady,
    meleeNet: {
      inbox,
      send(bytes) { if (ws && ws.readyState === 1) ws.send(bytes); },
      onMatchEnd(desynced) {
        sendReportOnce(desynced ? 'desync' : 'match');
        $('banner-text').textContent = desynced
          ? 'Match over -- but the players fell out of step (desync).' : 'Match over.';
        $('banner').classList.add('show');
      },
    },
    print: (t) => console.log(t),
    printErr: (t) => {
      console.error(t);
      // The engine's own fatal error, on the loading screen that would
      // otherwise spin forever.
      if (/No usable GL context/.test(t)) {
        $('loading-text').textContent = 'This browser could not start WebGL 2, so the game cannot draw.';
        $('loading-detail').textContent = 'Turn on graphics acceleration (Chrome: Settings → System), relaunch, ' +
          'and check that chrome://gpu lists WebGL2 as available.';
      }
    },
    setStatus: () => {},
    // The engine by version (serve.js stamps it on this script's tag), so a
    // cached old melee.wasm can never be paired with a new page.
    locateFile: (path, prefix) => prefix + path + (path.endsWith('.wasm') && wasmVersion ? `?v=${wasmVersion}` : ''),
    // emscripten's abort(): an assertion, an out-of-memory, a missing import.
    onAbort: (what) => crashed('abort', String(what), new Error(String(what)).stack),
  };
  let discReady = null;
  const whenEngine = () => new Promise((res) => {
    const t = () => (Module.meleeIsoOpenUrl ? res() : setTimeout(t, 50)); t();
  });
  whenEngine().then(() => {
    discReady = Module.meleeIsoOpenUrl('disc.iso').catch((e) => { setStatus(e.message, true); throw e; });
    discReady.then(prefetchMatch, () => {});
  });

  /* ---- lobby state ---- */
  const me = { name: store.get('name', ''), ckind: store.get('ckind', 8), color: store.get('color', 0) };
  /* The other players at this computer, one per extra gamepad: local pad k
   * plays the k-th of this page's players (pad 0, and the keyboard, are
   * `me`). The relay gives each a slot of its own. */
  const MAX_LOCAL = 4;
  const guests = (store.get('guests', []) || []).slice(0, MAX_LOCAL - 1)
    .map((g) => ({ ckind: g && g.ckind != null ? g.ckind : 8, color: g && g.color != null ? g.color : 0 }));
  let editing = 0;   // whose fighter the picker shows: 0 is `me`, k is guests[k - 1]
  const seat = (k) => (k === 0 ? me : guests[k - 1]);
  let lobby = null;
  let rules = { cpus: store.get('cpus', 0), cpulevel: store.get('cpulevel', 9) };

  /* ---- the keyboard editor ---- */
  const presetOf = (name) => JSON.parse(JSON.stringify(KEY_PRESETS[name]));
  let keyPreset = store.get('keyPreset', 'box');
  let keys = Object.assign(presetOf('box'), store.get('keys', null) || presetOf(KEY_PRESETS[keyPreset] ? keyPreset : 'box'));
  let rebinding = null;   // the action waiting for a key
  const keysEnv = () => KEY_ACTIONS.map(([a]) =>
    `${a}=${(keys[a] || []).map((c) => SCANCODE[c]).filter(Boolean).join('+')}`).join(',');
  function paintKeys() {
    $('key-preset').value = KEY_PRESETS[keyPreset] && JSON.stringify(keys) === JSON.stringify(presetOf(keyPreset))
      ? keyPreset : 'custom';
    $('keys').innerHTML = KEY_ACTIONS.map(([a, label]) => {
      const now = rebinding === a ? '<i>press a key…</i>'
        : (keys[a] || []).map((c) => `<kbd>${escapeHtml(keyName(c))}</kbd>`).join(' ') || '<span class="pad no">—</span>';
      return `<tr><td>${escapeHtml(label)}</td><td><button class="key${rebinding === a ? ' on' : ''}" data-act="${a}">${now}</button></td></tr>`;
    }).join('');
  }
  function saveKeys() { store.set('keys', keys); store.set('keyPreset', keyPreset); paintKeys(); }
  $('keys').addEventListener('click', (e) => {
    const b = e.target.closest('[data-act]');
    if (!b) return;
    rebinding = rebinding === b.dataset.act ? null : b.dataset.act;
    $('key-note').textContent = rebinding ? 'Press the new key. Esc cancels; Backspace clears it.' : '';
    paintKeys();
  });
  window.addEventListener('keydown', (e) => {
    if (!rebinding || started) return;
    e.preventDefault();
    e.stopPropagation();
    const act = rebinding;
    let note = '';
    if (e.code === 'Escape') {
      rebinding = null;
    } else if (e.code === 'Backspace' || e.code === 'Delete') {
      keys[act] = [];
      rebinding = null;
    } else if (/^(Alt|Control|Meta|OS)/.test(e.code)) {
      note = 'Alt, Ctrl and the system key belong to the browser (Ctrl+W closes the tab). Pick another.';
    } else if (!SCANCODE[e.code]) {
      note = `The game cannot read ${e.code || 'that key'}. Pick another.`;
    } else {
      // A key does one thing: take it off whatever had it.
      for (const [a] of KEY_ACTIONS) {
        if (a !== act && (keys[a] || []).includes(e.code)) {
          keys[a] = keys[a].filter((c) => c !== e.code);
          note = `${keyName(e.code)} was ${KEY_ACTIONS.find(([x]) => x === a)[1]}; that is now unbound.`;
        }
      }
      keys[act] = [e.code];
      rebinding = null;
    }
    keyPreset = 'custom';
    $('key-note').textContent = note;
    saveKeys();
  }, true);
  $('key-preset').addEventListener('change', () => {
    const v = $('key-preset').value;
    if (!KEY_PRESETS[v]) return;
    keyPreset = v;
    keys = presetOf(v);
    rebinding = null;
    $('key-note').textContent = '';
    saveKeys();
  });
  if (navigator.keyboard && navigator.keyboard.getLayoutMap) {
    navigator.keyboard.getLayoutMap().then((m) => { layoutMap = m; paintKeys(); }).catch(() => {});
  }
  paintKeys();
  $('fighters').innerHTML = FIGHTERS.map((f, i) => `<button data-ck="${i}">${f}</button>`).join('');
  $('colors').innerHTML = Array.from({ length: COSTUMES }, (_, i) => `<button data-col="${i}">${i + 1}</button>`).join(' ');
  $('stage').innerHTML = STAGES.map(([v, n]) => `<option value="${v}">${n}</option>`).join('');
  $('cpulevel').innerHTML = Array.from({ length: 9 }, (_, i) => `<option>${i + 1}</option>`).join('');
  $('name').value = me.name;

  function paintPicks() {
    if (editing > guests.length) editing = 0;
    const p = seat(editing);
    document.querySelectorAll('[data-ck]').forEach((b) => b.classList.toggle('on', +b.dataset.ck === p.ckind));
    document.querySelectorAll('[data-col]').forEach((b) => b.classList.toggle('on', +b.dataset.col === p.color));
    $('locals').value = String(1 + guests.length);
    $('seat-tabs').hidden = guests.length === 0;
    $('seat-tabs').innerHTML = Array.from({ length: 1 + guests.length }, (_, k) =>
      `<button data-seat="${k}" class="${k === editing ? 'on' : ''}">${k === 0 ? 'You (pad 1 / keyboard)' : `Player ${k + 1} (pad ${k + 1})`}</button>`).join(' ');
    $('fighter-title').textContent = guests.length ? 'Fighters on this computer' : 'Your fighter';
  }
  const pickMsg = (t) => ({ t, name: me.name, ckind: me.ckind, color: me.color, guests });
  function sendPick() {
    store.set('name', me.name); store.set('ckind', me.ckind); store.set('color', me.color);
    store.set('guests', guests);
    send(pickMsg('pick'));
    paintPicks();
  }
  $('fighters').onclick = (e) => { const b = e.target.closest('[data-ck]'); if (b) { seat(editing).ckind = +b.dataset.ck; sendPick(); } };
  $('colors').onclick = (e) => { const b = e.target.closest('[data-col]'); if (b) { seat(editing).color = +b.dataset.col; sendPick(); } };
  $('seat-tabs').onclick = (e) => { const b = e.target.closest('[data-seat]'); if (b) { editing = +b.dataset.seat; paintPicks(); } };
  $('locals').innerHTML = Array.from({ length: MAX_LOCAL }, (_, i) => `<option>${i + 1}</option>`).join('');
  $('locals').onchange = () => {
    const n = +$('locals').value - 1;
    // A new player starts on a different costume from everyone already here.
    while (guests.length < n) guests.push({ ckind: me.ckind, color: (me.color + guests.length + 1) % COSTUMES });
    guests.length = n;
    sendPick();
  };
  $('name').oninput = () => { me.name = $('name').value; sendPick(); };

  const isHost = () => lobby && lobby.host === lobby.you;
  function sendRules() {
    send({ t: 'rules', stage: +$('stage').value, kind: $('kind').value, stocks: +$('stocks').value || 4,
           minutes: Math.max(0, +$('minutes').value || 0), items: +$('items').value,
           cpus: rules.cpus, cpulevel: rules.cpulevel });
  }
  ['stage', 'kind', 'stocks', 'minutes', 'items'].forEach((id) => { $(id).onchange = sendRules; });
  $('cpus').onchange = () => { rules.cpus = +$('cpus').value; store.set('cpus', rules.cpus); sendRules(); };
  $('cpulevel').onchange = () => { rules.cpulevel = +$('cpulevel').value; store.set('cpulevel', rules.cpulevel); sendRules(); };
  $('btn-start').onclick = () => send({ t: 'start' });

  function paintLobby() {
    if (!lobby) return;
    const ps = lobby.players;
    $('players').innerHTML = Array.from({ length: SLOTS }, (_, i) => i).map((i) => {
      const p = ps[i];
      if (!p) return `<div class="slot s${i} empty"><div class="who">P${i + 1}</div><div class="what">open</div></div>`;
      return `<div class="slot s${i}${p.id === lobby.you ? ' me' : ''}">
        <div class="who">P${i + 1} · ${escapeHtml(p.name)}${p.id === lobby.host && !p.seat ? '<span class="tag">host</span>' : ''}</div>
        <div class="what">${FIGHTERS[p.ckind]} · costume ${p.color + 1}${p.ping != null ? ` · ${p.ping} ms` : ''}${p.inMatch ? ' · <b>in a match</b>' : ''}${p.pre != null && p.pre < 100 && !p.inMatch ? ` · <b>downloading ${p.pre}%</b>` : ''}</div></div>`;
    }).join('');
    const host = isHost();
    $('host-note').textContent = host ? 'you are the host' : 'the host chooses';
    ['stage', 'kind', 'stocks', 'minutes', 'items', 'cpus', 'cpulevel'].forEach((id) => { $(id).disabled = !host; });
    const kind = lobby.rules.kind || 'time';   // a relay from before the choice ran time matches
    $('stage').value = lobby.rules.stage;
    $('kind').value = kind;
    $('stocks').value = lobby.rules.stocks;
    $('minutes').value = lobby.rules.minutes;
    $('items').value = lobby.rules.items != null ? lobby.rules.items : -1;
    // A time match counts KOs; stocks do not end it.
    $('stocks').disabled = !host || kind !== 'stock';
    $('minutes-label').firstChild.textContent = kind === 'stock' ? 'Time limit ' : 'Minutes ';
    $('minutes').title = kind === 'stock' ? '0: no time limit' : '0: no time limit (the match never ends)';
    const free = Math.max(0, SLOTS - Math.min(SLOTS, ps.length));
    $('cpus').innerHTML = Array.from({ length: free + 1 }, (_, i) => `<option>${i}</option>`).join('');
    if (!host) { rules.cpus = lobby.rules.cpus | 0; rules.cpulevel = lobby.rules.cpulevel || 9; }
    $('cpus').value = Math.min(rules.cpus, free);
    $('cpulevel').value = rules.cpulevel;
    $('btn-start').disabled = !host || lobby.inMatch;
    $('btn-start').textContent = lobby.inMatch ? 'A match is running…' : host ? 'Start match' : 'Waiting for the host';
    const n = Math.min(SLOTS, ps.length);
    const out = !ps.some((p) => p.id === lobby.you);
    setStatus(`${n} player${n === 1 ? '' : 's'} here${lobby.waiting ? `, ${lobby.waiting} waiting for a free slot` : ''}` +
      (out ? ' -- not enough room for everyone on this computer: fewer players here, or wait for a slot' : ''), out);
  }

  /* ---- a worse network, on purpose: ?netlag=MS&netjitter=MS&netloss=PCT ----
   * Delays everything this page receives -- game packets, and the relay's
   * pings too, so the delay it picks sees the same network -- to see what a
   * match feels like over the internet before playing one there. In order, as the
   * WebSocket's TCP delivers them: a "lost" packet is TCP's retransmit, held
   * ~200 ms (its minimum timeout) plus the lag, and everything behind it
   * waits too. Applied on both pages, the round trip is twice the lag. */
  const impair = (() => {
    const q = new URLSearchParams(location.search);
    const n = (k) => Math.max(0, +q.get(k) || 0);
    return { lag: n('netlag'), jitter: n('netjitter'), loss: n('netloss') };
  })();
  let impairDue = 0;
  function deliver(bytes, handle) {
    if (!impair.lag && !impair.jitter && !impair.loss) { handle(bytes); return; }
    let due = performance.now() + impair.lag + Math.random() * impair.jitter;
    if (Math.random() * 100 < impair.loss) due += 200 + impair.lag;
    impairDue = Math.max(impairDue, due);
    setTimeout(() => handle(bytes), impairDue - performance.now());
  }
  if (impair.lag || impair.jitter || impair.loss) console.log('[lobby] network impairment', JSON.stringify(impair));

  /* ---- the relay ---- */
  function received(bytes) {
    if (bytes[0] !== 0x7B) { inbox.push(bytes); return; }      // a game packet
    const msg = JSON.parse(new TextDecoder().decode(bytes));
    if (msg.t === 'ping') send({ t: 'pong', s: msg.s });        // the relay measuring the round trip
    else if (msg.t === 'lobby') { lobby = msg; if (!started) { paintLobby(); prefetchMatch(); } }
    else if (msg.t === 'start') { lastStart = msg; startMatch(msg); }
  }
  function send(obj) { if (ws && ws.readyState === 1) ws.send(new TextEncoder().encode(JSON.stringify(obj))); }
  let started = false;
  function connect() {
    ws = new WebSocket(`${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/net`);
    ws.binaryType = 'arraybuffer';
    ws.onopen = () => send(pickMsg('hello'));
    ws.onmessage = (ev) => deliver(new Uint8Array(ev.data), received);
    ws.onclose = () => {
      if (started) { Module.meleeNet.onMatchEnd(false); $('banner-text').textContent = 'Lost the connection to the host.'; }
      else { setStatus('Lost the connection to the host; retrying…', true); setTimeout(connect, 1500); }
    };
  }

  /* ---- a match ---- */
  async function startMatch(msg) {
    if (started) return;
    started = true;
    // The game reads what it still lacks itself; queued downloads would
    // only compete with the match for the host's upload.
    if (Module.meleeIsoPrefetchPause) Module.meleeIsoPrefetchPause(true);
    document.body.classList.add('playing');
    setStatus('Starting…');
    try { await discReady; } catch (e) { return; }
    const humans = msg.players.length;
    // Every human at one page is a local match: pad k drives port k, with no
    // netplay. A relay from before `pages` seated one player per page.
    const pages = msg.pages != null ? msg.pages : humans;
    const cpuCount = isHostCpuCount(msg);
    const lineup = msg.players.map((p) => `${p.ckind}:${p.color}`);
    const levels = msg.players.map(() => 0);
    for (let i = 0; i < cpuCount; i++) {
      // CPU fighters come from the seed, so every page picks the same ones.
      lineup.push(`${(msg.seed >>> (i * 5)) % 26}:${i % COSTUMES}`);
      levels.push(msg.rules.cpulevel || 9);
    }
    const env = {
      MELEE_BOOT_MODE: '14',
      MELEE_BOOT_MATCH: `${msg.players[0].ckind},${(msg.players[1] || msg.players[0]).ckind},${msg.rules.stage}`,
      MELEE_BOOT_PLAYERS: lineup.join(','),
      MELEE_BOOT_KIND: msg.rules.kind || 'time',
      MELEE_BOOT_ITEMS: String(msg.rules.items != null ? msg.rules.items : -1),
      MELEE_BOOT_STOCKS: String(msg.rules.stocks),
      MELEE_BOOT_TIME: String(msg.rules.minutes * 60),
      MELEE_SEED: msg.seed.toString(16),
    };
    if (cpuCount) env.MELEE_BOOT_CPU = levels.join(',');
    env.MELEE_KEYS = keysEnv();
    env.MELEE_IRES_MAX_LINES = String(gfxLines());
    if (pages > 1) {
      Object.assign(env, {
        MELEE_NET: 'web', MELEE_NET_SLOT: String(msg.slot), MELEE_NET_PLAYERS: String(humans),
        MELEE_NET_SLOTS: (msg.slots || [msg.slot]).join(','),
        MELEE_NET_SEED: msg.seed.toString(16), MELEE_NET_DELAY: String(msg.delay),
        // The slowest page's load is the first wait; a player who leaves is
        // announced by the relay, so a long timeout costs nothing -- and a
        // short one is worse than nothing: at 120 s the page that loaded first
        // gave up and played alone, and every slower page then waited on it
        // for ever. Fifteen minutes covers a first download on a slow upload.
        MELEE_NET_TIMEOUT: '900000',
      });
    }
    // Before main(): emscripten reads ENV on the first getenv, and the
    // constructors that call it run only once the start dependency clears.
    Object.assign(Module.ENV, env);
    console.log('[lobby] start', JSON.stringify(env), `-- delay ${msg.delay}: ${msg.why || ''}`);
    $('canvas').focus();
    resolveStart();
    showLoading(pages > 1, lineup.map((p) => +p.split(':')[0]), msg.rules.stage);
  }

  /* Which disc files a match load reads (tools/wasm/gen_load_manifest.py):
   * the ones every load reads, a stage's, and a fighter's -- costumes apart,
   * one per player. Summed against the disc's own sizes it is the total the
   * progress bar fills toward. */
  const loadManifest = fetch('load_manifest.json')
    .then((r) => (r.ok ? r.json() : null)).catch(() => null);

  function loadBytes(m, fighters, stage) {
    const iso = Module.meleeIso;
    const size = (p) => { const e = iso && iso.lookup(p); return e ? e.size : 0; };
    // An entry is a file read whole, or {path, bytes} for one read in part.
    const sum = (list) => (list || []).reduce(
      (a, p) => a + (typeof p === 'string' ? size(p) : p.bytes), 0);
    let total = sum(m.common) + sum(m.stages[stage]);
    for (const ck of new Set(fighters)) total += sum((m.fighters[ck] || {}).files);
    // The trace loaded two costumes per fighter (two players); each player loads half.
    for (const ck of fighters) total += sum((m.fighters[ck] || {}).costumes) / 2;
    return total;
  }

  /* The loading screen: a bar of the disc read so far against what this
   * match needs, then -- in netplay -- the wait for the slowest page, until
   * the match's first frame is drawn. */
  function showLoading(netplay, fighters, stage) {
    const t0 = performance.now();
    const base = Module.meleeIsoBytes || 0;   // the disc's header and FST, read before Start
    let total = 0;
    loadManifest.then((m) => {
      if (!m || !Module.meleeIso) return;
      total = loadBytes(m, fighters, stage);
      if (total > 0) $('loading-bar').hidden = false;
    });
    const tick = setInterval(() => {
      if ((Module.meleeMatchFrame | 0) > 0) {
        send({ t: 'loaded' });
        $('loading-fill').style.width = '100%';
        $('loading').classList.add('done');
        console.log(`[lobby] match loaded: read ${(Module.meleeIsoBytes || 0) - base} bytes, estimated ${Math.round(total)}`);
        clearInterval(tick);
        return;
      }
      const read = (Module.meleeIsoBytes || 0) - base;
      const mb = (n) => (n / 2 ** 20).toFixed(1);
      const secs = (performance.now() - t0) / 1000;
      if (total > 0) {
        // Never full until the match is: a load can read a little past the
        // estimate, and the game still has work after the last byte.
        const frac = Math.min(0.99, read / total);
        $('loading-fill').style.width = `${(frac * 100).toFixed(1)}%`;
        $('loading-detail').textContent = `${mb(Math.min(read, total))} of ${mb(total)} MB`;
      } else {
        $('loading-detail').textContent = read >= 0.1 * 2 ** 20 ? `${mb(read)} MB read from the disc` : '';
      }
      // Who is still loading, by name: the relay hears each page's 'loaded'.
      const slow = lobby ? lobby.players.filter((p) => p.inMatch && !p.loaded && p.id !== lobby.you)
        .map((p) => p.name + (p.pre != null && p.pre < 100 ? ` (${p.pre}% downloaded)` : '')) : [];
      if (netplay && total > 0 && read >= total * 0.99 && slow.length) {
        $('loading-text').textContent = `Waiting for ${slow.join(', ')}…`;
      } else if (netplay && secs > 8 && total > 0 && read >= total * 0.99) {
        $('loading-text').textContent = 'Waiting for the other players…';
      } else if (netplay && secs > 8) {
        $('loading-text').textContent = 'Loading — and waiting for the other players…';
      }
    }, 250);
  }
  /* ---- download the match before Start ----
   * Everything comes up the host's upload, which can be slow enough that a
   * load took five minutes. So while players pick, fetch what the match will
   * read -- every load's files, the stage's, each player's fighter -- into
   * the browser's cache (iso_fs.js, Module.meleeIsoPrefetch). Asked again on
   * every lobby change; files already asked for cost nothing. The relay
   * shows each player's progress in their slot. */
  let preSent = -1;
  async function prefetchMatch() {
    if (!discReady || !Module.meleeIsoPrefetch) return;
    const m = await loadManifest;
    if (!m || started) return;
    const files = (list) => (list || []).filter((x) => typeof x === 'string');
    const want = [...files(m.common)];
    if (lobby) {
      want.push(...files(m.stages[lobby.rules.stage]));
      for (const p of lobby.players) {
        const f = m.fighters[p.ckind] || {};
        want.push(...files(f.files), ...files(f.costumes));
      }
    }
    Module.meleeIsoPrefetch(want);
    if (store.get('fullDownload', false)) downloadGame();
  }

  /* ---- download the whole game, once ----
   * On a slow host upload even one match's files take minutes, so a player
   * can fetch everything a VS match can read -- every stage, every fighter in
   * every costume, the common files: ~270 MB, ~200 MB gzipped, no movies --
   * into the browser's cache ahead of time. Remembered: it carries on at
   * each visit until it is done, and files already fetched are kept.
   * Queued after the next match's own files, and paused in a match, where
   * it would share the host's upload with the game. */
  let fullAsked = false;
  async function downloadGame() {
    if (fullAsked || !discReady || !Module.meleeIsoPrefetch) return;
    const m = await loadManifest, iso = Module.meleeIso;
    if (!m || !iso) return;
    fullAsked = true;
    store.set('fullDownload', true);
    // Ask the browser not to evict it under storage pressure.
    try { if (navigator.storage && navigator.storage.persist) navigator.storage.persist(); } catch (e) { /* no */ }
    const files = (list) => (list || []).filter((x) => typeof x === 'string');
    const all = [...files(m.common)];
    for (const k of Object.keys(m.stages)) all.push(...files(m.stages[k]));
    for (const f of Object.values(m.fighters)) {
      all.push(...files(f.files));
      // Every costume: Pl<fighter><colour>.dat, from the fighter's own prefix.
      const prefixes = new Set(files(f.costumes).map((c) => c.slice(0, 4)));
      for (const e of iso.entries) {
        if (prefixes.has(e.path.slice(0, 4)) && /^Pl[A-Z][a-z][A-Z][a-z]\.dat$/.test(e.path)) all.push(e.path);
      }
    }
    Module.meleeIsoPrefetch(all);
  }
  $('btn-download').onclick = () => { downloadGame(); paintDownload(); };
  function paintDownload() {
    const st = Module.meleeIsoPrefetchState;
    const full = fullAsked || store.get('fullDownload', false);
    $('btn-download').hidden = full;
    if (!st || !st.want) return;
    const pct = Math.floor(100 * st.have / st.want);
    const mb = (n) => (n / 2 ** 20).toFixed(0);
    const what = full ? 'the game' : 'this match';
    $('dl').textContent = pct >= 100 ? (full ? 'The whole game is downloaded: matches load from this computer.'
                                            : 'This match is downloaded: ready to play.')
      : `Downloading ${what}: ${pct}% (${mb(st.have)} of ${mb(st.want)} MB)…`;
    $('dl').className = 'pad ' + (pct >= 100 ? 'ok' : 'no');
  }
  setInterval(() => {
    const st = Module.meleeIsoPrefetchState;
    if (!st || !st.want || started) return;
    const pct = Math.floor(100 * st.have / st.want);
    paintDownload();
    if (pct !== preSent && (pct >= 100 || pct - preSent >= 5 || preSent < 0)) { preSent = pct; send({ t: 'pre', pct }); }
  }, 1000);

  // The host's CPU choice travels with the rules; it fills only free slots.
  const isHostCpuCount = (msg) => Math.max(0, Math.min(SLOTS - msg.players.length, msg.rules.cpus | 0));

  // The reload throws the log and the frame profile away: report first.
  async function backToLobby() {
    send({ t: 'done' });
    await sendReportOnce('match');
    setTimeout(() => location.reload(), 100);
  }
  $('btn-lobby').onclick = backToLobby;
  // Click twice: confirm() would stop the page, and the match with it.
  let quitArmed = 0;
  $('btn-quit').onclick = async () => {
    if (performance.now() - quitArmed > 3000) {
      quitArmed = performance.now();
      $('btn-quit').textContent = 'Click again to leave';
      setTimeout(() => { if (performance.now() - quitArmed >= 3000) $('btn-quit').textContent = 'Leave match'; }, 3100);
      $('canvas').focus();
      return;
    }
    $('btn-quit').textContent = 'Leaving…';
    await sendReportOnce('left');
    location.reload();
  };
  $('btn-full').onclick = () => {
    if (document.fullscreenElement) document.exitFullscreen();
    else document.documentElement.requestFullscreen().catch(() => {});
    $('canvas').focus();
  };

  /* ---- gamepad: show it is seen; Start on it returns to the lobby after a match ---- */
  const pads = () => [...(navigator.getGamepads ? navigator.getGamepads() : [])].filter(Boolean);
  let padText = '';
  function padLoop() {
    const ps = pads(), want = 1 + guests.length;
    const name = (p) => p.id.replace(/\s*\(.*\)$/, '');
    let t = !ps.length ? 'No gamepad yet: plug one in and press a button.'
      : ps.length === 1 ? `Gamepad ready: ${name(ps[0])}` : `${ps.length} gamepads ready: ${ps.map(name).join(', ')}`;
    // The first pad is player 1's (the keyboard is too), so player k plays
    // on pad k: with more than one player here, everyone needs a pad.
    if (want > 1 && ps.length < want) t += ` -- ${want} players here: plug in ${want - ps.length} more and press a button on each`;
    if (t !== padText) { padText = t; $('pad').textContent = t; }
    $('pad').className = 'pad ' + (ps.length && ps.length >= want ? 'ok' : 'no');
    if ($('banner').classList.contains('show')) {
      if (ps.some((p) => p.buttons[9] && p.buttons[9].pressed)) backToLobby();     // Start
    }
    requestAnimationFrame(padLoop);
  }
  if (!window.isSecureContext) {
    $('pad').textContent = 'Gamepads need https: open this page through the https:// address.';
  } else {
    requestAnimationFrame(padLoop);
  }

  /* Audio starts only after a gesture; the click on Start is one, but resume
   * on any input in case the context began later. */
  const resumeAudio = () => { const c = Module.SDL2 && Module.SDL2.audioContext; if (c && c.state === 'suspended') c.resume(); };
  ['keydown', 'pointerdown'].forEach((t) => window.addEventListener(t, resumeAudio));
  window.addEventListener('gamepadconnected', resumeAudio);

  /* Right-click over the canvas is suppressed so a drag does not pop a menu. It
   * used to be oncontextmenu="..." in the markup, which the CSP forbids (inline
   * handler attribute); this is the same suppression, from a file. */
  $('canvas').addEventListener('contextmenu', (e) => e.preventDefault());

  /* ---- graphics: say so when the browser is not using the GPU ----
   * Chrome falls back to SwiftShader (WebGL drawn on the CPU) when hardware
   * acceleration is off or the GPU is blocklisted. The game still runs, at a
   * fraction of its speed and with no other symptom, so check for it here:
   * failIfMajorPerformanceCaveat refuses exactly that kind of context. */
  /* Ask for the fast GPU. A laptop with two (Intel + NVIDIA) gives the
   * browser the Intel one by default, and the engine's context asks for
   * nothing in particular: one player's NVIDIA T500 sat idle while the
   * Intel UHD drew the match at 10 fps -- and lockstep held everyone to it.
   * Browsers may ignore this; the warning below says how to force it. */
  (function preferFastGpu() {
    const orig = HTMLCanvasElement.prototype.getContext;
    HTMLCanvasElement.prototype.getContext = function (type, attrs) {
      const asked = attrs;
      if (/^(webgl2?|experimental-webgl)$/.test(type)) {
        attrs = Object.assign({ powerPreference: 'high-performance' }, attrs || {});
      }
      const ctx = orig.call(this, type, attrs);
      if (ctx && type === 'webgl2' && this === $('canvas')) glAttach(ctx, asked);
      return ctx;
    };
  })();

  /* ---- graphics profiling: what the GPU and the WebGL calls cost ----
   * (gpu: opened as render_present starts drawing, closed after the swap.)
 * The frame profile's `late` says the browser handed a frame back late --
   * on one player's laptop 110 ms of every frame -- but not why: the GPU
   * genuinely busy, or a call that makes the page wait on it. Two probes on
   * the engine's own WebGL context say which:
   *
   *   gpu     each frame's GPU time, from EXT_disjoint_timer_query_webgl2,
   *           written into the profile's `gpu` column a few frames late
   *           (where the browser offers the extension at all)
   *   calls   every WebGL call timed, counted and its upload bytes summed,
   *           for 300 frames: on its own once a match runs slow (a second
   *           of frames averaging over 25 ms), or at match frame 600 for a
   *           baseline. ?glprof starts it at match frame 120. The timing
   *           costs a millisecond or two a frame while it runs.
   *
   * Both go in the report (glInfo, glProf), with the context the game got. */
  const glProbe = { ctx: null, timer: null, queries: [], prof: null, done: null, info: null,
                    walls: [], hooked: false };
  function glAttach(gl, asked) {
    if (glProbe.ctx) return;
    glProbe.ctx = gl;
    try { glProbe.timer = gl.getExtension('EXT_disjoint_timer_query_webgl2'); } catch (e) { /* none */ }
    try {
      const dbg = gl.getExtension('WEBGL_debug_renderer_info');
      glProbe.info = {
        asked: asked || null, got: gl.getContextAttributes(),
        renderer: dbg ? gl.getParameter(dbg.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER),
        vendor: dbg ? gl.getParameter(dbg.UNMASKED_VENDOR_WEBGL) : gl.getParameter(gl.VENDOR),
        timerQuery: !!glProbe.timer, maxSamples: gl.getParameter(gl.MAX_SAMPLES),
        extensions: gl.getSupportedExtensions(),
      };
    } catch (e) { glProbe.info = { error: e.message }; }
  }
  const GL_SIZED = /^(bufferData|bufferSubData|texImage[23]D|texSubImage[23]D|compressedTex(Sub)?Image[23]D|readPixels|uniform\w*v)$/;
  function glProfStart(frames, why) {
    const gl = glProbe.ctx;
    if (!gl || glProbe.prof || glProbe.done) return;
    const proto = WebGL2RenderingContext.prototype, stats = {};
    for (const n of Object.getOwnPropertyNames(proto)) {
      const d = Object.getOwnPropertyDescriptor(proto, n);
      if (!d || typeof d.value !== 'function' || n === 'constructor') continue;
      const f = d.value, st = stats[n] = [0, 0, 0, 0];   // calls, ms, worst ms, bytes
      const sized = GL_SIZED.test(n);
      gl[n] = function () {
        const t = performance.now();
        const r = f.apply(gl, arguments);
        const dt = performance.now() - t;
        st[0]++; st[1] += dt; if (dt > st[2]) st[2] = dt;
        if (sized) {
          for (let i = 0; i < arguments.length; i++) {
            const a = arguments[i];
            if (!ArrayBuffer.isView(a)) continue;
            // emscripten passes the whole heap plus an offset and a length:
            // the length is the last argument for buffers (bytes) and
            // uniforms (elements); a texture's is its size, not counted.
            if (a.byteLength > (64 << 20)) {
              const len = arguments[arguments.length - 1];
              if (typeof len === 'number' && /^(buffer|uniform)/.test(n)) st[3] += len * (a.BYTES_PER_ELEMENT || 1);
            } else st[3] += a.byteLength;
            break;
          }
        }
        return r;
      };
    }
    glProbe.prof = { stats, frames: 0, want: frames, why, t0: performance.now(),
                     mframe: Module.meleeMatchFrame | 0 };
    console.log(`[GLPROF] timing every WebGL call for ${frames} frames (${why})`);
  }
  function glProfStop() {
    const pr = glProbe.prof, gl = glProbe.ctx;
    if (!pr) return;
    for (const n of Object.keys(pr.stats)) delete gl[n];   // back to the prototype's
    const ms = performance.now() - pr.t0, F = pr.frames || 1;
    const rows = Object.entries(pr.stats).filter(([, v]) => v[0] > 0)
      .map(([name, [calls, t, worst, bytes]]) => ({
        name, callsPerFrame: +(calls / F).toFixed(1), msPerFrame: +(t / F).toFixed(3),
        worstMs: +worst.toFixed(2), kbPerFrame: +(bytes / F / 1024).toFixed(1) }))
      .sort((a, b) => b.msPerFrame - a.msPerFrame);
    glProbe.done = {
      why: pr.why, frames: pr.frames, fromMatchFrame: pr.mframe, msPerFrame: +(ms / F).toFixed(2),
      glMsPerFrame: +rows.reduce((a, r) => a + r.msPerFrame, 0).toFixed(2),
      callsPerFrame: Math.round(rows.reduce((a, r) => a + r.callsPerFrame, 0)), top: rows.slice(0, 40),
    };
    glProbe.prof = null;
    console.log(`[GLPROF] done: ${glProbe.done.msPerFrame} ms/frame, ${glProbe.done.glMsPerFrame} ms of it in ` +
                `${glProbe.done.callsPerFrame} WebGL calls; top: ` +
                rows.slice(0, 5).map((r) => `${r.name} ${r.msPerFrame}`).join(', '));
  }
  Module.meleeGlProf = (frames) => glProfStart(frames || 300, 'asked');
  /* Once a frame, from the engine's profile row (render.c web_prof_frame):
   * close this frame's timer query and open the next, collect finished ones,
   * and run the call profile. */
  function glOnFrame(o) {
    const p = Module.meleeProf, gl = glProbe.ctx, F = p.fields.length;
    if (gl && glProbe.timer) {
      const T = glProbe.timer;
      if (glProbe.open) {
        gl.endQuery(T.TIME_ELAPSED_EXT);
        glProbe.queries.push({ q: glProbe.open, o, n: p.n });
        glProbe.open = null;
      }
      while (glProbe.queries.length) {
        const h = glProbe.queries[0];
        if (!gl.getQueryParameter(h.q, gl.QUERY_RESULT_AVAILABLE)) break;
        glProbe.queries.shift();
        const ns = gl.getQueryParameter(h.q, gl.QUERY_RESULT);
        gl.deleteQuery(h.q);
        if (!gl.getParameter(T.GPU_DISJOINT_EXT) && p.n - h.n < p.cap) p.buf[h.o + F - 1] = ns / 1e6;
      }
    }
    const mframe = p.buf[o + p.fields.indexOf('mframe')];
    if (glProbe.prof && ++glProbe.prof.frames >= glProbe.prof.want) glProfStop();
    if (mframe > 0 && !glProbe.prof && !glProbe.done) {
      const w = glProbe.walls;
      w.push(p.buf[o]);
      if (w.length > 60) w.shift();
      const asked = new URLSearchParams(location.search).has('glprof');
      if (asked && mframe >= 120) glProfStart(300, '?glprof');
      else if (w.length === 60 && w.reduce((a, b) => a + b, 0) / 60 > 25) glProfStart(300, 'slow frames');
      else if (mframe >= 600) glProfStart(300, 'baseline at frame 600');
    }
  }
  function glOnBegin() {
    const gl = glProbe.ctx, T = glProbe.timer;
    if (!gl || !T || glProbe.open || glProbe.queries.length >= 8) return;
    glProbe.open = gl.createQuery();
    gl.beginQuery(T.TIME_ELAPSED_EXT, glProbe.open);
  }
  setInterval(() => {
    if (!glProbe.hooked && Module.meleeProf) {
      Module.meleeProf.hook = glOnFrame;
      Module.meleeProf.begin = glOnBegin;
      glProbe.hooked = true;
    }
  }, 200);

  /* ---- graphics quality: how many lines the game draws ----
   * The canvas is drawn at its on-screen size (src/port/window.c,
   * web_fit_canvas) up to MELEE_IRES_MAX_LINES; fewer lines, fewer pixels,
   * and the browser stretches the picture to fit. Integrated graphics need
   * it: an Intel UHD drew a 1903x860 match at 5-10 fps, and in lockstep the
   * slowest page sets everyone's speed. Auto is Low on Intel graphics.
   * ?lines=N overrides, for testing. */
  const GFX = { high: 1440, medium: 720, low: 480 };
  let gfxAuto = 'high';
  const gfxLines = () => {
    const q = parseInt(new URLSearchParams(location.search).get('lines'), 10);
    if (q >= 0) return q;
    const pick = store.get('gfx', 'auto');
    return GFX[pick === 'auto' ? gfxAuto : pick] || GFX.high;
  };
  const paintGfx = () => {
    $('gfx').value = store.get('gfx', 'auto');
    $('gfx').options[0].textContent = `Auto (${gfxAuto[0].toUpperCase() + gfxAuto.slice(1)})`;
  };
  $('gfx').onchange = () => { store.set('gfx', $('gfx').value); paintGfx(); };

  (function checkGpu() {
    const probe = (opts) => {
      let gl = null;
      try { gl = document.createElement('canvas').getContext('webgl2', opts); } catch (e) { /* none */ }
      if (!gl) return null;
      const info = gl.getExtension('WEBGL_debug_renderer_info');
      const renderer = info ? String(gl.getParameter(info.UNMASKED_RENDERER_WEBGL)) : '';
      const lose = gl.getExtension('WEBGL_lose_context');
      if (lose) lose.loseContext();      // a probe should not hold one of the page's contexts
      return { renderer };
    };
    // No context at all: say so before a match is started for nothing.
    // Recent Chrome no longer falls back to SwiftShader for WebGL, so with
    // acceleration off or the GPU blocklisted there is no WebGL at all.
    const any = probe({});
    if (!any) {
      $('gpu').innerHTML = '<b>Your browser could not start WebGL 2</b>, which the game needs to draw. ' +
        'In Chrome: Settings → System → turn on <i>Use graphics acceleration when available</i>, ' +
        'relaunch, then check that <code>chrome://gpu</code> lists WebGL2 as available.';
      $('gpu').classList.add('show');
      return;
    }
    console.log('[GPU] WebGL renderer:', any.renderer || '(hidden)');
    gpuRenderer = any.renderer || null;
    if (/intel|swiftshader|llvmpipe|softpipe|software|basic render/i.test(any.renderer || '') &&
        !/\barc\b/i.test(any.renderer)) gfxAuto = 'low';
    paintGfx();
    const software = any.renderer
      ? /swiftshader|llvmpipe|softpipe|software|basic render driver/i.test(any.renderer)
      : !probe({ failIfMajorPerformanceCaveat: true });
    if (software) {
      $('gpu').innerHTML = '<b>Your browser is drawing without the graphics card</b>' +
        (any.renderer ? ` (${escapeHtml(any.renderer)})` : '') + ', so the game will run slowly. ' +
        'In Chrome: Settings → System → turn on <i>Use graphics acceleration when available</i>, ' +
        'relaunch, then check that <code>chrome://gpu</code> lists WebGL as “Hardware accelerated”.';
      $('gpu').classList.add('show');
    } else if (/intel/i.test(any.renderer) && !/arc/i.test(any.renderer)) {
      // Intel graphics: on a laptop that also has an NVIDIA or AMD GPU, the
      // browser is on the slow one.
      $('gpu').innerHTML = `<b>Your browser is using Intel graphics</b> (${escapeHtml(any.renderer)}). ` +
        'If this computer also has an NVIDIA or AMD graphics card, the game will run far better on it: ' +
        'in Windows, Settings → System → Display → <i>Graphics</i>, pick your browser, set it to ' +
        '<i>High performance</i>, then close the browser completely and reopen it. ' +
        '<code>chrome://gpu</code> then names the NVIDIA/AMD card. Until then, Graphics → Low ' +
        '(the default here) draws fewer pixels so it keeps up.';
      $('gpu').classList.add('show');
    }
  })();

  /* ---- frame rate: ?fps shows it always; otherwise it appears once a
   * match has run well under 60 for a couple of seconds ---- */
  (function fpsMeter() {
    const always = new URLSearchParams(location.search).has('fps');
    let last = 0, lastT = performance.now(), slow = 0;
    setInterval(() => {
      const n = Module.meleeFrames | 0, t = performance.now();
      const fps = (n - last) * 1000 / (t - lastT);
      last = n; lastT = t;
      if (!started || !n) return;
      const el = $('fps');
      el.textContent = `${fps.toFixed(0)} fps`;
      el.classList.toggle('slow', fps < 55);
      // Two slow readings in a row, so a stage load's hitch does not count.
      slow = fps < 50 ? slow + 1 : 0;
      if (always || slow >= 2) el.classList.add('show');
    }, 2000);
  })();

  /* ---- reports: what the host needs to fix a friend's slow or broken match ----
   * A report is the page's log, any crash, the browser and GPU, the match,
   * and the engine's frame profile (Module.meleeProf, one row a frame from
   * src/port/render.c's web_prof_frame: where each frame's time went). It is
   * POSTed to the host's server (tools/wasm/serve.js), which keeps it in its
   * reports directory; `node tools/wasm/report.js <file>` reads one.
   *
   * Sent by itself at the end of a match, on leaving one, and on a crash;
   * "Send report" sends one any time, with a note. */
  let lastStart = null;          // the relay's start message
  const hidden = { count: 0, ms: 0, since: document.hidden ? performance.now() : 0 };
  document.addEventListener('visibilitychange', () => {
    if (document.hidden) { hidden.count++; hidden.since = performance.now(); }
    else if (hidden.since) { hidden.ms += performance.now() - hidden.since; hidden.since = 0; }
  });
  // Main-thread stalls the browser itself saw (Chrome): garbage collection,
  // another tab, an extension.
  const longTasks = { count: 0, ms: 0, worst: [] };
  try {
    new PerformanceObserver((list) => {
      for (const e of list.getEntries()) {
        longTasks.count++; longTasks.ms += e.duration;
        longTasks.worst.push([Math.round(e.startTime), Math.round(e.duration)]);
        longTasks.worst.sort((a, b) => b[1] - a[1]);
        longTasks.worst.length = Math.min(longTasks.worst.length, 30);
      }
    }).observe({ type: 'longtask', buffered: true });
  } catch (e) { /* not this browser */ }

  /* The profile, oldest row first, and what it says. "Slow" frames are the
   * ones over 20 ms (a missed 60 Hz vsync); the average of each column over
   * them is the answer to "where did the time go". */
  function profile() {
    const p = Module.meleeProf;
    if (!p || !p.n) return null;
    const F = p.fields.length, rows = Math.min(p.n, p.cap);
    const data = new Float32Array(rows * F);
    const first = p.n > p.cap ? p.n % p.cap : 0;
    data.set(p.buf.subarray(first * F, rows * F), 0);
    if (first) data.set(p.buf.subarray(0, first * F), (rows - first) * F);
    const col = (r, name) => data[r * F + p.fields.indexOf(name)];
    const inMatch = [];
    for (let r = 1; r < rows; r++) if (col(r, 'mframe') > 0) inMatch.push(r);
    const use = inMatch.length > 60 ? inMatch : [...Array(rows).keys()].slice(1);
    const stats = {};
    for (const f of p.fields) {
      if (f === 'mframe') continue;
      const v = use.map((r) => col(r, f)).filter(Number.isFinite).sort((a, b) => a - b);
      const q = (x) => +(v[Math.min(v.length - 1, Math.floor(v.length * x))] || 0).toFixed(2);
      stats[f] = { mean: +(v.reduce((a, b) => a + b, 0) / (v.length || 1)).toFixed(2),
                   p50: q(0.5), p95: q(0.95), p99: q(0.99), max: q(1) };
    }
    const slow = use.filter((r) => col(r, 'wall') > 20);
    const inSlow = {};
    for (const f of p.fields) {
      if (f === 'mframe' || f === 'wall') continue;
      const sv = slow.map((r) => col(r, f)).filter(Number.isFinite);
      inSlow[f] = +(sv.reduce((a, b) => a + b, 0) / (sv.length || 1)).toFixed(2);
    }
    const wallSum = use.reduce((a, r) => a + col(r, 'wall'), 0);
    const worst = [...use].sort((a, b) => col(b, 'wall') - col(a, 'wall')).slice(0, 40)
      .map((r) => Object.fromEntries(p.fields.map((f) => [f, +col(r, f).toFixed(2)])));
    let bin = '';
    const bytes = new Uint8Array(data.buffer);
    for (let i = 0; i < bytes.length; i += 0x8000) bin += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
    return {
      fields: p.fields, rows, total: p.n, matchRows: inMatch.length,
      summary: {
        frames: use.length, seconds: +(wallSum / 1000).toFixed(1),
        fps: +(use.length * 1000 / (wallSum || 1)).toFixed(1),
        over20ms: slow.length, over33ms: use.filter((r) => col(r, 'wall') > 33.4).length,
        over50ms: use.filter((r) => col(r, 'wall') > 50).length,
        stats, slowFramesAverage: inSlow,
      },
      worst,
      data: btoa(bin),           // Float32 little-endian, rows x fields
    };
  }

  const myEntry = () => (lobby && lobby.players.find((x) => x.id === lobby.you)) || {};
  const myName = () => me.name || myEntry().name || null;

  async function buildReport(kind, note, crash, small) {
    const c = $('canvas');
    let build = null;
    try {
      const r = await fetch('melee.wasm', { method: 'HEAD', cache: 'no-store' });
      build = { modified: r.headers.get('last-modified'), size: r.headers.get('content-length') };
    } catch (e) { /* offline */ }
    const prof = profile();
    if (prof && small) delete prof.data;
    return {
      v: 1, kind, note: note || '', time: new Date().toISOString(),
      name: myName(), uptimeMs: Math.round(performance.now()),
      page: {
        url: location.pathname + location.search, ua: navigator.userAgent,
        platform: navigator.userAgentData ? navigator.userAgentData.platform : navigator.platform,
        cores: navigator.hardwareConcurrency || null, memoryGB: navigator.deviceMemory || null,
        dpr: devicePixelRatio, screen: [screen.width, screen.height],
        viewport: [innerWidth, innerHeight], canvas: [c.width, c.height],
        fullscreen: !!document.fullscreenElement, gpu: gpuRenderer, gfxLines: gfxLines(),
        jspi: typeof WebAssembly.Suspending === 'function', isolated: !!window.crossOriginIsolated,
        // What this page is running, as opposed to `build`, the server's now.
        running: { lobby: lobbyVersion, wasm: wasmVersion || null },
        build,
      },
      match: lastStart && { slot: lastStart.slot, players: lastStart.players, seed: lastStart.seed,
                            rules: lastStart.rules, delay: lastStart.delay, why: lastStart.why },
      net: { ping: myEntry().ping == null ? null : myEntry().ping, impair },
      hidden: { count: hidden.count, ms: Math.round(hidden.ms + (hidden.since ? performance.now() - hidden.since : 0)) },
      longTasks: { count: longTasks.count, ms: Math.round(longTasks.ms), worst: longTasks.worst },
      disc: { net: Module.meleeIsoNet || null, prefetch: Module.meleeIsoPrefetchState || null },
      glInfo: glProbe.info, glProf: glProbe.done || (glProbe.prof ? { running: glProbe.prof.frames } : null),
      matchFrame: Module.meleeMatchFrame | 0, framesPresented: Module.meleeFrames | 0,
      crash: crash || null,
      errors: pageErrors,
      prof,
      logs: small ? pageLog.slice(-300) : pageLog,
    };
  }

  async function sendReport(kind, note, crash) {
    const report = await buildReport(kind, note, crash, false);
    let body = new TextEncoder().encode(JSON.stringify(report));
    const headers = { 'Content-Type': 'application/json' };
    if (typeof CompressionStream === 'function') {
      try {
        const gz = new Blob([body]).stream().pipeThrough(new CompressionStream('gzip'));
        body = new Uint8Array(await new Response(gz).arrayBuffer());
        headers['Content-Encoding'] = 'gzip';
      } catch (e) { /* send it plain */ }
    }
    const ctl = new AbortController();
    const timer = setTimeout(() => ctl.abort(), 8000);
    try {
      const r = await fetch('report', { method: 'POST', headers, body, signal: ctl.signal });
      if (!r.ok) throw new Error(`the host answered ${r.status}`);
      console.log(`[report] sent (${kind}, ${(body.length / 1024).toFixed(0)} KB)`);
      return true;
    } catch (e) {
      console.warn('[report] could not send:', e.message);
      return false;
    } finally { clearTimeout(timer); }
  }
  const reportsSent = new Set();
  // One automatic report of each kind a page load; the button always sends.
  function sendReportOnce(kind, note, crash) {
    if (reportsSent.has(kind)) return Promise.resolve(true);
    reportsSent.add(kind);
    return sendReport(kind, note, crash);
  }
  // Closing the tab mid-match: a beacon is all there is time for, and it
  // carries 64 KB at most -- the summary and the log's tail, no raw rows.
  window.addEventListener('pagehide', () => {
    if (!started || reportsSent.size) return;
    // Synchronous: the page is going. buildReport's await is only the HEAD.
    const p = profile();
    if (p) delete p.data;
    const r = { v: 1, kind: 'closed', time: new Date().toISOString(), name: myName(),
                match: lastStart && { slot: lastStart.slot, seed: lastStart.seed },
                prof: p, errors: pageErrors.slice(-10), logs: pageLog.slice(-150) };
    try { navigator.sendBeacon('report', new Blob([JSON.stringify(r)], { type: 'application/json' })); } catch (e) { /* gone */ }
  });

  let crashShown = false;
  function crashed(source, message, stack) {
    const crash = { source, message: String(message).slice(0, 4000), stack: String(stack || '').slice(0, 20000),
                    matchFrame: Module.meleeMatchFrame | 0 };
    pageErrors.push(crash);
    if (pageErrors.length > 50) pageErrors.shift();
    if (crashShown) return;
    crashShown = true;
    /* The engine and its loader from different builds: a browser cache
     * holding an old one (it rebuilt a tab without asking the server). One
     * reload that bypasses the cache cures it; the session flag keeps a real
     * problem from looping. */
    if (/LinkError|function import requires a callable/.test(crash.message)) {
      let tried = false;
      try { tried = sessionStorage.getItem('melee.staleReload') === '1'; sessionStorage.setItem('melee.staleReload', '1'); } catch (e) { tried = true; }
      sendReportOnce('crash', 'stale cache', crash).finally(() => {
        if (!tried) { location.replace(location.pathname + '?fresh=' + Date.now()); return; }
        $('loading').classList.add('done');
        $('banner-text').textContent = 'This browser is holding an old copy of the game. Press Ctrl+Shift+R (Cmd+Shift+R on a Mac) to reload it.';
        $('banner').classList.add('show');
      });
      return;
    }
    if (started) {
      $('loading').classList.add('done');
      $('banner-text').textContent = 'The game crashed. Sending a report to the host…';
      $('banner').classList.add('show');
    }
    sendReportOnce('crash', '', crash).then((ok) => {
      if (started) {
        $('banner-text').textContent = ok ? 'The game crashed. A report went to the host.'
          : 'The game crashed, and the report could not be sent.';
      }
    });
  }
  window.addEventListener('error', (e) => {
    // A resource that failed to load is not a crash.
    if (!e.error && !e.message) return;
    // Nor is an extension's error: keep it for the report, but carry on.
    if (e.filename && !e.filename.startsWith(location.origin)) {
      pageErrors.push({ source: 'other', message: `${e.message} (${e.filename})`, matchFrame: Module.meleeMatchFrame | 0 });
      return;
    }
    crashed('error', e.message, e.error && e.error.stack);
  });
  window.addEventListener('unhandledrejection', (e) => {
    const r = e.reason;
    crashed('rejection', r && r.message ? r.message : String(r), r && r.stack);
  });

  async function reportButton(btn) {
    /* No note once the engine runs. prompt() is modal: it stops this page's
     * thread, the game on it, and in lockstep everyone else's -- a player
     * who clicked Send report mid-match froze the match for both of them
     * until the box was closed. A text box would fight the game for the
     * keyboard, so in a match the report goes as it is. */
    let note = '';
    if (!started) {
      note = prompt('Send a report to the host. What happened? (optional)', '');
      if (note === null) return;
    }
    btn.disabled = true;
    const label = btn.textContent;
    btn.textContent = 'Sending…';
    const ok = await sendReport('manual', note);
    btn.textContent = ok ? 'Report sent' : 'Could not send';
    setTimeout(() => { btn.textContent = label; btn.disabled = false; }, 3000);
    if (started) $('canvas').focus();
  }
  for (const id of ['btn-report', 'btn-report-hud', 'btn-report-banner']) {
    const b = $(id);
    if (b) b.onclick = () => reportButton(b);
  }

  paintPicks();
  connect();
