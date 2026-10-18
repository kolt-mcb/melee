# melee-web: the browser build's page + disc streaming + the /net netplay relay.
# node tools/wasm/serve.js is one process for all three (no framework, no deps).
# The web build (melee.{html,js,wasm}) and the disc are MOUNTED, never baked in:
# redeploying a new build is a file copy + `docker compose restart melee`, no
# image rebuild.
FROM node:22-alpine
WORKDIR /app
COPY tools/wasm/ /app/tools/wasm/
# Players' crash and frame-rate reports land in /reports: mount a volume
# there to keep them past a restart.
RUN mkdir -p /reports && chown node:node /reports
EXPOSE 8000
USER node
CMD ["node", "tools/wasm/serve.js", "--dir", "/web", "--iso", "/disc/disc.iso", "--http", "--port", "8000", "--reports", "/reports"]
