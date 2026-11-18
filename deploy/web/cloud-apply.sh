#!/usr/bin/env bash
# Run ON the cloud host, as root (cloud-deploy.sh copies it to /opt/melee and
# runs it with sudo). The cloud half of a deploy, after the files are copied:
#   1. the image: rebuilt only when the Dockerfile changed or there is none
#   2. the container, LEFT ALONE when nothing of the process changed: the page,
#      engine, lobby and server code are mounted and served fresh on the next
#      request. Recreated when its compose config or image changed, restarted
#      when the server code changed -- and in either case not before the lobby
#      is empty (up to MELEE_DRAIN_SECS, default 900; 0 = at once). A restart
#      ends every match in progress.
#   3. health over the container's bridge address
# The compose file, Caddy and the sign-in services here are hand-managed and
# never touched by this script.
#
#   cloud-apply.sh           apply
#   cloud-apply.sh --check   step 3 only
#   cloud-apply.sh --seed    before the copy: if nothing records what the
#                            running process started with, record the server
#                            code on disk now, which is what it is running
set -euo pipefail

STACK=/opt/melee
REMOTE=$STACK/melee-web
DRAIN=${MELEE_DRAIN_SECS:-900}
# The server files the running process started with. A file the process
# require()s belongs here: a change to it is a restart.
STAMP=$REMOTE/.running-server.sha
SERVER_FILES="tools/wasm/serve.js tools/wasm/relay.js tools/wasm/rtc.js tools/wasm/profiles.js"

say() { echo "[apply] $*"; }
cd "$STACK"

melee_ip() { docker inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' melee 2>/dev/null || true; }

health() {
  local ip
  ip=$(melee_ip)
  say "health ($(hostname) -> $ip):"
  curl -sS -w '  healthz  %{http_code} ' "http://$ip:8000/healthz"; echo
  curl -sS -o /dev/null -w '  page     %{http_code} %{size_download}B\n' "http://$ip:8000/"
  curl -sS -o /dev/null -w '  lobby.js %{http_code} %{size_download}B\n' "http://$ip:8000/lobby.js"
  curl -sS -o /dev/null -r 0-1023 -w '  disc     %{http_code} %{size_download}B\n' "http://$ip:8000/disc.iso"
  curl -sS -o /dev/null -w '  wasm     %{http_code} %{size_download}B\n' "http://$ip:8000/melee.wasm"
  printf '  wasm on disk %sB\n' "$(stat -c %s "$REMOTE/web/melee.wasm")"
  docker ps --filter name=^/melee$ --format '  container {{.Status}}'
}

# The files that exist: profiles.js is newer than the first cloud copy.
server_sha() { (cd "$REMOTE" && for f in $SERVER_FILES; do [ -f "$f" ] && cat "$f"; done | sha256sum | cut -c1-16); }

case "${1:-}" in
  --check) health; exit 0 ;;
  --seed)
    if [ ! -f "$STAMP" ] && [ "$(docker inspect -f '{{.State.Running}}' melee 2>/dev/null || true)" = true ]; then
      server_sha > "$STAMP"
      say "melee: recorded the running server code ($(cat "$STAMP"))"
    fi
    exit 0 ;;
esac

# Until nobody is in the lobby or a match. /healthz says who is there.
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

# 1. image
DF_STAMP=$REMOTE/.built-dockerfile.sha
df=$(sha256sum "$REMOTE/Dockerfile" | cut -c1-16)
if [ ! -f "$DF_STAMP" ] && docker image inspect melee-web:local >/dev/null 2>&1; then
  # First run of this script: the image was built by hand from this same
  # Dockerfile. Adopt it instead of building a new id, which would recreate
  # the container for nothing.
  echo "$df" > "$DF_STAMP"
  say "melee: adopted the existing image (first run)"
fi
if ! docker image inspect melee-web:local >/dev/null 2>&1 || [ "$df" != "$(cat "$DF_STAMP")" ]; then
  say "melee: building the image (Dockerfile changed)"
  docker compose build -q melee
  echo "$df" > "$DF_STAMP"
else
  say "melee: image up to date (Dockerfile unchanged)"
fi

# 2. container
want=$(server_sha)
why=""
if [ "$(docker inspect -f '{{.State.Running}}' melee 2>/dev/null || true)" != true ]; then
  why="not running"
elif [ "$(docker compose config --hash melee | awk '{print $2}')" != \
       "$(docker inspect -f '{{index .Config.Labels "com.docker.compose.config-hash"}}' melee)" ]; then
  why="compose config changed"
elif [ "$(docker image inspect -f '{{.Id}}' melee-web:local)" != "$(docker inspect -f '{{.Image}}' melee)" ]; then
  why="image changed"
elif [ "$want" != "$(cat "$STAMP" 2>/dev/null || true)" ]; then
  why="server code changed"
fi
if [ -z "$why" ]; then
  say "melee: nothing to restart -- the files are mounted; the running relay serves the new ones"
else
  say "melee: restart needed ($why)"
  wait_empty
  if [ "$why" = "server code changed" ]; then
    docker compose restart melee
  else
    docker compose up -d melee
  fi
  echo "$want" > "$STAMP"
  sleep 4
fi

# 3. health
health
