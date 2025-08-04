# Game Server

A real-time multiplayer, multitenant WebSocket game server

## Overview
This server handles real-time player interactions for
- [Thoughts](../thoughts), a pretty chill 3D vibe you can play [here](https://muchq.com/thoughts)
- Some other games someday

### Core Components
- **Hub** (`hub.go`): Central message broker that manages all active clients and game state
- **Main** (`main.go`): HTTP server setup and WebSocket upgrade handling
- **Games**: individual games can implement the Hub interface and be registered on a dedicated path in main

## Running the Server

### Build
```bash
bazel build //go/backend_ws_server
```

### Development Mode
```bash
DEV_MODE=1 bazel-bin/go/games_ws_backend/games_ws_backend_/games_ws_backend
```

### Deploy
```bash
go/games_ws_backend/deploy/deploy.sh
```

### Configuration

- `DEV_MODE`: Enables debug logging and allows all WebSocket origins
- `-addr`: Server address (default: `:8080`)

## Security

- **Origin Validation**: Only allows connections from `thoughts.muchq.com`, `muchq.com`, and `www.muchq.com` in production
- **HTTPS Enforcement**: Rejects non-HTTPS origins in production

### Future Enhancements to Consider
- [ ] **Chat**: Is this worth doing?
- [ ] **Rate Limiting**: Implement per-player rate limiting for actions
- [ ] **Persistence**: Add database support for player progress and game history
- [ ] **Metrics**: Implement Prometheus metrics for monitoring
- [ ] **Anti-Cheat**: Server-side physics validation and anomaly detection
- [ ] **Replay System**: Record game sessions for debugging and spectating

