# Golf Game Improvements: Room-Based Multi-Game System

## Overview

This document outlines improvements to the golf card game to support room-based gameplay where players can play multiple games in a single room while tracking cumulative scores across games.

## ✅ IMPLEMENTATION STATUS

**Phase 1 Multi-Game Room Architecture: FULLY COMPLETED** (2025-09-08)

The room-based multi-game system has been successfully implemented and **significantly evolved beyond the original plan**. The current implementation supports **multiple concurrent games** within the same room (not just sequential games), with complete game isolation and a chat-ready architecture.

### ✅ Implemented Features

1. **Room and Game Separation**: 
   - `Room` struct with persistent player stats, game history, and current game
   - `Player` struct combines both room stats and game state
   - Separate room ID and game ID concepts

2. **Room Management**:
   - Room creation with `createRoom()` 
   - Player joining with `addPlayerToRoom()`
   - Room cleanup when all players disconnect
   - Room state broadcasting with `RoomStateUpdateMessage`

3. **Multi-Game Flow**:
   - `startNewGameInRoom()` creates new games within existing rooms
   - `completeGameInRoom()` updates room stats when games end
   - `handleStartNewGame()` allows "play again" functionality
   - Game results stored in room's `GameHistory`

4. **Cumulative Scoring**:
   - `TotalScore`: Running sum across all games in room
   - `GamesPlayed`: Count of completed games
   - `GamesWon`: Count of games where player had lowest score
   - Stats automatically updated when games complete

5. **New Message Types**:
   - `StartNewGameMessage` for starting another game
   - `RoomJoinedMessage` for initial room entry
   - `RoomStateUpdateMessage` for room state changes
   - `NewGameStartedMessage` for game start notifications

## ✅ RESOLVED Architecture Issues

The following issues have been **completely resolved** in the current implementation:

1. **✅ Room ID = Game ID**: **RESOLVED** - Room IDs and Game IDs are now completely separate. Multiple games can exist concurrently in the same room.
2. **✅ No Score Persistence**: **RESOLVED** - Player stats (`TotalScore`, `GamesPlayed`, `GamesWon`) persist across all games in a room.
3. **✅ "Play Again" Broken**: **RESOLVED** - Games are automatically cleaned up when completed, allowing unlimited new games in the same room.

## ✅ IMPLEMENTED Architecture (Current System)

### ✅ 1. Room and Game Separation - **FULLY IMPLEMENTED**

#### ✅ Backend Implementation Complete

**✅ Current Types in `types.go`:**
```go
// ✅ IMPLEMENTED - Room represents a persistent room where multiple games can be played
type Room struct {
    ID              string                 `json:"id"`
    Players         []*Player              `json:"players"`    // Players with room + game stats
    Games           map[string]*Game       `json:"games"`      // Multiple concurrent games
    GameHistory     []*GameResult          `json:"gameHistory"`
    CreatedAt       time.Time              `json:"createdAt"`
    LastActivity    time.Time              `json:"lastActivity"`
}

// ✅ IMPLEMENTED - Player combines both room stats and game state  
type Player struct {
    // Game-specific fields
    ID            string  `json:"id"`
    Name          string  `json:"name"`
    Cards         []*Card `json:"cards"`
    Score         int     `json:"score"`
    // ... game fields
    
    // Room/persistence fields - ✅ IMPLEMENTED
    ClientID      string    `json:"clientId"`
    TotalScore    int       `json:"totalScore"`      // Running total across all games
    GamesPlayed   int       `json:"gamesPlayed"`
    GamesWon      int       `json:"gamesWon"`
    IsConnected   bool      `json:"isConnected"`
    JoinedAt      time.Time `json:"joinedAt"`
}

// ✅ IMPLEMENTED - GameContext tracks both room and game for clients
type GameContext struct {
    RoomID string `json:"roomId"`
    GameID string `json:"gameId"`
}

// ✅ IMPLEMENTED - All message types
type JoinGameMessage struct {
    Type   string `json:"type"`
    RoomID string `json:"roomId"`  // ✅ Required
    GameID string `json:"gameId"`  // ✅ Required
}
```

**✅ Current Hub Structure:**
```go
type GolfHub struct {
    // ✅ IMPLEMENTED - rooms instead of games
    rooms        map[string]*Room
    clientToGame map[*hub.Client]*GameContext  // ✅ Tracks both room+game
    
    // Keep existing fields
    mu           sync.RWMutex
    gameMessage  chan hub.GameMessageData
    register     chan *hub.Client
    unregister   chan *hub.Client
    clients      map[*hub.Client]bool
}
```

#### ✅ Game Lifecycle - **FULLY IMPLEMENTED**

1. **✅ Room Creation**: `{\"type\": \"createGame\"}` creates a room with unique ID
2. **✅ Game Creation**: `{\"type\": \"joinGame\", \"roomId\": \"ABC123\", \"gameId\": \"GAME1\"}` creates games with unique IDs
3. **✅ Game Completion**: When a game ends:
   - ✅ Store `GameResult` in room's `GameHistory`
   - ✅ Update players' cumulative stats (`TotalScore`, `GamesPlayed`, `GamesWon`)
   - ✅ Remove game from `Games` map (automatic cleanup)
