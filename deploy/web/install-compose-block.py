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

# The service is there. Bring it up to date, each step idempotent, keeping its
# comments and hand edits (node1 sets MELEE_RTC and MELEE_RTC_HOST by hand).
start = next(i for i, l in enumerate(lines) if re.match(r"^  melee:\s*$", l))
def service_end():
    return next((i for i in range(start + 1, len(lines))
                 if re.match(r"^  [a-z0-9_-]+:", lines[i]) or re.match(r"^[a-z]", lines[i])), len(lines))
did = []

# 1. The server-code mount, right after the disc mount, which every version
#    of the block has had.
if TOOLS_MOUNT not in text:
    end = service_end()
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
    did.append(f"tools/wasm mount (line {disc + 2})")

# 2. The UDP publish bound to one host address (MELEE_RTC_BIND, from .env),
#    not 0.0.0.0: a bare "9400:9400/udp" listens on every host interface.
end = service_end()
for i in range(start, end):
    m = re.match(r'^(\s*)-\s*["\']?(\d+):(\d+)/udp["\']?\s*$', lines[i])
    if m and m.group(2) == "9400":
        lines[i] = f'{m.group(1)}- "${{MELEE_RTC_BIND:-127.0.0.1}}:{m.group(2)}:{m.group(3)}/udp"\n'
        did.append(f"UDP publish bound to MELEE_RTC_BIND (line {i + 1})")
        break

# 3. MELEE_STUN in the environment (tools/wasm/rtc.js): the pages' STUN
#    server, so a remote player's channel opens without an inbound forward.
end = service_end()
if not any(re.match(r"^\s*MELEE_STUN:", lines[i]) for i in range(start, end)):
    env = next((i for i in range(start, end) if re.match(r"^\s*environment:\s*$", lines[i])), None)
    if env is None:
        print("the melee service has no environment: to add MELEE_STUN to; refusing to guess", file=sys.stderr)
        sys.exit(1)
    anchor = next((i for i in range(env + 1, end) if re.match(r"^\s*MELEE_RTC_HOST:", lines[i])), None)
    if anchor is None:
        anchor = env
        indent = re.match(r"^(\s*)", lines[env]).group(1) + "  "
    else:
        indent = re.match(r"^(\s*)", lines[anchor]).group(1)
    lines[anchor + 1:anchor + 1] = [
        f"{indent}# A STUN server for the players' browsers (rtc.js MELEE_STUN), set in\n",
        f"{indent}# .env beside MELEE_RTC_BIND: with it, a remote player's channel opens\n",
        f"{indent}# through the router's own outbound state, no inbound port forward.\n",
        f'{indent}MELEE_STUN: "${{MELEE_STUN:-}}"\n',
    ]
    did.append(f"MELEE_STUN env (line {anchor + 2})")

if did:
    open(PATH, "w").write("".join(lines))
    print("updated the existing melee service: " + "; ".join(did))
else:
    print("already installed and up to date — no change")
