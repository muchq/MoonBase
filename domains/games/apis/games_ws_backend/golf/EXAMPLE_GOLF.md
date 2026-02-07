# Golf WebSocket Message Flow Example

This example demonstrates a complete message flow for the room-based multi-game golf system.

## Scenario
- **Alice** creates a room
- **Bob** joins the room  
- **Alice** creates a golf game
- **Bob** joins the game
- They play and finish the game
- **Bob** starts a new game
- **Alice** joins the new game
- They play again

---

## Message Flow

### 1. Alice Creates Room

**Alice → Server:**
```json
{"type": "createGame"}
```

**Server → Alice:**
```json
{
  "type": "roomJoined",
  "playerId": "player_ABC123_1",
  "roomState": {
    "id": "ABC123",
    "players": [
      {
        "id": "player_ABC123_1",
        "name": "SunnyPenguin",
        "clientId": "192.168.1.100:54321",
        "totalScore": 0,
        "gamesPlayed": 0,
        "gamesWon": 0,
        "isConnected": true,
        "joinedAt": "2025-01-15T10:30:00Z"
      }
    ],
    "games": {},
    "gameHistory": [],
    "createdAt": "2025-01-15T10:30:00Z",
    "lastActivity": "2025-01-15T10:30:00Z"
  }
}
```

### 2. Bob Joins Room (without specific game)

**Bob → Server:**
```json
{
  "type": "joinGame",
  "roomId": "ABC123",
  "gameId": "LOBBY"
}
```

**Server → Bob:**
```json
{
  "type": "roomJoined",
  "playerId": "player_ABC123_2", 
  "roomState": {
    "id": "ABC123",
    "players": [
      {
        "id": "player_ABC123_1",
        "name": "SunnyPenguin",
        "clientId": "192.168.1.100:54321",
        "totalScore": 0,
        "gamesPlayed": 0,
        "gamesWon": 0,
        "isConnected": true,
        "joinedAt": "2025-01-15T10:30:00Z"
      },
      {
        "id": "player_ABC123_2",
        "name": "CozyFox",
        "clientId": "192.168.1.101:54322",
        "totalScore": 0,
        "gamesPlayed": 0,
        "gamesWon": 0,
        "isConnected": true,
        "joinedAt": "2025-01-15T10:30:15Z"
      }
    ],
    "games": {},
    "gameHistory": [],
    "createdAt": "2025-01-15T10:30:00Z",
    "lastActivity": "2025-01-15T10:30:15Z"
  }
}
```

**Server → Alice (room state update):**
```json
{
  "type": "roomStateUpdate",
  "roomState": {
    "id": "ABC123",
    "players": [
      {"id": "player_ABC123_1", "name": "SunnyPenguin", "isConnected": true},
      {"id": "player_ABC123_2", "name": "CozyFox", "isConnected": true}
    ],
    "games": {},
    "gameHistory": []
  }
}
```

### 3. Alice Creates and Joins Golf Game

**Alice → Server:**
```json
{
  "type": "joinGame",
  "roomId": "ABC123", 
  "gameId": "GAME1"
}
```

**Server → Alice:**
```json
{
  "type": "roomJoined",
  "playerId": "player_ABC123_1",
  "roomState": {
    "id": "ABC123", 
    "players": [
      {"id": "player_ABC123_1", "name": "SunnyPenguin", "isConnected": true},
      {"id": "player_ABC123_2", "name": "CozyFox", "isConnected": true}
    ],
    "games": {
      "GAME1": {
        "id": "GAME1",
        "players": [
          {
            "id": "player_ABC123_1",
            "name": "SunnyPenguin", 
            "cards": [null, null, null, null],
            "score": 0,
            "revealedCards": [],
            "isReady": false,
            "hasPeeked": false
          }
        ],
        "currentPlayerIndex": 0,
        "drawPile": 0,
        "discardPile": [],
        "gamePhase": "waiting"
      }
    },
    "gameHistory": []
  }
}
```

### 4. Bob Joins Game

**Bob → Server:**
```json
{
  "type": "joinGame",
  "roomId": "ABC123",
  "gameId": "GAME1" 
}
```

**Server → Bob:**
```json
{
  "type": "roomJoined",
  "playerId": "player_ABC123_2",
  "roomState": {
    "id": "ABC123",
    "games": {
      "GAME1": {
        "id": "GAME1", 
        "players": [
          {"id": "player_ABC123_1", "name": "SunnyPenguin"},
          {"id": "player_ABC123_2", "name": "CozyFox"}
        ],
        "gamePhase": "waiting"
      }
    }
  }
}
```

**Server → Alice (room state update):**
```json
{
  "type": "roomStateUpdate", 
  "roomState": {
    "games": {
      "GAME1": {
        "players": [
          {"name": "SunnyPenguin"}, 
          {"name": "CozyFox"}
        ]
      }
    }
  }
}
```

### 5. Alice Starts Game

**Alice → Server:**
```json
{"type": "startGame"}
```

**Server → Alice & Bob:**
```json
{"type": "gameStarted"}
```