4. **✅ New Game**: Players can create unlimited new games within the same room

### ✅ 2. Multi-Game Flow - **FULLY IMPLEMENTED**

#### ✅ Backend Implementation Complete

**✅ Room Management Methods (Implemented):**
```go
// ✅ IMPLEMENTED in golf_hub.go
func (h *GolfHub) createRoom() *Room
func (h *GolfHub) getOrCreateGame(roomID, gameID string) (*Game, error)
func (h *GolfHub) completeGame(roomID, gameID string, finalScores []*FinalScore)
func (h *GolfHub) removePlayerFromRoom(client *hub.Client)
// + comprehensive message handlers for all game operations
```

**✅ Game State Transitions (Implemented):**
```
Room Created → Multiple Concurrent Games → Games Complete → New Games Created
    ↑                      ↓                      ↓              ↓
    ←←← Players can join any game or create new games ←←←←←←←←←←
```

**✅ Message Handling (All Implemented):**
```go
// ✅ IMPLEMENTED - All message types supported
case "createGame", "joinGame", "startGame": // ✅ Room & game management
case "peekCard", "drawCard", "swapCard":    // ✅ Game actions  
case "knock", "takeFromDiscard":            // ✅ Advanced game actions
// + comprehensive error handling and validation
```

### 🚧 NEXT PHASE: Frontend Implementation

#### 🚧 Required Frontend Updates (Phase 2)
The backend is **fully complete** and ready. Frontend needs updates to support the new architecture:

**🚧 UI State Updates Needed:**
1. **Room Lobby State**: Support for players in rooms without being in games
2. **Multi-Game Awareness**: Display multiple concurrent games in the same room  
3. **Game End Flow**: Show cumulative scores and "start new game" options

**🚧 Component Updates Required:**

**GolfGame.tsx Changes Needed:**
```tsx
// 🚧 TODO - Add room state management
const [roomState, setRoomState] = useState<Room | null>(null)
const [gameContext, setGameContext] = useState<{roomId: string, gameId: string} | null>(null)

// 🚧 TODO - Update join flow to require both roomId and gameId
const joinGame = (roomId: string, gameId: string) => {
  ws.send(JSON.stringify({
    type: "joinGame", 
    roomId: roomId,
    gameId: gameId  // Now required by backend
  }));
}

// 🚧 TODO - Handle new message types
useEffect(() => {
  // Handle roomJoined, roomStateUpdate messages
  // Update game end flow to show room stats
}, []);
```

**🚧 New Components Needed:**
- `RoomLobby.tsx`: Shows room info, connected players, and cumulative scores
- `GameResults.tsx`: Displays individual game results with room context
- `CumulativeScores.tsx`: Shows running totals across games
- `GameSelector.tsx`: Shows available games in room and allows creating new ones

### ✅ 3. Score Tracking System - **FULLY IMPLEMENTED (Backend)**

#### ✅ Cumulative Scoring Rules (Implemented)
1. **✅ Game Score**: Individual game scores (unchanged)
2. **✅ Total Score**: Sum of all game scores in the room
3. **✅ Games Won**: Count of games where player had lowest score  
4. **✅ Win Percentage**: Can be calculated as `GamesWon / GamesPlayed * 100`

#### 🚧 Score Display (Frontend TODO)
```tsx
// 🚧 TODO - Frontend implementation needed
<div className={styles.playerStats}>
  <div className={styles.playerName}>{player.name}</div>
  <div className={styles.stats}>
    <span>Total: {player.totalScore}</span>
    <span>Games: {player.gamesPlayed}</span>
    <span>Wins: {player.gamesWon}</span>
    <span>Win%: {(player.gamesWon / player.gamesPlayed * 100).toFixed(1)}%</span>
  </div>
</div>
```

### 4. Database Schema (Optional - Future Enhancement)

For persistent room history:

```sql
CREATE TABLE rooms (
    id VARCHAR(6) PRIMARY KEY,
    created_at TIMESTAMP,
    last_activity TIMESTAMP
);

CREATE TABLE room_players (
    room_id VARCHAR(6),
    player_name VARCHAR(50),
    total_score INT DEFAULT 0,
    games_played INT DEFAULT 0,
    games_won INT DEFAULT 0,
    joined_at TIMESTAMP,
    FOREIGN KEY (room_id) REFERENCES rooms(id)
);

CREATE TABLE games (
    id UUID PRIMARY KEY,
    room_id VARCHAR(6),
    winner VARCHAR(50),
    completed_at TIMESTAMP,
    FOREIGN KEY (room_id) REFERENCES rooms(id)
);

CREATE TABLE game_scores (
    game_id UUID,
    player_name VARCHAR(50),
    score INT,
    FOREIGN KEY (game_id) REFERENCES games(id)
);
```

## Implementation Plan & Status

