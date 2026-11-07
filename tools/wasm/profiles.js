'use strict';
/* Each player's saved preferences, by the Google account the gateway signed
 * them in with.
 *
 * Caddy's SSO gate (deploy/web/caddy-melee.block) puts the signed-in address
 * in X-Forwarded-Email on every request it lets through, the /net WebSocket
 * included, and relay.js reads it as the member's identity. Before this the
 * address only counted sockets; now it names the player -- the lobby shows
 * it under the name, so "who is P3" has an answer -- and keys a profile: the
 * name, fighter, costume, local players, CPU rules, key bindings and volume
 * the page used to keep in its own localStorage alone. A player who sits at
 * a different computer, or clears the browser, gets them back from here.
 *
 * One JSON file per address in the directory serve.js names (--profiles,
 * default <reports>/profiles so that it lands on the host directory the
 * compose file already mounts, and no container is recreated for it). Each
 * write is whole-file and atomic (temp + rename), sanitised to the schema
 * below: the page sends whatever it likes, and only these keys, these types
 * and these ranges reach the disk.
 *
 * Per-device settings (graphics cap, "download the whole game") are
 * deliberately not here: a laptop and a desktop want different ones. */

const fs = require('fs');
const path = require('path');

let DIR = null;
const MAX_PROFILES = 500;       // the allowlist is a household; this bounds the disk anyway
const MAX_BYTES = 16 << 10;     // one saved profile, after sanitising

const isEmail = (s) => typeof s === 'string' && s.length <= 254 && /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(s);

exports.init = (dir) => {
    DIR = dir;
    try { fs.mkdirSync(DIR, { recursive: true }); } catch (e) { console.log(`[profiles] cannot create ${DIR}: ${e.message}`); DIR = null; }
    return DIR;
};
exports.dir = () => DIR;

/* The identity the relay saw, as an address, or null: off the gateway it is
 * a socket address, and that names a computer, not a player. Lower-cased so
 * that Kolt@ and kolt@ are one profile. */
exports.emailOf = (ident) => (isEmail(ident) ? ident.trim().toLowerCase() : null);

/* An address as a file name the operator can read: `kolt@example.com.json`.
 * Anything outside a safe set becomes `_`; a collision between two such
 * addresses would merely share a profile. */
const fileOf = (email) => path.join(DIR, email.replace(/[^a-z0-9._@+-]/g, '_').slice(0, 120) + '.json');

const int = (v, lo, hi) => (Number.isInteger(v) && v >= lo && v <= hi ? v : undefined);
const str = (v, n) => (typeof v === 'string' ? v.slice(0, n) : undefined);

/* What a page may save. Mirrors lobby.js SYNCED and the ranges relay.js
 * accepts in a pick. Unknown keys are dropped, bad values skipped. */
function sanitise(p) {
    if (!p || typeof p !== 'object' || Array.isArray(p)) return null;
    const out = {};
    const put = (k, v) => { if (v !== undefined) out[k] = v; };
    put('name', str(p.name, 16));
    put('ckind', int(p.ckind, 0, 25));
    put('color', int(p.color, 0, 5));
    if (Array.isArray(p.guests)) {
        put('guests', p.guests.slice(0, 3).map((g) => ({
            ckind: (g && int(g.ckind, 0, 25)) ?? 8, color: (g && int(g.color, 0, 5)) ?? 0,
        })));
    }
    put('cpus', int(p.cpus, 0, 7));
    put('cpulevel', int(p.cpulevel, 1, 9));
    put('keyPreset', str(p.keyPreset, 16));
    if (p.keys && typeof p.keys === 'object' && !Array.isArray(p.keys)) {
        const keys = {};
        for (const [act, codes] of Object.entries(p.keys).slice(0, 32)) {
            if (!/^[a-z0-9]{1,16}$/.test(act) || !Array.isArray(codes)) continue;
            keys[act] = codes.slice(0, 4).filter((c) => typeof c === 'string' && /^[A-Za-z0-9]{1,24}$/.test(c));
        }
        put('keys', keys);
    }
    put('volume', int(p.volume, 0, 100));
    if (typeof p.muted === 'boolean') put('muted', p.muted);
    return out;
}
exports.sanitise = sanitise;

exports.load = (email) => {
    if (!DIR || !isEmail(email)) return null;
    try {
        const o = JSON.parse(fs.readFileSync(fileOf(email), 'utf8'));
        return sanitise(o.prefs);
    } catch (e) {
        if (e.code !== 'ENOENT') console.log(`[profiles] ${email}: unreadable profile: ${e.message}`);
        return null;
    }
};

/* Saves the whole profile; the page sends every synced key each time. Returns
 * what was written, or null when nothing was. */
exports.save = (email, prefs) => {
    if (!DIR || !isEmail(email)) return null;
    const clean = sanitise(prefs);
    if (!clean) return null;
    const file = fileOf(email);
    try {
        if (!fs.existsSync(file) && fs.readdirSync(DIR).length >= MAX_PROFILES) {
            console.log(`[profiles] ${email}: not saved, ${MAX_PROFILES} profiles already`);
            return null;
        }
        const body = JSON.stringify({ email, savedAt: new Date().toISOString(), prefs: clean }, null, 1);
        if (body.length > MAX_BYTES) return null;
        const tmp = `${file}.${process.pid}.tmp`;
        fs.writeFileSync(tmp, body);
        fs.renameSync(tmp, file);
        return clean;
    } catch (e) {
        console.log(`[profiles] ${email}: not saved: ${e.message}`);
        return null;
    }
};
