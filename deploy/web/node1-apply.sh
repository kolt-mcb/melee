#!/usr/bin/env bash
# Run ON grunt-node1, from anywhere (deploy.sh on grunt-openclaw copies it and
# the blocks to /tmp and runs it). Applies the melee deploy:
#   1. the `melee:` compose service: installs it, or adds the tools mount to
#      an older one (idempotent)
#   2. the Caddy vhost block: installs it, and ONLY THEN restarts Caddy. A
#      Caddy restart cuts every connection on every mcbridefarm.com subdomain,
#      so an already-installed vhost is never touched.
#   3. the container, which is LEFT ALONE when nothing of the process changed:
#      the page, the engine, the lobby and the server code are all mounted, and
#      the running process serves the new files on the next request. It is
#      recreated only when its compose config or image changed, and restarted
#      only when serve.js or relay.js changed -- and in either case not before
#      the lobby is empty (up to MELEE_DRAIN_SECS, default 900; 0 = at once).
#      A restart empties the lobby and ends every match in progress: this is
#      what "melee keeps going offline" was.
#   4. health-checks it over loopback
# Every step prints what it did; the whole thing is undone by the git revert
# in the change record.
set -euo pipefail

GW=/home/grunt/mcbridefarm-gateway
REMOTE=/home/grunt/melee-web
BLOCK=/tmp/caddy-melee.block
COMPOSE_BLOCK=/tmp/compose-melee.block
MARK='melee.mcbridefarm.com {'
DRAIN=${MELEE_DRAIN_SECS:-900}
# The serve.js/relay.js the running process started with.
STAMP=$REMOTE/.running-server.sha
SERVER_FILES="tools/wasm/serve.js tools/wasm/relay.js"

say() { echo "[apply] $*"; }

cd "$GW"
say "gateway repo: $(git rev-parse --short HEAD) ($(git status --porcelain | wc -l) files dirty)"

# 1. compose service
[ -s "$COMPOSE_BLOCK" ] || { echo "missing $COMPOSE_BLOCK" >&2; exit 1; }
say "compose: $(python3 /tmp/install-compose-block.py "$COMPOSE_BLOCK")"
docker compose config -q
say "compose: config valid"

# 2. caddy vhost
if grep -qF "$MARK" caddy/Caddyfile; then
  say "caddy: vhost already installed; not restarting caddy"
else
  python3 /tmp/install-caddy-block.py "$BLOCK"
  docker compose exec -T caddy caddy validate --config /etc/caddy/Caddyfile --adapter caddyfile 2>&1 |
    grep -v '^{"level"' | tail -2
  # `caddy reload` cannot work here: the admin API is not listening inside the
  # container, so applying a config means restarting the process (~2 s blip).
  say "caddy: new vhost -- restarting caddy once"
  docker compose restart caddy >/dev/null 2>&1
  sleep 4
  docker ps --filter name=caddy --format 'caddy: {{.Names}} {{.Status}}'
fi

# 3. container
melee_ip() { docker inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' melee 2>/dev/null || true; }
server_sha() { (cd "$REMOTE" && cat $SERVER_FILES | sha256sum | cut -c1-16); }

# Until nobody is in the lobby or a match. /healthz says who is there; an
# older serve.js answers "ok" and then there is nothing to wait on.
wait_empty() {
  [ "$DRAIN" -gt 0 ] || { say "melee: MELEE_DRAIN_SECS=0, not waiting for the lobby"; return; }
  local h n m until said=0
  until=$(( $(date +%s) + DRAIN ))
  while :; do
    h=$(curl -sS --max-time 3 "http://$(melee_ip):8000/healthz" 2>/dev/null || true)
    case "$h" in
      '{'*) read -r n m < <(printf '%s' "$h" | python3 -c \
              'import json,sys; d=json.load(sys.stdin); print(d.get("members", 0), "yes" if d.get("match") else "no")') ;;
      *) say "melee: the running process gives no lobby count (${h:-no answer}); not waiting"; return ;;
    esac
    if [ "$n" = 0 ]; then say "melee: lobby empty"; return; fi
    if [ "$(date +%s)" -ge "$until" ]; then
      say "melee: still $n player(s) here after ${DRAIN}s -- restarting anyway; their match ends"
      return
    fi
    [ "$said" = 1 ] || say "melee: $n player(s) here (in a match: $m); waiting up to ${DRAIN}s for the lobby to empty (MELEE_DRAIN_SECS)"
    said=1
    sleep 10
  done
}

want=$(server_sha)
# Build only when the Dockerfile changed or there is no image. This host's
# builder does not cache, so every build is a new image id, and a new id
# means a recreate below -- the very thing this script is here to avoid.
DF_STAMP=$REMOTE/.built-dockerfile.sha
df=$(sha256sum "$REMOTE/Dockerfile" | cut -c1-16)
if ! docker image inspect melee-web:local >/dev/null 2>&1 || [ "$df" != "$(cat "$DF_STAMP" 2>/dev/null || true)" ]; then
  say "melee: building the image (Dockerfile changed)"
  docker compose build -q melee 2>&1 | grep -v 'configured to build using Bake' || true
  echo "$df" > "$DF_STAMP"
else
  say "melee: image up to date (Dockerfile unchanged)"
fi
why=""
if [ "$(docker inspect -f '{{.State.Running}}' melee 2>/dev/null || true)" != true ]; then
  why="not running"
elif [ "$(docker compose config --hash melee | awk '{print $2}')" != \
       "$(docker inspect -f '{{index .Config.Labels "com.docker.compose.config-hash"}}' melee)" ]; then
  why="compose config changed"
elif [ "$(docker image inspect -f '{{.Id}}' melee-web:local)" != "$(docker inspect -f '{{.Image}}' melee)" ]; then
  why="image changed"
elif [ "$want" != "$(cat "$STAMP" 2>/dev/null || true)" ]; then
  why="serve.js/relay.js changed"
fi
if [ -z "$why" ]; then
  say "melee: nothing to restart -- the files are mounted; the running relay serves the new ones"
else
  say "melee: restart needed ($why)"
  wait_empty
  if [ "$why" = "serve.js/relay.js changed" ]; then
    docker compose restart melee
  else
    docker compose up -d melee
  fi
  echo "$want" > "$STAMP"
  sleep 4
fi
docker ps --filter name=^/melee$ --format 'melee: {{.Names}} {{.Status}}'

# 4. health, from node1 into the container's bridge IP (the melee container
# publishes no port on purpose -- only Caddy, on the same bridge, talks to it)
IP=$(melee_ip)
say "health (node1 -> $IP):"
curl -sS -w '  healthz  %{http_code} ' "http://$IP:8000/healthz"; echo
curl -sS -o /dev/null -w '  page     %{http_code} %{size_download}B\n' "http://$IP:8000/"
curl -sS -o /dev/null -w '  lobby.js %{http_code} %{size_download}B\n' "http://$IP:8000/lobby.js"
curl -sS -o /dev/null -r 0-1023 -w '  disc     %{http_code} %{size_download}B\n' "http://$IP:8000/disc.iso"
curl -sS -o /dev/null -w '  wasm     %{http_code} %{size_download}B\n' "http://$IP:8000/melee.wasm"
