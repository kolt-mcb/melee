# What comparable projects do

Notes behind the choices in [LEGAL.md](../../LEGAL.md), gathered from the
projects' own repositories and from GitHub's DMCA notice archive. Everything
below was read from a primary source unless marked otherwise; this is a record
of what other people do, not legal advice.

## The bring-your-own-copy pattern is standard

Every comparable port requires the player's own game and ships no assets:

| Project | License | How it words the requirement |
|-|-|-|
| [Ship of Harkinian](https://github.com/HarbourMasters/Shipwright) (OoT) | none in root; components MIT | "The Ship does not include any copyrighted assets. You are required to provide a supported copy of the game." + "we do not condone piracy" |
| [2 Ship 2 Harkinian](https://github.com/HarbourMasters/2ship2harkinian) (MM) | CC0-1.0 | same wording |
| [Starship](https://github.com/HarbourMasters/Starship) | CC0-1.0 | same, with accepted SHA-1s printed in the README |
| [Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp) | GPL-3.0 | "This repository and its releases do not contain game assets. The original game is required to build or run this project." |
| [Perfect Dark](https://github.com/fgsfdsfgs/perfect_dark) | MIT | upstream ships an MD5 table of accepted ROMs |
| [sm64-port](https://github.com/sm64-port/sm64-port) | none | "A prior copy of the game is required to extract the assets." |
| [OpenGOAL](https://github.com/open-goal/jak-project) | ISC | the strongest wording of the group: a bold warning not to use the project without your own legally purchased copy |

Two mechanisms recur and both are implemented here:

- **A hash list of accepted revisions.** Ship of Harkinian keeps
  `docs/supportedHashes.json`; Perfect Dark and sm64 ship hash tables and their
  extraction scripts check against them, reporting found-vs-expected.
- **Refusing to run without a valid copy.** Zelda64Recomp validates at launch
  and distinguishes "not a valid ROM", "not the correct game", and "the correct
  game, but the wrong version". This was the single strongest posture found,
  and `fs_have_game_data()` follows it.

## An unlicensed upstream is common here

Where a GameCube/N64 decompilation does carry a license, CC0-1.0 is the house
style: pikmin2, sms, mkdd, mkw, brawl, zeldaret/tp, zeldaret/ss, zeldaret/tww
and n64decomp/sm64 are all CC0; ttyd is MIT. But a large cohort ships no
license at all — melee, gnt4, kar, smg, the dolsdk repos, **zeldaret/oot**,
**zeldaret/mm**, and the pret Pokémon repos.

So doldecomp/melee having no LICENSE is normal rather than an oversight, and
it does not stop a fork existing. It does mean nobody has granted terms for
that code, which is why [LEGAL.md](../../LEGAL.md) grants none either.

MIT for the port layer matches what nearly every port in this space chose.
CC0-1.0 would also fit and is closer to the decomp house style; MIT was picked
for the attribution requirement.

## What has actually drawn enforcement

Across every Nintendo notice in the [github/dmca](https://github.com/github/dmca)
archive from 2014 to 2026 — 88 of them — **none** mentions Melee, Slippi, a
decompilation, a recompilation, Dolphin, or sm64. The targets cluster into:

- **Switch emulators and their fork networks** — yuzu (8,535 repos in one
  April 2024 notice), suyu, torzu, Citron, Eden, Ryujinx, skyline.
- **Key and TPM circumvention tooling** — `prod.keys`, Lockpick_RCM, LibHac,
  sigpatch updaters.
- **Hosted playable ROMs** — sites serving the games in the browser.
- **Asset reimplementations** — FullScreenMario and similar.

The recurring legal hook in the modern notices is DMCA §1201 anti-circumvention
over cryptographic keys, not the existence of reimplemented code. The Yuzu
settlement centred on `prod.keys`; the Dolphin-on-Steam episode was about the
bundled Wii Common Key; Portal 64 was shut down over Nintendo's proprietary
**libultra SDK**, not over its game code.

That last one is the most relevant warning here. The riskiest material in this
repository is not the reimplemented game logic — it is the SDK reconstruction
under `extern/dolphin/`, `src/MSL/`, `src/MetroTRK/` and `src/Runtime/`,
inherited from upstream. It is the closest analogue to what killed Portal 64.

Nintendo's Melee-specific aggression is real but has been directed at events
and services rather than repositories: the Big House Online cease-and-desist
over Slippi netplay in 2020, Smash World Tour in 2022, and the 2023 community
tournament guidelines. **Avoid netplay branding**, and avoid anything that
looks like a competing service.

## Consequences for this repository

1. **Source only, no release binaries.** GitHub's Terms of Service grant other
   users the right to view and fork a public repository regardless of any
   LICENSE file — but only *through the Service*. That covers the fork's
   existence; it does not cover distributing binaries built from it.
2. **Keep the SDK question visible** rather than pretending it is settled. It
   is inherited from upstream and is the highest-risk element by the evidence
   above.
3. **Never ship keys or circumvention tooling**, and never bundle a disc image
   or its contents. That is the line the notice archive actually polices.
4. **Do not use Nintendo's trademarks as the project's identity** — not in the
   repository name, the icon, or the description.
