#!/usr/bin/env python3
"""Put the melee compose service into the gateway docker-compose.yml.

Run ON grunt-node1, from the repo root:

    python3 /tmp/install-compose-block.py /tmp/compose-melee.block

The service has to go INSIDE the `services:` mapping — appending it to the end
of the file lands it under `secrets:` and compose rejects it. So: insert before
the first top-level key after `services:`. Idempotent.

A service that is already there is not replaced (its comments and any hand
edits stay), but it is brought up to date with the one mount a newer block
needs: the server code, tools/wasm, so that a deploy no longer rebuilds the
image and recreates the container. Undo: `git checkout -- docker-compose.yml`.
"""
import re
import sys

BLOCK = sys.argv[1] if len(sys.argv) > 1 else "/tmp/compose-melee.block"
PATH = "docker-compose.yml"
TOOLS_MOUNT = "/home/grunt/melee-web/tools/wasm:/app/tools/wasm:ro"
DISC_MOUNT = "/disc/disc.iso:ro"

block = open(BLOCK).read().rstrip("\n") + "\n"
lines = open(PATH).read().splitlines(keepends=True)
text = "".join(lines)

if not re.search(r"^  melee:\s*$", text, re.M):
    try:
        svc = next(i for i, l in enumerate(lines) if l.startswith("services:"))
    except StopIteration:
        print("no `services:` key; refusing to guess", file=sys.stderr)
        sys.exit(1)
    at = next((i for i in range(svc + 1, len(lines)) if re.match(r"^[a-z0-9_-]+:", lines[i])), len(lines))
    lines.insert(at, "\n" + block)
    open(PATH, "w").write("".join(lines))
    print(f"inserted the melee service at line {at + 1} (inside services:, before line {at + 1} top-level key)")
    sys.exit(0)

if TOOLS_MOUNT in text:
    print("already installed, with the tools mount — no change")
    sys.exit(0)

# The service is there without the mount: add it right after the disc mount,
# which every version of the block has had.
start = next(i for i, l in enumerate(lines) if re.match(r"^  melee:\s*$", l))
end = next((i for i in range(start + 1, len(lines)) if re.match(r"^  [a-z0-9_-]+:", lines[i]) or re.match(r"^[a-z]", lines[i])), len(lines))
disc = next((i for i in range(start, end) if DISC_MOUNT in lines[i]), None)
if disc is None:
    print("the melee service has no disc mount to anchor on; refusing to guess", file=sys.stderr)
    sys.exit(1)
indent = re.match(r"^(\s*)-", lines[disc]).group(1)
lines[disc + 1:disc + 1] = [
    f"{indent}# The server code too, so a deploy is a copy and not a new container:\n",
    f"{indent}# recreating it empties the lobby and ends every match in progress.\n",
    f"{indent}- {TOOLS_MOUNT}\n",
]
open(PATH, "w").write("".join(lines))
print(f"added the tools/wasm mount to the existing melee service (line {disc + 2})")
