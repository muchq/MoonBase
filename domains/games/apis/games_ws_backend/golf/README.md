# Golf Card Game

WebSocket backend for 4-card golf with rooms, JWT authentication, and session reconnection.

## Game Rules

- Each player receives 4 face-down cards in a 2x2 grid
- Players peek at exactly 2 cards at the start
- On each turn: draw from deck or take from discard, then swap with one of your cards or discard
- A player can knock to trigger the final round
- Lowest total score wins

### Card Values

| Card | Value |
|------|-------|
| A    | 1     |
| 2-10 | Face value |
| J    | 0     |
| Q, K | 10    |

## Architecture

### Room-Based Multi-Game System

```
Room (6-char ID, e.g. "ABC123")
├── Players[] — persistent across games, track wins/scores
├── Games{}   — concurrent game instances
│   ├── GAME1 (players A, B playing)
│   └── GAME2 (players C, D playing)
└── GameHistory[] — completed game results
```

- **Rooms** are persistent containers with player membership and cumulative stats
- **Games** are isolated match instances within rooms (2-4 players each)
- Players can be in a room without being in a specific game

### Authentication and Reconnection

Players authenticate via JWT on every WebSocket connection:

1. Client connects and sends `authenticate` with a stored session token (or empty for new session)
2. Server validates the token and either restores the existing session or creates a new one
3. Server responds with `authenticated` containing a session token for the client to store
4. If reconnecting, server automatically restores the player to their room and game

Disconnected players have a **5-minute grace period** during which their session is preserved. If they reconnect within that window, they resume with the same player ID and game state. After the grace period, the session is cleaned up and the player is removed from their game/room.

Tokens use HMAC-SHA256 with a random secret generated at server startup. The signing method is strictly validated to prevent algorithm confusion attacks.

**Limitation:** The JWT secret is generated randomly on each server start, so all existing session tokens are invalidated on restart or deploy. Players will need to re-authenticate as new sessions. A future improvement is to load the secret from a file or environment variable so tokens survive restarts.

### Concurrency Model

`GolfHub` runs a single-goroutine event loop processing channels for register, unregister, game messages, and session cleanup. Game instances (`Game`) use mutex-based locking for state access. This means the hub never blocks on game operations, and games are safe to access from broadcast goroutines.

## Message Protocol

### Connection Lifecycle

```
Client                          Server
  |--- WebSocket connect -------->|
  |--- authenticate ------------->|  (with sessionToken or empty)
  |<-- authenticated -------------|  (sessionToken, playerId, reconnected)
  |--- createGame / joinGame ---->|  (room + game operations)
  |<-- roomJoined ----------------|  (playerId, roomState)
  |--- startGame ---------------->|
  |<-- gameStarted ---------------|
  |<-- gameState -----------------|  (personalized per player)
  |    ... game play ...          |
  |<-- gameEnded -----------------|  (winner, finalScores)
```

### Client to Server

| Message | Fields | Description |
|---------|--------|-------------|
| `authenticate` | `sessionToken?` | First message after connect; empty token for new session |
| `createGame` | | Create a new room with the player in it |
| `joinGame` | `roomId`, `gameId` | Join a specific game in a room (creates game if needed) |
| `startGame` | | Start the current game (requires 2+ players) |
| `peekCard` | `cardIndex` | Peek at one of your cards (during peeking phase) |
| `drawCard` | | Draw from deck |
| `takeFromDiscard` | | Take the top discard pile card |
| `swapCard` | `cardIndex` | Swap drawn card with one of yours |
| `discardDrawn` | | Discard the drawn card |
| `knock` | | Signal final round |
| `hideCards` | | Hide peeked cards |
| `startNewGame` | | Start a new game in the current room |

### Server to Client

| Message | Fields | Description |
|---------|--------|-------------|
| `authenticated` | `sessionToken`, `playerId`, `reconnected` | Auth confirmation |
| `roomJoined` | `playerId`, `roomState` | Joined room confirmation |
| `roomStateUpdate` | `roomState` | Room state changed |
| `gameJoined` | `playerId`, `gameState` | Joined game confirmation |
| `gameState` | `gameState` | Game state update (cards personalized per player) |
| `gameStarted` | | Game has begun |
| `turnChanged` | `playerName` | Turn passed to next player |
| `playerKnocked` | `playerName` | A player knocked |
| `gameEnded` | `winner`, `finalScores` | Game over |
| `newGameStarted` | `gameId`, `previousGameId?` | New game created in room |
| `error` | `message` | Error message |

## Code Layout

| File | Description |
|------|-------------|
| `golf_hub.go` | Hub event loop: register/unregister, message routing, auth, reconnection, session cleanup |
| `game.go` | Game instance: state machine, turn logic, scoring, player management |
| `auth.go` | JWT token creation and validation (HMAC-SHA256 via `golang-jwt/jwt/v5`) |
| `types.go` | All message types, game state structs, card utilities |
| `state_validation.go` | Game phase transition validators |

## Development

```bash
# Run all tests
bazel test //domains/games/apis/games_ws_backend/golf:golf_test

# Run specific tests
bazel test //domains/games/apis/games_ws_backend/golf:golf_test --test_filter="TestHub_Auth"
bazel test //domains/games/apis/games_ws_backend/golf:golf_test --test_filter="TestIntegration"
```

See [EXAMPLE_GOLF.md](EXAMPLE_GOLF.md) for a complete annotated message flow walkthrough.
