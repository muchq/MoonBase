# Local Development Setup

## Overview
Run services directly on your machine and use Caddy to proxy requests with CORS for localhost.

## Quick Start

Use the provided script to run all services and Caddy in one command:

```bash
./deploy/consolidated/local.sh
```

This will start:
- games_ws_backend on port 8080
- portrait on port 8081
- mithril on port 8083
- posterize on port 8084
- Caddy proxy on port 2015

Then run the local UI:
```bash
cd /path/to/muchq.github.io
npm run local-server
```

## Manual Setup

Alternatively, run services individually:

```bash
# Terminal 1: run posterize
PORT=8084 bazel run //rust/posterize

# Terminal 2: run portrait
PORT=8081 bazel run //cpp/portrait

# ... other services as desired

# Terminal N: run Caddy
caddy run --config deploy/consolidated/Caddyfile.local
```
