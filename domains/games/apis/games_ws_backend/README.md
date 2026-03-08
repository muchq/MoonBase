# Game Server

A real-time multiplayer, multitenant WebSocket game server.

## Overview

This server hosts multiple game backends on a single process, each on its own WebSocket endpoint:

- **[Golf](golf/)** (`/games/v1/golf-ws`) — a 4-card golf card game with rooms, JWT authentication, and session reconnection
- **[Thoughts](thoughts/)** (`/games/v1/thoughts-ws`) — a chill 3D multiplayer vibe, playable at [muchq.com/thoughts](https://muchq.com/thoughts)

## Architecture

```
main.go
├── hub/             # Shared WebSocket hub: client lifecycle, ping/pong, origin checks
├── players/         # Player ID generators (whimsical for prod, deterministic for tests)
├── golf/            # Golf game hub + game logic + JWT auth
└── thoughts/        # Thoughts game hub + game logic
```

Each game implements the `hub.Hub` interface and is registered on a dedicated HTTP path in `main.go`. The shared `hub` package handles WebSocket upgrades, client read/write pumps, and origin validation.

### Hub Interface

```go
type Hub interface {
    Register(c *Client)
    Unregister(c *Client)
    GameMessage(data GameMessageData)
    Run()
}
```

Games receive raw messages via `GameMessage`, manage their own state, and send responses through `client.Send` channels.

### Client Identity

Each WebSocket connection gets a UUID (`hub.Client.ID`) assigned at upgrade time. Games build their own identity layer on top — golf uses JWT-based player sessions that persist across reconnections.

## Running

```bash
# Development (debug logging, all origins allowed)
DEV_MODE=1 bazel run //domains/games/apis/games_ws_backend

# Build
bazel build //domains/games/apis/games_ws_backend

# Test everything
bazel test //domains/games/apis/games_ws_backend/...
```

### Configuration

| Variable   | Description |
|------------|-------------|
| `DEV_MODE` | Enables debug logging and allows all WebSocket origins |
| `-addr`    | Server listen address (default `:8080`) |

## Deployment

Deployed via the consolidated deploy script:

```bash
deploy/consolidated/deploy.sh
```

## Security

- **Origin validation**: Production only allows `muchq.com`, `www.muchq.com`, and `thoughts.muchq.com` over HTTPS
- **JWT authentication** (golf): HMAC-SHA256 tokens with algorithm validation to prevent confusion attacks
- **Server-assigned IDs**: Player IDs are generated server-side, never accepted from clients
