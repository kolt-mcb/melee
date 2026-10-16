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
  const CONTROLS = [
    ['Gamepad', 'Plays as you. Plug it in before the match and press a button.'],
    ['Move', '<kbd>←</kbd><kbd>↑</kbd><kbd>→</kbd><kbd>↓</kbd> or <kbd>W</kbd><kbd>A</kbd><kbd>S</kbd><kbd>D</kbd>'],
    ['C-stick', '<kbd>I</kbd><kbd>J</kbd><kbd>K</kbd><kbd>L</kbd>'],
    ['A / B', '<kbd>Z</kbd> / <kbd>X</kbd>'],
    ['Jump', '<kbd>Space</kbd> or <kbd>V</kbd>'],
    ['Shield', '<kbd>Shift</kbd> / <kbd>Alt</kbd>'],
    ['Grab', '<kbd>C</kbd>'], ['Start (pause)', '<kbd>Enter</kbd>'],
  ];

  const $ = (id) => document.getElementById(id);
  const store = {
    get(k, d) { try { const v = localStorage.getItem('melee.' + k); return v === null ? d : JSON.parse(v); } catch (e) { return d; } },
    set(k, v) { try { localStorage.setItem('melee.' + k, JSON.stringify(v)); } catch (e) { /* private mode */ } },
  };
  const setStatus = (t, bad) => { $('status').textContent = t; $('status').classList.toggle('bad', !!bad); };

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
        $('banner-text').textContent = desynced
          ? 'Match over -- but the players fell out of step (desync).' : 'Match over.';
        $('banner').classList.add('show');
      },
    },
    print: (t) => console.log(t),
    printErr: (t) => console.error(t),
    setStatus: () => {},
  };
  let discReady = null;
  const whenEngine = () => new Promise((res) => {
    const t = () => (Module.meleeIsoOpenUrl ? res() : setTimeout(t, 50)); t();
  });
  whenEngine().then(() => {
    discReady = Module.meleeIsoOpenUrl('disc.iso').catch((e) => { setStatus(e.message, true); throw e; });
  });

  /* ---- lobby state ---- */
  const me = { name: store.get('name', ''), ckind: store.get('ckind', 8), color: store.get('color', 0) };
  let lobby = null;
  let rules = { cpus: store.get('cpus', 0), cpulevel: store.get('cpulevel', 9) };

  $('controls').innerHTML = CONTROLS.map(([a, b]) => `<tr><td>${a}</td><td>${b}</td></tr>`).join('');
  $('fighters').innerHTML = FIGHTERS.map((f, i) => `<button data-ck="${i}">${f}</button>`).join('');
  $('colors').innerHTML = Array.from({ length: COSTUMES }, (_, i) => `<button data-col="${i}">${i + 1}</button>`).join(' ');
  $('stage').innerHTML = STAGES.map(([v, n]) => `<option value="${v}">${n}</option>`).join('');
  $('cpulevel').innerHTML = Array.from({ length: 9 }, (_, i) => `<option>${i + 1}</option>`).join('');
  $('name').value = me.name;

  function paintPicks() {
    document.querySelectorAll('[data-ck]').forEach((b) => b.classList.toggle('on', +b.dataset.ck === me.ckind));
    document.querySelectorAll('[data-col]').forEach((b) => b.classList.toggle('on', +b.dataset.col === me.color));
  }
  function sendPick() {
    store.set('name', me.name); store.set('ckind', me.ckind); store.set('color', me.color);
    send({ t: 'pick', name: me.name, ckind: me.ckind, color: me.color });
    paintPicks();
  }
  $('fighters').onclick = (e) => { const b = e.target.closest('[data-ck]'); if (b) { me.ckind = +b.dataset.ck; sendPick(); } };
  $('colors').onclick = (e) => { const b = e.target.closest('[data-col]'); if (b) { me.color = +b.dataset.col; sendPick(); } };
  $('name').oninput = () => { me.name = $('name').value; sendPick(); };

  const isHost = () => lobby && lobby.host === lobby.you;
  function sendRules() {
    send({ t: 'rules', stage: +$('stage').value, stocks: +$('stocks').value || 4,
           minutes: Math.max(0, +$('minutes').value || 0), cpus: rules.cpus, cpulevel: rules.cpulevel });
  }
  ['stage', 'stocks', 'minutes'].forEach((id) => { $(id).onchange = sendRules; });
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
        <div class="who">P${i + 1} · ${escapeHtml(p.name)}${p.id === lobby.host ? '<span class="tag">host</span>' : ''}</div>
        <div class="what">${FIGHTERS[p.ckind]} · costume ${p.color + 1}${p.ping != null ? ` · ${p.ping} ms` : ''}${p.inMatch ? ' · <b>in a match</b>' : ''}</div></div>`;
    }).join('');
    const host = isHost();
    $('host-note').textContent = host ? 'you are the host' : 'the host chooses';
    ['stage', 'stocks', 'minutes', 'cpus', 'cpulevel'].forEach((id) => { $(id).disabled = !host; });
    $('stage').value = lobby.rules.stage;
    $('stocks').value = lobby.rules.stocks;
    $('minutes').value = lobby.rules.minutes;
    const free = Math.max(0, SLOTS - Math.min(SLOTS, ps.length));
    $('cpus').innerHTML = Array.from({ length: free + 1 }, (_, i) => `<option>${i}</option>`).join('');
    if (!host) { rules.cpus = lobby.rules.cpus | 0; rules.cpulevel = lobby.rules.cpulevel || 9; }
    $('cpus').value = Math.min(rules.cpus, free);
    $('cpulevel').value = rules.cpulevel;
    $('btn-start').disabled = !host || lobby.inMatch;
    $('btn-start').textContent = lobby.inMatch ? 'A match is running…' : host ? 'Start match' : 'Waiting for the host';
    const n = Math.min(SLOTS, ps.length);
    setStatus(`${n} player${n === 1 ? '' : 's'} here${lobby.waiting ? `, ${lobby.waiting} waiting for a free slot` : ''}`);
  }
  const escapeHtml = (s) => s.replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

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
    else if (msg.t === 'lobby') { lobby = msg; if (!started) paintLobby(); }
    else if (msg.t === 'start') startMatch(msg);
  }
  function send(obj) { if (ws && ws.readyState === 1) ws.send(new TextEncoder().encode(JSON.stringify(obj))); }
  let started = false;
  function connect() {
    ws = new WebSocket(`${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/net`);
    ws.binaryType = 'arraybuffer';
    ws.onopen = () => send({ t: 'hello', name: me.name, ckind: me.ckind, color: me.color });
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
    document.body.classList.add('playing');
    setStatus('Starting…');
    try { await discReady; } catch (e) { return; }
    const humans = msg.players.length;
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
      MELEE_BOOT_STOCKS: String(msg.rules.stocks),
      MELEE_BOOT_TIME: String(msg.rules.minutes * 60),
      MELEE_SEED: msg.seed.toString(16),
    };
    if (cpuCount) env.MELEE_BOOT_CPU = levels.join(',');
    if (humans > 1) {
      Object.assign(env, {
        MELEE_NET: 'web', MELEE_NET_SLOT: String(msg.slot), MELEE_NET_PLAYERS: String(humans),
        MELEE_NET_SEED: msg.seed.toString(16), MELEE_NET_DELAY: String(msg.delay),
        // The slowest page's load is the first wait; a player who leaves is
        // announced by the relay, so a long timeout costs nothing.
        MELEE_NET_TIMEOUT: '120000',
      });
    }
    // Before main(): emscripten reads ENV on the first getenv, and the
    // constructors that call it run only once the start dependency clears.
    Object.assign(Module.ENV, env);
    console.log('[lobby] start', JSON.stringify(env), `-- delay ${msg.delay}: ${msg.why || ''}`);
    $('canvas').focus();
    resolveStart();
    showLoading(humans > 1);
  }

  /* The loading screen: the disc read so far, then -- in netplay -- the wait
   * for the slowest page, until the match's first frame is drawn. */
  function showLoading(netplay) {
    const t0 = performance.now();
    const tick = setInterval(() => {
      if ((Module.meleeMatchFrame | 0) > 0) {
        $('loading').classList.add('done');
        clearInterval(tick);
        return;
      }
      const mb = (Module.meleeIsoBytes || 0) / 2 ** 20;
      const secs = (performance.now() - t0) / 1000;
      $('loading-detail').textContent = mb >= 0.1 ? `${mb.toFixed(1)} MB read from the disc` : '';
      if (netplay && secs > 8) $('loading-text').textContent = 'Loading — and waiting for the other players…';
    }, 250);
  }
  // The host's CPU choice travels with the rules; it fills only free slots.
  const isHostCpuCount = (msg) => Math.max(0, Math.min(SLOTS - msg.players.length, msg.rules.cpus | 0));

  function backToLobby() { send({ t: 'done' }); setTimeout(() => location.reload(), 100); }
  $('btn-lobby').onclick = backToLobby;
  $('btn-quit').onclick = () => { if (confirm('Leave the match? The others carry on without you.')) location.reload(); };
  $('btn-full').onclick = () => {
    if (document.fullscreenElement) document.exitFullscreen();
    else document.documentElement.requestFullscreen().catch(() => {});
    $('canvas').focus();
  };

  /* ---- gamepad: show it is seen; Start on it returns to the lobby after a match ---- */
  function padName() { const p = [...(navigator.getGamepads ? navigator.getGamepads() : [])].find(Boolean); return p && p.id; }
  function padLoop() {
    const n = padName();
    $('pad').textContent = n ? `Gamepad ready: ${n.replace(/\s*\(.*\)$/, '')}` : 'No gamepad yet: plug one in and press a button.';
    $('pad').className = 'pad ' + (n ? 'ok' : 'no');
    if ($('banner').classList.contains('show')) {
      const p = [...navigator.getGamepads()].find(Boolean);
      if (p && p.buttons[9] && p.buttons[9].pressed) backToLobby();     // Start
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
    // No context at all is left to the game to report: it can be a GPU
    // process still starting, and the game's own error is more specific.
    const any = probe({});
    if (!any) return;
    console.log('[GPU] WebGL renderer:', any.renderer || '(hidden)');
    const software = any.renderer
      ? /swiftshader|llvmpipe|softpipe|software|basic render driver/i.test(any.renderer)
      : !probe({ failIfMajorPerformanceCaveat: true });
    if (software) {
      $('gpu').innerHTML = '<b>Your browser is drawing without the graphics card</b>' +
        (any.renderer ? ` (${escapeHtml(any.renderer)})` : '') + ', so the game will run slowly. ' +
        'In Chrome: Settings → System → turn on <i>Use graphics acceleration when available</i>, ' +
        'relaunch, then check that <code>chrome://gpu</code> lists WebGL as “Hardware accelerated”.';
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

  paintPicks();
  connect();
