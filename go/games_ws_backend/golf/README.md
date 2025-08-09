# Golf Card Game WebSocket Backend

This package implements the WebSocket backend for the 4-card Golf card game. It follows the API contract defined in the frontend's GOLF.md documentation.

## Features

- Multi-player support (2-4 players)
- Real-time game state synchronization
- Room-based game sessions with 6-character codes
- Full implementation of 6-card golf rules
- Thread-safe game state management

## Game Rules

- Each player receives 4 cards in a 2x2 grid
- Players can peek at exactly 2 cards at the start
- On each turn, players can:
  - Draw from deck or take from discard pile
  - Swap with one of their cards or discard the drawn card
- Players can knock to trigger the final round
- Lowest total score wins

## WebSocket Endpoints

The golf game is served at `/golf-ws` on the games backend server.

## Message Types

### Client to Server:
- `createGame` - Create a new game room
- `joinGame` - Join existing game with room code
- `startGame` - Start the game (requires 2+ players)
- `peekCard` - Peek at one of your cards
- `drawCard` - Draw from deck
- `takeFromDiscard` - Take top discard card
- `swapCard` - Swap drawn card with your card
- `discardDrawn` - Discard the drawn card
- `knock` - Signal final round

### Server to Client:
- `gameJoined` - Confirmation with player ID and game state
- `gameState` - Full game state update
- `error` - Error message
- `gameStarted` - Game has begun
- `turnChanged` - Turn changed to new player
- `playerKnocked` - Player has knocked
- `gameEnded` - Game over with winner and scores

## Development

Run tests:
```bash
bazel test //go/games_ws_backend/golf:golf_test
```

Build:
```bash
bazel build //go/games_ws_backend:games_ws_backend
```