# Thoughts Game Server

A real-time multiplayer WebSocket game server written in Go for the Thoughts Game - an interactive 3D multiplayer experience where players navigate as different shapes in a shared world.

## Overview

This server handles real-time player interactions for the Thoughts Game, including:
- WebSocket connection management with origin validation
- Player state synchronization (position, shape, color)
- Game state broadcasting

## Architecture

### Core Components

- **Hub** (`hub.go`): Central message broker that manages all active clients and game state
- **Client** (`client.go`): WebSocket connection handler with read/write pumps
- **Game** (`game.go`): Game protocol definitions and message validation
- **Main** (`main.go`): HTTP server setup and WebSocket upgrade handling

### Protocol

The server uses JSON messages for client-server communication:

```json
{
  "type": "player_join|position_update|shape_update|player_leave|game_state",
  "playerId": "player-abc123",
  "position": [x, y, z],
  "color": [r, g, b],
  "shape": 0|1|2,
  "timestamp": 1234567890
}
```

## Running the Server

### Development Mode
```bash
DEV_MODE=1 go run .
```

### Production
```bash
go build
./thoughts
```

### Configuration

- `DEV_MODE`: Enables debug logging and allows all WebSocket origins
- `-addr`: Server address (default: `:8080`)

## Deployment

The server is designed to run behind Caddy as a reverse proxy:

```bash
cd deploy
docker-compose up -d
```

## Security

- **Origin Validation**: Only allows connections from `thoughts.muchq.com`, `muchq.com`, and `www.muchq.com` in production
- **HTTPS Enforcement**: Rejects non-HTTPS origins in production

## Development Phases

### Phase 1: Player ID Generation and Session Token Generation
**Priority**: Foundation security improvements

**Required Changes**:
- [ ] Remove client-generated player IDs from the protocol
- [ ] Implement server-side ID generation during WebSocket handshake
- [ ] Add session token management for reconnection support
- [ ] Update protocol to include server-assigned IDs in initial handshake message

```go
// Example implementation
type Handshake struct {
    Type           string `json:"type"`
    AssignedID     string `json:"assignedId"`
    SessionToken   string `json:"sessionToken"`
    InitialState   GameState `json:"initialState"`
}
```

### Phase 2: Join/Leave Messages and Watching Mode
**Priority**: Core player state management and spectator functionality

