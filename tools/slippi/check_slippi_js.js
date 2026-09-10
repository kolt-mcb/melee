// Read a .slp with Project Slippi's own parser, @slippi/slippi-js -- the
// library every tool in that ecosystem is built on. Structural checks live in
// verify_slp.py; this answers the different question of whether the file the
// port wrote means to Slippi's reader what it was meant to mean.
//
//   npm install @slippi/slippi-js
//   node tools/slippi/check_slippi_js.js <file.slp>
const { SlippiGame } = require("@slippi/slippi-js");

const path = process.argv[2];
if (!path) { console.error("usage: check_slippi_js.js <file.slp>"); process.exit(2); }

const game = new SlippiGame(path);
const settings = game.getSettings();
const metadata = game.getMetadata();
const frames = game.getFrames();
const stats = game.getStats();
const gameEnd = game.getGameEnd();

const keys = Object.keys(frames).map(Number).sort((a, b) => a - b);
console.log("slippiVersion  ", settings.slpVersion);
console.log("stage          ", settings.stageId);
console.log("isTeams        ", settings.isTeams, " timer", settings.timerType,
            " startingTimer", settings.startingTimerSeconds);
console.log("players        ", settings.players.map(p =>
  `p${p.playerIndex} char=${p.characterId} color=${p.characterColor} ` +
  `type=${p.type} stocks=${p.startStocks} cpu=${p.cpuLevel}`).join("\n                "));
console.log("frames         ", keys.length, `(${keys[0]} .. ${keys[keys.length - 1]})`);
console.log("gameEnd        ", JSON.stringify(gameEnd));
console.log("metadata       ", JSON.stringify(metadata));

// A frame slippi-js considers complete has every port's pre and post present.
let bad = 0;
for (const k of keys) {
  const f = frames[k];
  if (!f.players) { console.log(`frame ${k}: no players`); bad++; continue; }
  for (const p of settings.players) {
    const pl = f.players[p.playerIndex];
    if (!pl || !pl.pre || !pl.post) {
      if (bad < 5) console.log(`frame ${k}: port ${p.playerIndex} incomplete`);
      bad++;
    }
  }
}
console.log("incomplete     ", bad);

// The stats module is the real exercise: it walks actionStateId, percent,
// stocks, hitlag and the rest across every frame, and throws or produces
// nonsense if any of them is misread.
console.log("stocks         ", stats.stocks.length, "stock entries");
console.log("conversions    ", stats.conversions.length);
console.log("actionCounts   ", JSON.stringify(stats.actionCounts.map(a =>
  ({ i: a.playerIndex, wavedash: a.wavedashCount, dash: a.dashDanceCount,
     airDodge: a.airDodgeCount, attacks: a.attackCount }))));
console.log("overall        ", JSON.stringify(stats.overall.map(o =>
  ({ i: o.playerIndex, inputs: o.inputCounts.total, apm: Math.round(o.inputsPerMinute.ratio * 100) / 100,
     openings: o.totalDamage ? Math.round(o.totalDamage * 10) / 10 : 0 }))));

process.exit(bad ? 1 : 0);
