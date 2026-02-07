# Game Server

A real-time multiplayer, multitenant WebSocket game server

## Overview
This server handles real-time player interactions for
- **[Thoughts](../thoughts)**, a pretty chill 3D vibe you can play [here](https://muchq.com/thoughts)
- **[Golf](golf/)**, a 4-card golf game with room-based multi-game architecture
- Some other games someday

### Core Components
- **Hub** (`hub.go`): Central message broker that manages all active clients and game state
- **Main** (`main.go`): HTTP server setup and WebSocket upgrade handling
- **Games**: individual games can implement the Hub interface and be registered on a dedicated path in main

## Running the Server

### Build
```bash
bazel build //domains/games/apis/games_ws_backend
```

### Development Mode
```bash
DEV_MODE=1 bazel run //domains/games/apis/games_ws_backend
```

### Deploy
This is now deployed as part of the consolidated deploy in
```bash
deploy/consolidated/deploy.sh
```

### Configuration

- `DEV_MODE`: Enables debug logging and allows all WebSocket origins
- `-addr`: Server address (default: `:8080`)

## Security

- **Origin Validation**: Only allows connections from `thoughts.muchq.com`, `muchq.com`, and `www.muchq.com` in production
- **HTTPS Enforcement**: Rejects non-HTTPS origins in production

## Game Implementations

### Golf (`/games/v1/golf-ws`)
4-card golf game with advanced room-based multi-game architecture:
- **Multiple concurrent games** within the same room
- **Room-based player management** with persistent statistics
- **Chat-ready architecture** - players can join rooms without joining games
- **Game isolation** - complete separation between concurrent games
- **Automatic cleanup** of completed games

See [golf/README.md](golf/README.md) for detailed documentation.

### Future Enhancements to Consider
- [ ] **Chat**: Room-based chat system (architecture ready in golf)
- [ ] **Rate Limiting**: Implement per-player rate limiting for actions
- [ ] **Persistence**: Add database support for player progress and game history
- [ ] **Metrics**: Implement Prometheus metrics for monitoring
- [ ] **Anti-Cheat**: Server-side physics validation and anomaly detection
- [ ] **Replay System**: Record game sessions for debugging and spectating