**Required Changes**:
- [ ] Add explicit `player_join` and `player_leave` message types to protocol
- [ ] Implement proper join/leave message handling in Hub
- [ ] Add `watching` player mode for spectators
- [ ] Create watching mode state management (watchers don't affect game state)
- [ ] Send only `game_state` updates to watchers (no other message types)
- [ ] Block all watcher messages except `condense_into_player` transition
- [ ] Add watcher count to game state messages

```go
// New message types
type PlayerJoin struct {
    Type     string `json:"type"`  // "player_join"
    PlayerID string `json:"playerId"`
    Mode     string `json:"mode"`  // "playing" or "watching"
    Position [3]float64 `json:"position,omitempty"`  // only for playing mode
    Color    [3]float64 `json:"color,omitempty"`    // only for playing mode
    Shape    int        `json:"shape,omitempty"`    // only for playing mode
}

type PlayerLeave struct {
    Type     string `json:"type"`  // "player_leave"
    PlayerID string `json:"playerId"`
    Reason   string `json:"reason,omitempty"` // "disconnect", "quit", etc.
}

type CondenseIntoPlayer struct {
    Type     string `json:"type"`  // "condense_into_player"
    PlayerID string `json:"playerId"`
    Position [3]float64 `json:"position"`
    Color    [3]float64 `json:"color"`
    Shape    int        `json:"shape"`
}

// Updated game state
type GameState struct {
    Type           string             `json:"type"`  // "game_state"
    Players        map[string]*Player `json:"players"`  // only playing players
    WatcherCount   int                `json:"watcherCount"`
    Timestamp      int64              `json:"timestamp"`
}

// Player modes
type PlayerMode string
const (
    ModePlaying  PlayerMode = "playing"
    ModeWatching PlayerMode = "watching"
)

// Message validation for watchers
func (h *Hub) validateWatcherMessage(msg []byte, clientMode PlayerMode) bool {
    if clientMode != ModeWatching {
        return true  // playing clients can send any valid message
    }
    
    var baseMsg struct {
        Type string `json:"type"`
    }
    if err := json.Unmarshal(msg, &baseMsg); err != nil {
        return false
    }
    
    // Watchers can only send condense_into_player messages
    return baseMsg.Type == "condense_into_player"
}
```

### Phase 3: Add Mass to Player Model
**Priority**: Core gameplay mechanics foundation

**Required Changes**:
- [ ] Add `Mass float64` field to Player struct
- [ ] Include mass in game state messages
- [ ] Implement mass accumulation logic for player interactions
- [ ] Add mass-based movement physics (larger mass = slower movement)
- [ ] Add radius calculation based on mass for collision detection

```go
type Player struct {
    ID       string     `json:"id"`
    Position [3]float64 `json:"position"`
    Color    [3]float64 `json:"color"`
    Shape    int        `json:"shape"`
    Mass     float64    `json:"mass"`    // New field
    Velocity [3]float64 `json:"velocity"` // For physics
    Radius   float64    `json:"radius"`   // For collision detection
    Mode     PlayerMode `json:"mode"`    // "playing" or "watching"
}
```

### Phase 4: Hoovering Mechanic Support
**Priority**: Core player interaction system

**Required Changes**:
- [ ] Add `hoover_attempt` message type to protocol
- [ ] Implement server-side collision detection between players
- [ ] Add hoovering range and success validation logic
- [ ] Handle mass transfer from hoovered to hooverer
- [ ] Implement post-hoover choice system (respawn, watch, haunt)
- [ ] Add cooldown periods to prevent hoover spam

```go
// New message types
type HooverAttempt struct {
    Type       string `json:"type"`  // "hoover_attempt"
    AttackerID string `json:"attackerId"`
    TargetID   string `json:"targetId"`
    Timestamp  int64  `json:"timestamp"`
}

type HooverResult struct {
    Type         string  `json:"type"`  // "hoover_result"
    AttackerID   string  `json:"attackerId"`
    TargetID     string  `json:"targetId"`
    Success      bool    `json:"success"`
    MassGained   float64 `json:"massGained,omitempty"`
    FailureReason string `json:"failureReason,omitempty"`
}
```

### Phase 5: Simple Feature Flag Support
**Priority**: Configuration and experimentation infrastructure

**Required Changes**:
- [ ] Add feature flag configuration system (environment variables or config file)
- [ ] Implement feature flag validation and defaults
- [ ] Add feature flags for experimental gameplay mechanics
- [ ] Create feature flag middleware for message handling
- [ ] Add runtime feature flag updates via admin interface

```go
type FeatureFlags struct {
    EnableHoovering     bool `json:"enableHoovering" env:"ENABLE_HOOVERING"`
    EnableMassPhysics   bool `json:"enableMassPhysics" env:"ENABLE_MASS_PHYSICS"`
    EnableCollisions    bool `json:"enableCollisions" env:"ENABLE_COLLISIONS"`
    MaxPlayersPerGame   int  `json:"maxPlayersPerGame" env:"MAX_PLAYERS_PER_GAME"`
    HooverCooldownMs    int  `json:"hooverCooldownMs" env:"HOOVER_COOLDOWN_MS"`
}

func (h *Hub) isFeatureEnabled(flag string) bool {
    return h.featureFlags.get(flag)
}
```

### Phase 6: New Level Architecture and Design
**Priority**: Scalability and advanced gameplay

**Required Changes**:
- [ ] Design multi-level game progression system
- [ ] Implement level-based player separation and transitions
- [ ] Add level-specific rules and physics parameters
- [ ] Create level management and player migration logic
- [ ] Design level unlock criteria based on mass thresholds
- [ ] Add level-specific visual themes and environments

```go
type Level struct {
    ID               int     `json:"id"`
    Name             string  `json:"name"`
    MinMassRequired  float64 `json:"minMassRequired"`
    MaxPlayers       int     `json:"maxPlayers"`
    PhysicsSettings  LevelPhysics `json:"physicsSettings"`
    Environment      LevelEnvironment `json:"environment"`
}

type LevelManager struct {
    levels    map[int]*Level
    players   map[string]int  // playerID -> levelID
    mu        sync.RWMutex
}
```

### Future Enhancements to Consider

- [ ] **Multi-Game Support**: Support multiple concurrent game sessions
- [ ] **Rate Limiting**: Implement per-player rate limiting for actions
- [ ] **Persistence**: Add database support for player progress and game history
- [ ] **Metrics**: Implement Prometheus metrics for monitoring
- [ ] **Anti-Cheat**: Server-side physics validation and anomaly detection
- [ ] **Replay System**: Record game sessions for debugging and spectating
- [ ] **Spectator Mode**: Allow disconnected players to watch ongoing games
- [ ] **Spatial Partitioning**: Optimize collision detection for large player counts

## Contributing

When adding new features:
1. Maintain backward compatibility where possible
2. Validate all client inputs
3. Consider performance implications for large player counts
4. Add tests for new game logic

