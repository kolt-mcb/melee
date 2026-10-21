#!/usr/bin/env python3
"""Insert the melee vhost block into the gateway Caddyfile, before the wildcard.

Run ON grunt-node1, from the repo root:

    python3 /tmp/install-caddy-block.py /tmp/caddy-melee.block

Idempotent: refuses to insert twice (greps for the exact-host marker). Writes
nothing if the marker is already there or the wildcard anchor is missing.
Undo: `git checkout -- caddy/Caddyfile` + `caddy reload`.
"""
import sys

BLOCK = sys.argv[1] if len(sys.argv) > 1 else "/tmp/caddy-melee.block"
PATH = "caddy/Caddyfile"
MARK = "melee.mcbridefarm.com {"
ANCHOR = "*.mcbridefarm.com {"

block = open(BLOCK).read()
txt = open(PATH).read()

if MARK in txt:
    print("already installed — no change")
    sys.exit(0)
if ANCHOR not in txt:
    print(f"anchor {ANCHOR!r} not found; refusing to guess where it goes", file=sys.stderr)
    sys.exit(1)

i = txt.index(ANCHOR)
open(PATH, "w").write(txt[:i] + block.rstrip("\n") + "\n\n" + txt[i:])
print(f"inserted {len(block.splitlines())} lines at line {txt[:i].count(chr(10)) + 1}, before the wildcard")
