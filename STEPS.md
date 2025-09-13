# Implementation Steps for Room-Based Multi-Game Architecture

## Current State Analysis
The room-based architecture has been successfully implemented for the Golf game in `go/games_ws_backend/golf/`. The core room/game separation is in place with:
- **Rooms** as persistent containers with player membership and game history
- **Games** as individual match instances with unique IDs
- Room-level statistics tracking cumulative scores and win counts
- Support for "play again" functionality within rooms

## Goals from ROOMS.md
1. Enable multiple concurrent games within the same room
2. Support optional `gameId` field in `JoinGameMessage` for joining specific games
3. Foundation for tournaments, spectator modes, and room-specific variants

## Implementation Steps

### Phase 1: Core Multi-Game Support (Backend)
1. **Update Room Data Structure** - `go/games_ws_backend/golf/types.go`
   - Change `CurrentGame *Game` to `Games map[string]*Game` in Room struct
   - Remove `DefaultGameID` - no backward compatibility needed
   - Update Room methods to handle multiple games

2. **Update Game Management Logic** - `go/games_ws_backend/golf/golf_hub.go`
   - Modify `startNewGameInRoom()` to support multiple concurrent games
   - Update `getClientGame()` to require game selection
   - Add game cleanup when games end (remove from Games map)
   - Handle player assignment to specific games

3. **Enhance Message Handling**
   - Make `gameId` required in `JoinGameMessage`
   - Update `handleJoinRoom()` to require specific game targeting
   - Add logic for creating new games when `gameId` doesn't exist
   - Update game state broadcasts to only send to players in that specific game

4. **Update Player-Game Associations**
   - Modify client tracking to include both room and game context
   - Update `clientToRoom map[*hub.Client]string` to `clientToGame map[*hub.Client]GameContext`
   - Add methods to move players between games within a room

### Phase 2: Message Protocol Overhaul
5. **Add New Message Types** - `go/games_ws_backend/golf/types.go`
   - `ListGamesMessage` - Get all games in current room
   - `CreateGameInRoomMessage` - Create new game in current room
   - `GameListUpdateMessage` - Notify when games are added/removed from room
   - `LeaveGameMessage` - Leave current game but stay in room

6. **Update Message Handlers**
   - Remove fallback logic from `handleJoinRoom()` - require explicit gameId
   - Update `broadcastRoomState()` to include all games information
   - Make all game actions require explicit game context

### Phase 3: Multi-Game State Management
7. **Room-Level Game Coordination**
   - Implement game lifecycle management within rooms
   - Add automatic cleanup of completed games after stats are recorded
   - Handle player movement between games in same room

8. **Enhanced Room Interface**
   - Add game discovery and browsing within rooms
   - Show all active games with status and player counts
   - Enable players to observe ongoing games before joining

### Phase 4: Advanced Features Foundation
9. **Spectator Mode**
   - Add spectator joins that don't affect game state
   - Implement read-only game state for spectators
   - Allow spectators to switch between games in room

10. **Tournament Support**
    - Add tournament room type with bracket management
    - Implement automatic game progression
    - Add tournament-specific statistics tracking

### Phase 5: Performance & Scalability
11. **Optimized Broadcasting**
    - Implement game-specific message routing
    - Optimize state updates for multiple concurrent games
    - Add efficient cleanup of completed games

12. **Memory Management**
    - Implement proper game cleanup policies
    - Add room-level garbage collection
    - Optimize data structures for multiple games

### Phase 6: Testing & Operations
13. **Comprehensive Testing**
    - Unit tests for multi-game scenarios
    - Integration tests for concurrent game flows
    - Load testing with multiple games per room
    - Edge case testing (disconnections, game cleanup)

14. **Monitoring & Admin Tools**
    - Add metrics for games per room and concurrent performance
    - Implement administrative game management
    - Add debugging tools for multi-game scenarios

## Implementation Priority

**Immediate (Phase 1-2)**: Core multi-game support and protocol overhaul
**Near-term (Phase 3-4)**: State management and advanced features
**Future (Phase 5-6)**: Performance optimization and operational tools

## Key Design Decisions

1. **No Backward Compatibility**: Clean break from single-game model
2. **Explicit Game Targeting**: All operations require gameId specification
3. **Independent Game State**: Each game maintains completely separate state
4. **Room as Coordinator**: Rooms manage multiple games and player assignments
5. **Aggressive Cleanup**: Remove completed games immediately after stats collection

## Technical Approach

- **Game Context**: Replace simple room mapping with room+game context
- **Message Routing**: Route all messages through game-specific handlers
- **State Isolation**: Ensure complete isolation between concurrent games
- **Resource Management**: Implement proper cleanup to prevent memory leaks