**Server → Alice (personalized game state):**
```json
{
  "type": "gameState",
  "gameState": {
    "id": "GAME1",
    "players": [
      {
        "id": "player_ABC123_1",
        "name": "SunnyPenguin",
        "cards": [
          {"rank": "7", "suit": "♠"}, 
          {"rank": "K", "suit": "♥"},
          null,
          null
        ],
        "revealedCards": [],
        "hasPeeked": false
      },
      {
        "id": "player_ABC123_2", 
        "name": "CozyFox",
        "cards": [null, null, null, null],
        "revealedCards": [],
        "hasPeeked": false
      }
    ],
    "currentPlayerIndex": 0,
    "drawPile": 44,
    "discardPile": [{"rank": "3", "suit": "♦"}],
    "gamePhase": "peeking"
  }
}
```

### 6. Game Play (Abbreviated)

**Alice peeks at cards:**
```json
{"type": "peekCard", "cardIndex": 0}
```

**Alice peeks at another card:**
```json
{"type": "peekCard", "cardIndex": 2}
```

**Bob peeks at his cards:**
```json
{"type": "peekCard", "cardIndex": 1}
```
```json
{"type": "peekCard", "cardIndex": 3}
```

**Game transitions to playing phase, players take turns...**

**Alice draws card:**
```json
{"type": "drawCard"}
```

**Alice swaps card:**
```json
{"type": "swapCard", "cardIndex": 1}
```

**Bob takes from discard:**
```json
{"type": "takeFromDiscard"}
```

**Bob swaps card:**
```json
{"type": "swapCard", "cardIndex": 0}
```

**Alice knocks:**
```json
{"type": "knock"}
```

**Server → Alice & Bob:**
```json
{
  "type": "playerKnocked",
  "playerName": "SunnyPenguin"
}
```

**Bob takes final turn:**
```json
{"type": "drawCard"}
```
```json
{"type": "discardDrawn"}
```

### 7. Game Ends

**Server → Alice & Bob:**
```json
{
  "type": "gameEnded",
  "winner": "SunnyPenguin",
  "finalScores": [
    {"playerName": "SunnyPenguin", "score": 12},
    {"playerName": "CozyFox", "score": 18}
  ]
}
```

**Server → Alice & Bob (room state update):**
```json
{
  "type": "roomStateUpdate",
  "roomState": {
    "id": "ABC123",
    "players": [
      {
        "id": "player_ABC123_1", 
        "name": "SunnyPenguin",
        "totalScore": 12,
        "gamesPlayed": 1,
        "gamesWon": 1
      },
      {
        "id": "player_ABC123_2",
        "name": "CozyFox", 
        "totalScore": 18,
        "gamesPlayed": 1,
        "gamesWon": 0
      }
    ],
    "games": {},
    "gameHistory": [
      {
        "gameId": "GAME1",
        "winner": "SunnyPenguin",
        "finalScores": [
          {"playerName": "SunnyPenguin", "score": 12},
          {"playerName": "CozyFox", "score": 18}
        ],
        "completedAt": "2025-01-15T10:45:30Z"
      }
    ]
  }
}
```

### 8. Bob Starts New Game

**Bob → Server:**
```json
{
  "type": "joinGame",
  "roomId": "ABC123",
  "gameId": "GAME2"
}
```

**Server → Bob:**
```json
{
  "type": "roomJoined",
  "playerId": "player_ABC123_2",
  "roomState": {
    "games": {
      "GAME2": {
        "id": "GAME2",
        "players": [
          {
            "id": "player_ABC123_2",
            "name": "CozyFox"
          }
        ],
        "gamePhase": "waiting"
      }
    }
  }
}
```

### 9. Alice Joins New Game

**Alice → Server:**
```json
{
  "type": "joinGame", 
  "roomId": "ABC123",
  "gameId": "GAME2"
}
```

**Server → Alice:**
```json
{
  "type": "roomJoined",
  "playerId": "player_ABC123_1",
  "roomState": {
    "games": {
      "GAME2": {
        "players": [
          {"name": "CozyFox"},
          {"name": "SunnyPenguin"}
        ]
      }
    }
  }
}
```

### 10. Second Game Starts

**Bob → Server:**
```json
{"type": "startGame"}
```

**Server → Alice & Bob:**
```json
{"type": "gameStarted"}
```

**...Game continues with same message patterns as before...**

---

## Key Message Flow Patterns

### Room vs Game Messages
- **Room Operations**: Use `roomJoined` and `roomStateUpdate` 
- **Game Operations**: Use `gameState` and game-specific messages
- **Game Creation**: Automatically happens when joining non-existent gameId

### Player Context
- Players are **always in a room** (via GameContext.RoomID)
- Players **optionally in a game** (via GameContext.GameID)
- Room membership persists across multiple games

### Game Lifecycle
1. **Game Creation**: Implicit when first player joins gameId
2. **Game Population**: Players join existing gameId
3. **Game Start**: Explicit `startGame` message
4. **Game Play**: Standard golf message flow
5. **Game End**: Automatic cleanup, stats added to room

### Multi-Game Architecture Benefits
- **Room Persistence**: Player relationships and statistics maintained
- **Game Isolation**: Multiple concurrent games don't interfere
- **Flexible Joining**: Players can join different games as desired
- **Chat Ready**: Room context perfect for pre-game coordination