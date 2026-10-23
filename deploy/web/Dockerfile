# melee-web: the browser build's page + disc streaming + the /net netplay relay.
# node tools/wasm/serve.js is one process for all three (no framework, no deps).
#
# NOTHING of ours is baked in. The web build (melee.{html,js,wasm}), the disc
# AND the server code (tools/wasm/) are mounted -- see compose-melee.block. So
# this image never changes, `docker compose up -d` never has a reason to
# recreate the container, and a deploy is a file copy that the running
# process picks up on the next request (serve.js keys its caches by mtime).
# Only a change to serve.js or relay.js needs the process restarted, and
# node1-apply.sh does that when nobody is playing.
#
# (It used to COPY tools/wasm/ in. Every deploy then built a new image and
# recreated the container, which emptied the lobby and ended every match in
# progress -- 28 times in the six days to 2026-10-21.)
FROM node:22-alpine
WORKDIR /app
# The one dependency: node-datachannel (libdatachannel), the relay's end of
# each page's WebRTC datagram channel for game inputs (tools/wasm/rtc.js).
# Installed here, outside the mounted tools/wasm, as /app/node_modules, which
# require() finds from /app/tools/wasm. It ships a prebuilt linux-x64-musl
# binary, so nothing compiles. Without it the relay runs as before, inputs on
# the WebSocket only.
# --ignore-scripts: node-datachannel ships a prebuilt binary (its platform
# package is a plain optionalDependency, resolved at runtime by detect-libc),
# so no install script is needed, and skipping them removes a build-time
# code-execution surface. The require() below confirms the binary still loads.
RUN npm install --prefix /app --omit=dev --no-audit --no-fund --no-save --ignore-scripts node-datachannel@0.33.4 \
 && node -e "require('/app/node_modules/node-datachannel')"
# Players' crash and frame-rate reports land in /reports: mount a volume
# there to keep them past a restart.
RUN mkdir -p /reports /app/tools/wasm && chown node:node /reports
EXPOSE 8000
# The datagram channel's one UDP port (all pages share it: ICE UDP mux). It
# must be published with the same number outside, and MELEE_RTC_HOST set to
# the address players reach it on -- see compose-melee.block.
EXPOSE 9400/udp
# Off until the compose file turns it on (MELEE_RTC: "1") with the port
# published and MELEE_RTC_HOST set: unreachable, every page would spend 8 s
# offering a channel that never opens before keeping to the WebSocket.
ENV MELEE_RTC=0
USER node
CMD ["node", "tools/wasm/serve.js", "--dir", "/web", "--iso", "/disc/disc.iso", "--http", "--port", "8000", "--reports", "/reports"]
