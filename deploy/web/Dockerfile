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
# Players' crash and frame-rate reports land in /reports: mount a volume
# there to keep them past a restart.
RUN mkdir -p /reports /app/tools/wasm && chown node:node /reports
EXPOSE 8000
USER node
CMD ["node", "tools/wasm/serve.js", "--dir", "/web", "--iso", "/disc/disc.iso", "--http", "--port", "8000", "--reports", "/reports"]
