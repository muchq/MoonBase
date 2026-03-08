# Thoughts

A chill 3D multiplayer vibe, playable at [muchq.com/thoughts](https://muchq.com/thoughts).

## Overview

Players connect via WebSocket and share position, shape, and color updates in real time. The server broadcasts each player's state to all other connected clients.

## Protocol

```json
{
  "type": "player_join|position_update|shape_update|player_leave|game_state",
  "playerId": "player-abc123",
  "position": [x, y, z],
  "color": [r, g, b],
  "shape": 0,
  "timestamp": 1234567890
}
```

## Code Layout

| File | Description |
|------|-------------|
| `thoughts_hub.go` | Hub event loop: client registration, message routing, state broadcast |
| `game.go` | Player struct and message types |

## Development

```bash
bazel test //domains/games/apis/games_ws_backend/thoughts:thoughts_test
```