### ✅ Phase 1: Backend Foundation - **FULLY COMPLETED (2025-09-08)**
1. ✅ **COMPLETED** - Analyzed current architecture and identified multi-game requirements
2. ✅ **COMPLETED** - Created new room/game separation types with `GameContext` 
3. ✅ **COMPLETED** - Implemented comprehensive room management in `GolfHub`
4. ✅ **COMPLETED** - Updated all message handling for multi-game flow
5. ✅ **COMPLETED** - Added cumulative scoring logic with automatic stat updates
6. ✅ **COMPLETED** - Wrote comprehensive test suite (8 total tests covering all scenarios)
7. ✅ **COMPLETED** - All tests passing with `bazel test //go/games_ws_backend/golf:all`

**✅ Extra Features Implemented Beyond Original Plan:**
- **Multiple concurrent games** per room (not just sequential)
- **Game isolation** - players in different games don't interfere
- **Chat-ready architecture** - players can join rooms without joining games
- **Automatic game cleanup** - completed games are removed after stats collection
- **Comprehensive error handling** - validates all game operations
- **WebSocket message flow documentation** - complete example in `EXAMPLE_GOLF.md`

### 🚧 Phase 2: Frontend Updates - **PENDING**
1. 🚧 **TODO** - Update network adapter for room-based communication (requires `roomId` + `gameId`)
2. 🚧 **TODO** - Modify `GolfGame` component to handle `roomJoined` and `roomStateUpdate` messages
3. 🚧 **TODO** - Create new UI components for room lobby and multi-game awareness
4. 🚧 **TODO** - Update game end flow to show cumulative stats and "new game" option

### 🚧 Phase 3: Testing & Polish - **READY WHEN FRONTEND COMPLETE**
1. 🧪 **BACKEND TESTED** - Multi-game scenarios fully tested in backend
2. ✅ **COMPLETED** - Player disconnection and room cleanup working
3. 🚧 **TODO** - Add room expiration/cleanup logic (low priority)
4. 🚧 **TODO** - Performance testing with multiple concurrent rooms (after frontend)

### 🔮 Phase 4: Future Enhancements - **READY FOR IMPLEMENTATION**
1. 🔮 **READY** - Persistent room storage (database integration) - architecture supports it
2. 🔮 **READY** - Room settings (game variants, scoring rules)  
3. 🔮 **READY** - Tournament mode (best of X games)
4. 🔮 **READY** - Spectator mode for completed rooms

## ✅ ACHIEVED Benefits 

The current implementation delivers all planned benefits:

1. **✅ Enhanced Social Experience**: Players stay together across multiple games with persistent room membership
2. **✅ Competitive Element**: Running scores (`TotalScore`, `GamesWon`, `GamesPlayed`) add stakes and engagement  
3. **✅ Persistent Context**: Rooms maintain player relationships and complete game history
4. **✅ Scalability**: Clean room/game separation enables concurrent games and room-specific features
5. **✅ Analytics**: Rich data collection with `GameHistory` and cumulative player statistics
6. **✅ BONUS: Chat-Ready**: Architecture supports room-level chat without game context
7. **✅ BONUS: Game Isolation**: Multiple concurrent games per room with complete independence

## 🚨 Breaking Changes (No Backward Compatibility)

**As planned in STEPS.md, this is a clean break from the old architecture:**

1. **🚨 Required gameId**: All `joinGame` messages now require both `roomId` and `gameId` parameters
2. **🚨 New Message Protocol**: Frontend must handle `roomJoined` and `roomStateUpdate` messages
3. **🚨 Game Context**: Players are now tracked with `GameContext` (room + game) instead of just room
4. **🚨 Multiple Games**: Rooms can contain multiple concurrent games instead of single game

**Migration Path:** Frontend must be updated to use new protocol. No backward compatibility layer provided.

## ✅ IMPLEMENTED Technical Considerations

All technical concerns have been addressed in the current implementation:

1. **✅ Memory Management**: Automatic room cleanup implemented - games removed when completed, rooms cleaned up when all players disconnect
2. **✅ Concurrency**: Thread-safe operations with proper mutex usage in `GolfHub`
3. **✅ Network Efficiency**: Optimized message broadcasting within rooms and games
4. **✅ Error Handling**: Comprehensive error handling for invalid room/game operations  
5. **✅ Testing**: Complete test coverage for room lifecycle events (8 comprehensive test functions)

## 🎯 NEXT STEPS (Immediate Priorities)

### 1. Frontend Implementation (Critical Path)
The backend is **production-ready**. Frontend updates needed:
- Update WebSocket client to send `roomId` + `gameId` in `joinGame` messages  
- Handle new message types (`roomJoined`, `roomStateUpdate`)
- Create room lobby UI showing multiple games and cumulative scores
- Add "start new game" flow after games complete

### 2. Documentation & Examples
- ✅ **COMPLETED** - `EXAMPLE_GOLF.md` provides complete WebSocket message flow
- ✅ **COMPLETED** - `README.md` updated with current architecture
- ✅ **COMPLETED** - All code properly documented

### 3. Optional Enhancements (After Frontend)
- Room expiration/cleanup timers (low priority - manual cleanup working)
- Advanced room management features 
- Chat system implementation (architecture ready)
- Database persistence (architecture ready)