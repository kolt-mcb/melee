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

# 4. Its own network, shared with Caddy alone. On the common `gateway` bridge
#    every other tenant container could reach melee:8000 with no SSO (the gate
#    is Caddy's). Three edits: the melee service's networks line, a named
#    top-level network, and caddy attached to it. node1-apply.sh then connects
#    the RUNNING caddy to it (docker network connect), so no Caddy restart;
#    caddy's compose config has changed, so the next `up -d caddy` recreates
#    it once -- the user's call, when the gateway is quiet.
NET = "melee-net"
end = service_end()
for i in range(start, end):
    m = re.match(r"^(\s*)networks:\s*\[\s*gateway\s*\]\s*$", lines[i])
    if m:
        lines[i] = f"{m.group(1)}networks: [{NET}]\n"
        did.append(f"melee on its own network {NET} (line {i + 1})")
        break
text = "".join(lines)
if not re.search(rf"^  {re.escape(NET)}:\s*$", text, re.M):
    top = next((i for i, l in enumerate(lines) if l.startswith("networks:")), None)
    if top is None:
        print("no top-level `networks:` to add melee-net to; refusing to guess", file=sys.stderr)
        sys.exit(1)
    lines[top + 1:top + 1] = [
        f"  # melee's own bridge: only the melee service and caddy are on it\n",
        f"  # (deploy/web/compose-melee.block, install-compose-block.py).\n",
        f"  {NET}:\n",
        f"    name: {NET}\n",
        f"    driver: bridge\n",
    ]
    did.append(f"top-level network {NET} (line {top + 2})")
# caddy: add the network to its list, whichever form the list takes.
cstart = next((i for i, l in enumerate(lines) if re.match(r"^  caddy:\s*$", l)), None)
if cstart is None:
    print("no caddy service to attach to melee-net; refusing to guess", file=sys.stderr)
    sys.exit(1)
cend = next((i for i in range(cstart + 1, len(lines))
             if re.match(r"^  [a-z0-9_-]+:", lines[i]) or re.match(r"^[a-z]", lines[i])), len(lines))
cnet = next((i for i in range(cstart, cend) if re.match(r"^\s*networks:", lines[i])), None)
if cnet is None:
    print("the caddy service has no networks: line; refusing to guess", file=sys.stderr)
    sys.exit(1)
inline = re.match(r"^(\s*)networks:\s*\[(.*)\]\s*$", lines[cnet])
if inline:
    items = [s.strip() for s in inline.group(2).split(",") if s.strip()]
    if NET not in items:
        lines[cnet] = f"{inline.group(1)}networks: [{', '.join(items + [NET])}]\n"
        did.append(f"caddy attached to {NET} (line {cnet + 1})")
else:
    # Block form: the list items follow, each "- name", until the indent drops.
    base = len(re.match(r"^(\s*)", lines[cnet]).group(1))
    j = cnet + 1
    have = False
    while j < cend and (lines[j].strip() == "" or len(lines[j]) - len(lines[j].lstrip()) > base):
        if re.match(rf"^\s*-\s*{re.escape(NET)}\s*$", lines[j]):
            have = True
        j += 1
    if not have:
        last = next((k for k in range(j - 1, cnet, -1) if lines[k].strip().startswith("-")), None)
        if last is None:
            print("caddy's networks: list has no items to copy the indent from; refusing to guess", file=sys.stderr)
            sys.exit(1)
        indent = re.match(r"^(\s*)", lines[last]).group(1)
        lines[last + 1:last + 1] = [f"{indent}- {NET}\n"]
        did.append(f"caddy attached to {NET} (line {last + 2})")

if did:
    open(PATH, "w").write("".join(lines))
    print("updated the existing melee service: " + "; ".join(did))
else:
    print("already installed and up to date — no change")
