# Golf Game Improvements: Room-Based Multi-Game System

## Overview

This document outlines improvements to the golf card game to support room-based gameplay where players can play multiple games in a single room while tracking cumulative scores across games.

## ✅ IMPLEMENTATION STATUS

**Phase 1 Backend Foundation: COMPLETED** (2025-09-07)

The room-based multi-game system has been successfully implemented with the following features:

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

## Current Architecture Issues

1. **Room ID = Game ID**: Currently, the room code and game ID are the same, making it impossible to play multiple games in the same room.
2. **No Score Persistence**: Scores are only tracked for individual games, with no running totals across multiple games.
3. **"Play Again" Broken**: When a game ends, players cannot start a new game because the game state remains in "ended" phase.

## Proposed Architecture Changes

### 1. Separate Room and Game Concepts

#### Backend Changes

**New Types in `types.go`:**
```go
// Room represents a persistent room where multiple games can be played
type Room struct {
    ID              string                 `json:"id"`
    Players         []*RoomPlayer         `json:"players"`
    CurrentGame     *Game                 `json:"currentGame,omitempty"`
    GameHistory     []*GameResult         `json:"gameHistory"`
    CreatedAt       time.Time             `json:"createdAt"`
}

// RoomPlayer extends Player with cumulative stats
type RoomPlayer struct {
    ID              string    `json:"id"`
    Name            string    `json:"name"`
    ClientID        string    `json:"clientId"`
    TotalScore      int       `json:"totalScore"`      // Running total across all games
    GamesPlayed     int       `json:"gamesPlayed"`
    GamesWon        int       `json:"gamesWon"`
    IsConnected     bool      `json:"isConnected"`
    JoinedAt        time.Time `json:"joinedAt"`
}

// GameResult stores the outcome of a completed game
type GameResult struct {
    GameID          string        `json:"gameId"`
    Winner          string        `json:"winner"`
    FinalScores     []*FinalScore `json:"finalScores"`
    CompletedAt     time.Time     `json:"completedAt"`
}

// New message types
type StartNewGameMessage struct {
    Type string `json:"type"`
}

type RoomStateUpdateMessage struct {
    Type      string `json:"type"`
    RoomState *Room  `json:"roomState"`
}
```

**New Hub Structure:**
```go
type GolfHub struct {
    // Change from games to rooms
    rooms           map[string]*Room
    clientToRoom    map[*hub.Client]string
    
    // Keep existing fields
    mu              sync.RWMutex
    gameMessage     chan hub.GameMessageData
    register        chan *hub.Client
    unregister      chan *hub.Client
    clients         map[*hub.Client]bool
}
```

#### Game Lifecycle Changes

1. **Room Creation**: When a player creates a game, create a `Room` with a unique room ID
2. **Game Creation**: Within a room, create individual `Game` instances with unique game IDs
3. **Game Completion**: When a game ends:
   - Store `GameResult` in room's `GameHistory`
   - Update players' cumulative stats (`TotalScore`, `GamesPlayed`, `GamesWon`)
   - Set `CurrentGame` to `nil`
4. **New Game**: Players can start a new game within the same room

### 2. Multi-Game Flow Implementation

#### Backend Implementation

**Room Management Methods:**
```go
// In golf_hub.go
func (h *GolfHub) createRoom() *Room
func (h *GolfHub) addPlayerToRoom(roomID, clientID string) (*RoomPlayer, error)
func (h *GolfHub) removePlayerFromRoom(roomID, clientID string) error
func (h *GolfHub) startNewGameInRoom(roomID string) (*Game, error)
func (h *GolfHub) completeGameInRoom(roomID string, gameResult *GameResult)
```

**Game State Transitions:**
```
Room Created → Waiting for Players → Game Started → Game Playing → Game Ended → 
    ↑                                                                      ↓
    ←←←←←←←←←←←← Start New Game (resets to Game Started) ←←←←←←←←←←←←
```

**Message Handling Updates:**
```go
// Add new message types to handleGameMessage
case "startNewGame":
    h.handleStartNewGame(msgData.Sender)
case "getRoomState":
    h.handleGetRoomState(msgData.Sender)
```

#### Frontend Implementation

**New UI States:**
1. **Room Lobby**: Shows room info, connected players, and cumulative scores
2. **Between Games**: Shows game results and option to start new game
3. **Game Active**: Current game interface (unchanged)

**Updated Components:**

**GolfGame.tsx Changes:**
```tsx
// Add room state management
const [roomState, setRoomState] = useState<Room | null>(null)
const [betweenGames, setBetweenGames] = useState(false)

// Update game end overlay to show cumulative scores
{gameState?.gamePhase === 'ended' && (
  <div className={styles.gameEndOverlay}>
    {/* Current game results */}
    <GameResults gameState={gameState} winner={winner} />
    
    {/* Cumulative room scores */}
    <CumulativeScores roomState={roomState} />
    
    {/* Action buttons */}
    <button onClick={startNewGame}>Play Another Game</button>
    <button onClick={() => window.location.reload()}>Leave Room</button>
  </div>
)}
```

**New Components:**
- `RoomLobby.tsx`: Shows room info and player stats
- `GameResults.tsx`: Displays individual game results
- `CumulativeScores.tsx`: Shows running totals across games

### 3. Score Tracking System

#### Cumulative Scoring Rules
1. **Game Score**: Individual game scores (unchanged)
2. **Total Score**: Sum of all game scores in the room
3. **Games Won**: Count of games where player had lowest score
4. **Win Percentage**: `GamesWon / GamesPlayed * 100`

#### Score Display
```tsx
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

## Implementation Plan

### Phase 1: Backend Foundation
1. ✅ Analyze current architecture
2. ✅ Create new room/game separation types - **COMPLETED**
3. ✅ Implement room management in `GolfHub` - **COMPLETED**
4. ✅ Update message handling for new flow - **COMPLETED**
5. ✅ Add cumulative scoring logic - **COMPLETED**

### Phase 2: Frontend Updates
1. Update network adapter for room-based communication
2. Modify `GolfGame` component for room states
3. Create new UI components for room lobby and score tracking
4. Update game end flow to show cumulative stats

### Phase 3: Testing & Polish
1. Test multi-game scenarios
2. Handle edge cases (player disconnections, room cleanup)
3. Add room expiration/cleanup logic
4. Performance testing with multiple concurrent rooms

### Phase 4: Future Enhancements
1. Persistent room storage (database integration)
2. Room settings (game variants, scoring rules)
3. Tournament mode (best of X games)
4. Spectator mode for completed rooms

## Benefits

1. **Enhanced Social Experience**: Players can stay together for multiple games
2. **Competitive Element**: Running scores add stakes and engagement
3. **Persistent Context**: Room maintains player relationships and history
4. **Scalability**: Clear separation allows for room-specific features
5. **Analytics**: Rich data for player behavior and game balance

## Migration Strategy

1. **Backward Compatibility**: Keep existing single-game flow as default
2. **Gradual Rollout**: New room-based flow as opt-in feature initially
3. **Data Migration**: Convert existing games to single-game rooms
4. **Feature Toggle**: Allow switching between old and new systems during transition

## Technical Considerations

1. **Memory Management**: Implement room cleanup for inactive rooms
2. **Concurrency**: Ensure thread-safe operations on room state
3. **Network Efficiency**: Optimize message broadcasting within rooms
4. **Error Handling**: Graceful degradation when room operations fail
5. **Testing**: Comprehensive test coverage for room lifecycle events