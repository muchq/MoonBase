package main

import (
	"crypto/rand"
	"encoding/json"
	"fmt"
	"time"
)

// Position represents a 3D coordinate [x, y, z]
type Position [3]float64

// Color represents RGB values [r, g, b] as floats 0.0-1.0
type Color [3]float64

// Shape represents the 3D shape type (0=Sphere, 1=Cube, 2=Pyramid)
type Shape int

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
	Shape    Shape    `json:"shape"`
}

// PositionUpdateMessage represents a player position update
type PositionUpdateMessage struct {
	BaseMessage
	Position Position `json:"position"`
}

// ShapeUpdateMessage represents a player shape change
type ShapeUpdateMessage struct {
	BaseMessage
	Shape Shape `json:"shape"`
}

// PlayerLeaveMessage represents a player leaving the game
type PlayerLeaveMessage struct {
	BaseMessage
}

// WelcomeMessage represents the initial message sent to a new client
type WelcomeMessage struct {
	Type      string `json:"type"`
	PlayerID  string `json:"playerId"`
	Timestamp int64  `json:"timestamp"`
}

// GameStatePlayer represents a player in the game state
type GameStatePlayer struct {
	PlayerID string   `json:"playerId"`
	Position Position `json:"position"`
	Color    Color    `json:"color"`
	Shape    Shape    `json:"shape"`
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
	Shape    Shape
	Client   *Client
}

// GameMessage represents any incoming game message
type GameMessage struct {
	Type      string          `json:"type"`
	PlayerID  string          `json:"playerId"`
	Position  *Position       `json:"position,omitempty"`
	Color     *Color          `json:"color,omitempty"`
	Shape     *Shape          `json:"shape,omitempty"`
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

// ValidateShape checks if shape value is valid (0=Sphere, 1=Cube, 2=Pyramid)
func ValidateShape(shape Shape) error {
	if shape < 0 || shape > 2 {
		return fmt.Errorf("shape value %d out of range (0-2)", shape)
	}
	return nil
}

// PlayerIDGenerator defines the interface for generating player IDs
type PlayerIDGenerator interface {
	GenerateID() string
}

// RandomIDGenerator generates cryptographically secure random IDs for production
type RandomIDGenerator struct{}

// GenerateID creates a random player ID string using crypto/rand
func (g *RandomIDGenerator) GenerateID() string {
	const charset = "abcdefghijklmnopqrstuvwxyz0123456789"
	const length = 8
	
	bytes := make([]byte, length)
	if _, err := rand.Read(bytes); err != nil {
		// Fallback to time-based ID if crypto/rand fails
		return fmt.Sprintf("player-%d", time.Now().UnixNano()%1000000)
	}
	
	for i := range bytes {
		bytes[i] = charset[bytes[i]%byte(len(charset))]
	}
	
	return string(bytes)
}

// DeterministicIDGenerator generates predictable IDs for testing
type DeterministicIDGenerator struct {
	counter int
}

// GenerateID creates a deterministic player ID for testing
func (g *DeterministicIDGenerator) GenerateID() string {
	g.counter++
	return fmt.Sprintf("player-%d", g.counter)
}

// NewDeterministicIDGenerator creates a new deterministic ID generator
func NewDeterministicIDGenerator() *DeterministicIDGenerator {
	return &DeterministicIDGenerator{counter: 0}
}

// WhimsicalIDGenerator generates fun, memorable player IDs
type WhimsicalIDGenerator struct{}

var (
	whimsicalAdjectives = []string{
		"bouncy", "giggly", "sparkly", "fuzzy", "wiggly",
		"snuggly", "dreamy", "bubbly", "twinkly", "jolly",
		"quirky", "peppy", "zesty", "frisky", "silly",
		"perky", "cheeky", "zippy", "groovy", "jazzy",
	}
	
	whimsicalColors = []string{
		"lavender", "periwinkle", "coral", "mint", "peach",
		"turquoise", "magenta", "cerulean", "lilac", "salmon",
		"chartreuse", "crimson", "cobalt", "amber", "jade",
		"fuchsia", "indigo", "teal", "mauve", "vermillion",
	}
	
	cuteAustralianAnimals = []string{
		"koala", "kangaroo", "wombat", "quokka", "platypus",
		"echidna", "wallaby", "bilby", "numbat", "possum",
		"kookaburra", "cockatoo", "lorikeet", "galah", "budgie",
		"dingo", "bandicoot", "pademelon", "potoroo", "glider",
	}
)

// GenerateID creates a whimsical player ID in format: {adj}-{color}-{animal}-{4char}
func (g *WhimsicalIDGenerator) GenerateID() string {
	// Generate random indices for each component
	bytes := make([]byte, 7) // 3 for array indices + 4 for alphanumeric slug
	if _, err := rand.Read(bytes); err != nil {
		// Fallback to time-based ID if crypto/rand fails
		return fmt.Sprintf("player-%d", time.Now().UnixNano()%1000000)
	}
	
	// Select random words from each list
	adjective := whimsicalAdjectives[int(bytes[0])%len(whimsicalAdjectives)]
	color := whimsicalColors[int(bytes[1])%len(whimsicalColors)]
	animal := cuteAustralianAnimals[int(bytes[2])%len(cuteAustralianAnimals)]
	
	// Generate 4-character alphanumeric slug
	const charset = "abcdefghijklmnopqrstuvwxyz0123456789"
	slug := make([]byte, 4)
	for i := 0; i < 4; i++ {
		slug[i] = charset[bytes[3+i]%byte(len(charset))]
	}
	
	return fmt.Sprintf("%s-%s-%s-%s", adjective, color, animal, string(slug))
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
		Shape:    player.Shape,
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

// CreateShapeUpdateMessage creates a properly formatted shape update message
func CreateShapeUpdateMessage(playerID string, shape Shape) ([]byte, error) {
	msg := ShapeUpdateMessage{
		BaseMessage: BaseMessage{
			Type:      "shape_update",
			PlayerID:  playerID,
			Timestamp: time.Now().UnixMilli(),
		},
		Shape: shape,
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

// CreateWelcomeMessage creates a welcome message with assigned player ID
func CreateWelcomeMessage(playerID string) ([]byte, error) {
	msg := WelcomeMessage{
		Type:      "welcome",
		PlayerID:  playerID,
		Timestamp: time.Now().UnixMilli(),
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
			Shape:    player.Shape,
		})
	}
	
	msg := GameStateMessage{
		Type:      "game_state",
		Players:   gamePlayers,
		Timestamp: time.Now().UnixMilli(),
	}
	return json.Marshal(msg)
}