# Golf Card Game WebSocket Backend

This package implements the WebSocket backend for the 4-card Golf card game with a **room-based multi-game architecture**.

## Features

- **Multi-Game Rooms**: Multiple concurrent games within the same room
- **Room-Based Chat Ready**: Players can join rooms without joining games (perfect for future chat features)
- **Game Isolation**: Complete isolation between concurrent games
- **Flexible Player Movement**: Players can join different games within the same room
- **Automatic Game Cleanup**: Completed games are automatically removed after stats collection
- **Multi-player support** (2-4 players per game)
- **Real-time game state synchronization**
- **Persistent room statistics** tracking across multiple games
- **Thread-safe game state management**
- **Full implementation of 4-card golf rules**

## Game Rules

- Each player receives 4 cards in a 2x2 grid
- Players can peek at exactly 2 cards at the start
- On each turn, players can:
  - Draw from deck or take from discard pile
  - Swap with one of their cards or discard the drawn card
- Players can knock to trigger the final round
- Lowest total score wins

## Architecture Overview

### Room-Based Multi-Game System

```
Room (6-character ID)
├── Players[] (persistent across games)
├── Games{} (multiple concurrent games)
│   ├── GAME1 (players A, B)
│   └── GAME2 (players C, D)  
├── GameHistory[] (completed games)
└── Room Statistics (total scores, wins)
```

**Key Concepts:**
- **Rooms**: Persistent containers with player membership and game history
- **Games**: Individual match instances with unique IDs within rooms
- **Game Context**: Players can be in a room without being in a specific game
- **Game Isolation**: Players in different games don't see each other's messages

### Room Lifecycle

1. **Room Creation**: `{"type": "createGame"}` creates a room with the player in it
2. **Room Joining**: Players join rooms and can chat/coordinate before joining games
3. **Game Joining**: `{"type": "joinGame", "roomId": "ABC123", "gameId": "GAME1"}` 
4. **Game Playing**: Standard golf game mechanics within isolated games
5. **Game Completion**: Games are cleaned up, stats added to room history

## WebSocket Endpoints

The golf game is served at `/games/v1/golf-ws` on the games backend server.

## Message Protocol

### Client to Server:

#### Room Management
- `createGame` - Create a new room (returns room with empty games)
- `joinGame` - **REQUIRES both roomId AND gameId**: `{"type": "joinGame", "roomId": "ABC123", "gameId": "GAME1"}`
- `startNewGame` - Create a new game within current room
- `getRoomState` - Get current room state with all games

#### Game Actions  
- `startGame` - Start specific game (requires 2+ players)
- `peekCard` - Peek at one of your cards
- `drawCard` - Draw from deck  
- `takeFromDiscard` - Take top discard card
- `swapCard` - Swap drawn card with your card
- `discardDrawn` - Discard the drawn card
- `knock` - Signal final round
- `hideCards` - Hide peeked cards after timeout

#### Future Chat Support (Ready for Implementation)
- `listGames` - List all games in current room
- `createGameInRoom` - Create new game in current room  
- `leaveGame` - Leave current game but stay in room

### Server to Client:

#### Room Messages
- `roomJoined` - Confirmation with player ID and room state
- `roomStateUpdate` - Updated room state (players, games, history)
- `newGameStarted` - New game created in room

#### Game Messages  
- `gameJoined` - Confirmation with player ID and game state
- `gameState` - Full game state update (personalized per player)
- `gameStarted` - Game has begun
- `turnChanged` - Turn changed to new player  
- `playerKnocked` - Player has knocked
- `gameEnded` - Game over with winner and scores

#### System Messages
- `error` - Error message
- `gameListUpdate` - Games added/removed from room (future)

## Examples

### Creating a Room and Starting a Game

```javascript
// 1. Create room
ws.send(JSON.stringify({"type": "createGame"}));
// Response: {"type": "roomJoined", "playerId": "player_ABC123_1", "roomState": {...}}

// 2. Other player joins room and specific game
ws.send(JSON.stringify({
  "type": "joinGame", 
  "roomId": "ABC123", 
  "gameId": "GAME1"
}));

// 3. First player also joins the same game
ws.send(JSON.stringify({
  "type": "joinGame",
  "roomId": "ABC123", 
  "gameId": "GAME1"  
}));

// 4. Start the game
ws.send(JSON.stringify({"type": "startGame"}));
```

### Multiple Games in Same Room

```javascript
// Players can create/join different games within the same room:
// - Players A, B join GAME1
// - Players C, D join GAME2  
// - Both games run concurrently with complete isolation

ws.send(JSON.stringify({
  "type": "joinGame",
  "roomId": "ABC123",
  "gameId": "GAME2"  // Different game ID
}));
```

## Implementation Status

### ✅ Phase 1 Complete (Current)
- ✅ Multiple concurrent games per room
- ✅ Game isolation and independent state  
- ✅ Required gameId for all game operations
- ✅ Automatic game cleanup
- ✅ Room-level player management
- ✅ Comprehensive test coverage

### 🚧 Phase 2 (Future)
- [ ] `listGames` message for game discovery
- [ ] `createGameInRoom` message
- [ ] `leaveGame` message (stay in room)
- [ ] Room-based chat system
- [ ] Game spectator mode

### 🔮 Phase 3+ (Future)  
- [ ] Tournament bracket support
- [ ] Room-specific game variants
- [ ] Advanced room management features

## Development

Run all tests:
```bash
bazel test //domains/games/apis/games_ws_backend/golf:all
```

Run specific test categories:
```bash
# Test new multi-game functionality
bazel test //domains/games/apis/games_ws_backend/golf:all --test_filter="TestHub_Multi"

# Test game isolation
bazel test //domains/games/apis/games_ws_backend/golf:all --test_filter="TestHub_GameIsolation"
```

Build:
```bash
bazel build //domains/games/apis/games_ws_backend:games_ws_backend
```

## Architecture Notes

- **No Backward Compatibility**: Clean break from single-game model per STEPS.md
- **Explicit Game Targeting**: All operations require gameId specification  
- **State Isolation**: Each game maintains completely separate state
- **Room as Coordinator**: Rooms manage multiple games and player assignments
- **Aggressive Cleanup**: Completed games are removed immediately after stats collection
- **Chat-Ready**: Architecture supports room-level chat without any game context