package main

import (
	"encoding/json"
	"fmt"
	"time"
)

// Position represents a 3D coordinate [x, y, z]
type Position [3]float64

// Color represents RGB values [r, g, b] as floats 0.0-1.0
type Color [3]float64

// BaseMessage contains common fields for all game messages
type BaseMessage struct {
	Type      string `json:"type"`
	PlayerID  string `json:"playerId"`
	Timestamp int64  `json:"timestamp"`
}

// PlayerJoinMessage represents a player joining the game
type PlayerJoinMessage struct {
	BaseMessage
	Position Position `json:"position"`
	Color    Color    `json:"color"`
}

// PositionUpdateMessage represents a player position update
type PositionUpdateMessage struct {
	BaseMessage
	Position Position `json:"position"`
}

// PlayerLeaveMessage represents a player leaving the game
type PlayerLeaveMessage struct {
	BaseMessage
}

// GameStatePlayer represents a player in the game state
type GameStatePlayer struct {
	PlayerID string   `json:"playerId"`
	Position Position `json:"position"`
	Color    Color    `json:"color"`
}

// GameStateMessage represents the full game state
type GameStateMessage struct {
	Type      string            `json:"type"`
	Players   []GameStatePlayer `json:"players"`
	Timestamp int64             `json:"timestamp"`
}

// Player represents a connected player's state
type Player struct {
	ID       string
	Position Position
	Color    Color
	Client   *Client
}

// GameMessage represents any incoming game message
type GameMessage struct {
	Type      string          `json:"type"`
	PlayerID  string          `json:"playerId"`
	Position  *Position       `json:"position,omitempty"`
	Color     *Color          `json:"color,omitempty"`
	Timestamp int64           `json:"timestamp"`
	RawData   json.RawMessage `json:"-"`
}

// ParseGameMessage parses a JSON message into a GameMessage
func ParseGameMessage(data []byte) (*GameMessage, error) {
	var msg GameMessage
	if err := json.Unmarshal(data, &msg); err != nil {
		return nil, fmt.Errorf("failed to parse JSON: %w", err)
	}
	
	msg.RawData = json.RawMessage(data)
	return &msg, nil
}

// ValidatePosition checks if position is within game boundaries (±50 units)
func ValidatePosition(pos Position) error {
	if pos[0] < -50 || pos[0] > 50 {
		return fmt.Errorf("x position %.2f out of bounds (±50)", pos[0])
	}
	if pos[2] < -50 || pos[2] > 50 {
		return fmt.Errorf("z position %.2f out of bounds (±50)", pos[2])
	}
	// Y should always be 0 according to spec
	if pos[1] != 0 {
		return fmt.Errorf("y position %.2f must be 0", pos[1])
	}
	return nil
}

// ValidateColor checks if color values are in valid range (0.0-1.0)
func ValidateColor(color Color) error {
	for i, val := range color {
		if val < 0.0 || val > 1.0 {
			return fmt.Errorf("color component %d value %.3f out of range (0.0-1.0)", i, val)
		}
	}
	return nil
}

// CreatePlayerJoinMessage creates a properly formatted player join message
func CreatePlayerJoinMessage(player *Player) ([]byte, error) {
	msg := PlayerJoinMessage{
		BaseMessage: BaseMessage{
			Type:      "player_join",
			PlayerID:  player.ID,
			Timestamp: time.Now().UnixMilli(),
		},
		Position: player.Position,
		Color:    player.Color,
	}
	return json.Marshal(msg)
}

// CreatePositionUpdateMessage creates a properly formatted position update message
func CreatePositionUpdateMessage(playerID string, position Position) ([]byte, error) {
	msg := PositionUpdateMessage{
		BaseMessage: BaseMessage{
			Type:      "position_update",
			PlayerID:  playerID,
			Timestamp: time.Now().UnixMilli(),
		},
		Position: position,
	}
	return json.Marshal(msg)
}

// CreatePlayerLeaveMessage creates a properly formatted player leave message
func CreatePlayerLeaveMessage(playerID string) ([]byte, error) {
	msg := PlayerLeaveMessage{
		BaseMessage: BaseMessage{
			Type:      "player_leave",
			PlayerID:  playerID,
			Timestamp: time.Now().UnixMilli(),
		},
	}
	return json.Marshal(msg)
}

// CreateGameStateMessage creates a full game state message
func CreateGameStateMessage(players map[string]*Player) ([]byte, error) {
	gamePlayers := make([]GameStatePlayer, 0, len(players))
	for _, player := range players {
		gamePlayers = append(gamePlayers, GameStatePlayer{
			PlayerID: player.ID,
			Position: player.Position,
			Color:    player.Color,
		})
	}
	
	msg := GameStateMessage{
		Type:      "game_state",
		Players:   gamePlayers,
		Timestamp: time.Now().UnixMilli(),
	}
	return json.Marshal(msg)
}