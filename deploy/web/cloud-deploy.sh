#!/usr/bin/env bash
# Deploy the melee web build to the cloud host (GCE game-server-vm), which has
# served melee.mcbridefarm.com since 2026-11-03. Run from a checkout with a
# built build/wasm and a gcloud login for the project; nothing goes through
# node1 or openclaw.
#
#   deploy/web/cloud-deploy.sh            copy what changed, then apply
#   deploy/web/cloud-deploy.sh --dry-run  list what would be copied; change nothing
#   deploy/web/cloud-deploy.sh --check    copy nothing, just the health checks
#
# The host's port 22 is open only to Google's IAP range (the home WAN address
# is dynamic), so every ssh and rsync goes through `gcloud compute
# start-iap-tunnel` with gcloud's own key and known-hosts file.
#
# Like node1's deploy, the page, engine, lobby and server code are all MOUNTED
# into the container, so a deploy is a file copy that the running process picks
# up on the next request. The remote half (cloud-apply.sh) rebuilds the image
# only when the Dockerfile changed, and restarts the process only when the
# server code changed -- not before the lobby is empty (MELEE_DRAIN_SECS,
# default 900; 0 = at once). A new melee.wasm makes every open page reload, so
# ship one only when the engine really changed.
set -euo pipefail

SRC=$(cd "$(dirname "$0")/../.." && pwd)
VM=${MELEE_CLOUD_VM:-game-server-vm}
ZONE=${MELEE_CLOUD_ZONE:-us-west1-b}
USER_AT=${MELEE_CLOUD_USER:-kolt}
REMOTE=/opt/melee/melee-web

say() { echo "[cloud-deploy] $*"; }

ID=$(gcloud compute instances describe "$VM" --zone "$ZONE" --format='value(id)')
SSH_CMD="ssh -o BatchMode=yes -o ConnectTimeout=20 -i $HOME/.ssh/google_compute_engine \
 -o UserKnownHostsFile=$HOME/.ssh/google_compute_known_hosts -o HostKeyAlias=compute.$ID \
 -o ProxyCommand='gcloud compute start-iap-tunnel $VM 22 --listen-on-stdin --zone $ZONE --verbosity=error'"
ssh_vm() { eval "$SSH_CMD" "$USER_AT@$VM" "$@"; }

case "${1:-}" in
  --check) ssh_vm "sudo bash /opt/melee/cloud-apply.sh --check"; exit ;;
  --dry-run) DRY=-n ;;
  "") DRY= ;;
  *) echo "usage: $0 [--dry-run|--check]" >&2; exit 2 ;;
esac

[ -f "$SRC/build/wasm/melee.wasm" ] || { echo "no build at $SRC/build/wasm -- build with WASM_LINK=1 first" >&2; exit 1; }
grep -q 'src="lobby.js"' "$SRC/build/wasm/melee.html" || {
  echo "melee.html does not load /lobby.js -- rebuild the web target" >&2; exit 1; }
grep -q 'disc.iso' "$SRC/tools/wasm/lobby.js" || {
  echo "$SRC/tools/wasm/lobby.js is not the lobby (no disc.iso)" >&2; exit 1; }
ssh_vm "command -v rsync >/dev/null" || { echo "no rsync on $VM (sudo apt-get install -y rsync)" >&2; exit 1; }
ssh_vm "test -s $REMOTE/disc.iso" || { echo "no disc at $VM:$REMOTE/disc.iso" >&2; exit 1; }

# -c: decide by content, not mtime. The cloud copy was made by hand on
# 2026-11-03 and its mtimes say nothing about what the files hold.
# Prints only what changed; any failure stops the deploy.
RSYNC() {
  local out
  if ! out=$(rsync -rlpc $DRY --itemize-changes -e "$SSH_CMD" "$@" 2>&1); then
    printf '%s\n' "$out" >&2
    echo "[cloud-deploy] rsync failed; stopping" >&2
    exit 1
  fi
  printf '%s\n' "$out" | grep -v '^\.' || true
}

# The apply script goes first: --seed must run before the server code moves.
RSYNC "$SRC/deploy/web/cloud-apply.sh" "$USER_AT@$VM:/opt/melee/cloud-apply.sh"
if [ -z "$DRY" ]; then
  ssh_vm "sudo bash /opt/melee/cloud-apply.sh --seed"
  # Keep the page being replaced: a rollback is two copies, not a rebuild.
  ssh_vm "cd $REMOTE/web && for f in melee.html lobby.js; do [ -f \$f ] && cp -p \$f \$f.orig; done; true"
fi
say "${DRY:+(dry run) }web build"
RSYNC "$SRC/build/wasm/melee.html" "$SRC/build/wasm/melee.js" "$SRC/build/wasm/melee.wasm" "$USER_AT@$VM:$REMOTE/web/"
# The page asks for /lobby.js beside melee.html, not out of tools/.
RSYNC "$SRC/tools/wasm/lobby.js" "$USER_AT@$VM:$REMOTE/web/lobby.js"
# Portraits and stage tiles come from the disc and are not in git; the page
# reads fine without them, so their absence here leaves the host's copy alone.
for d in css stages; do
  if [ -d "$SRC/build/wasm/$d" ]; then
    RSYNC --delete "$SRC/build/wasm/$d/" "$USER_AT@$VM:$REMOTE/web/$d/"
  else
    say "no build/wasm/$d here: leaving the host's copy as it is"
  fi
done
say "${DRY:+(dry run) }server code"
RSYNC --delete --exclude '__pycache__/' --exclude '*.bak*' --exclude '*.orig' \
  "$SRC/tools/wasm/" "$USER_AT@$VM:$REMOTE/tools/wasm/"
RSYNC "$SRC/deploy/web/Dockerfile" "$SRC/deploy/web/.dockerignore" "$USER_AT@$VM:$REMOTE/"

if [ -n "$DRY" ]; then
  say "dry run: nothing copied, nothing applied"
  exit 0
fi
say "applying on $VM"
ssh_vm "sudo MELEE_DRAIN_SECS=${MELEE_DRAIN_SECS:-900} bash /opt/melee/cloud-apply.sh"
