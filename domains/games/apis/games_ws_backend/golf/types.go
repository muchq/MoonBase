package golf

import (
	"encoding/json"
	"fmt"
	"math/rand"
	"time"
)

// Session token constants
const (
	// SessionTokenLifetime is how long a session token remains valid
	SessionTokenLifetime = 24 * time.Hour

	// ReconnectGracePeriod is how long to wait before cleaning up disconnected players
	ReconnectGracePeriod = 30 * time.Second
)

// Room represents a persistent room where multiple games can be played
type Room struct {
	ID              string                 `json:"id"`
	Players         []*Player              `json:"players"`
	Games           map[string]*Game       `json:"games"` // Active games mapped by game ID
	GameHistory     []*GameResult          `json:"gameHistory"`
	CreatedAt       time.Time              `json:"createdAt"`
	LastActivity    time.Time              `json:"lastActivity"`
}

// MarshalJSON implements custom JSON marshaling for Room
// This ensures that Games are serialized using their GetState() method
func (r *Room) MarshalJSON() ([]byte, error) {
	// Create a temporary struct for JSON serialization
	type Alias Room
	
	// Convert Games map to GameState map
	gameStates := make(map[string]*GameState)
	for gameID, game := range r.Games {
		if game != nil {
			gameStates[gameID] = game.GetState()
		}
	}
	
	// Create the JSON representation
	return json.Marshal(&struct {
		*Alias
		Games map[string]*GameState `json:"games"`
	}{
		Alias: (*Alias)(r),
		Games: gameStates,
	})
}

// ClientContext holds the complete context for a client including room and game state
type ClientContext struct {
	RoomID       string    `json:"roomId"`       // Room the client is in (empty if not in room)
	GameID       string    `json:"gameId"`       // Game the client is in (empty if not in specific game)
	PlayerID     string    `json:"playerId"`     // Player ID for faster lookups
	SessionToken string    `json:"sessionToken"` // Session token for reconnection
	TokenExpiry  time.Time `json:"tokenExpiry"`  // When the session token expires
	JoinedAt     time.Time `json:"joinedAt"`     // When client joined the room
	LastAction   time.Time `json:"lastAction"`   // Last action timestamp
}

// GameResult stores the outcome of a completed game
type GameResult struct {
	GameID          string        `json:"gameId"`
	Winner          string        `json:"winner"`
	FinalScores     []*FinalScore `json:"finalScores"`
	CompletedAt     time.Time     `json:"completedAt"`
}

// Card represents a playing card
type Card struct {
	Rank string `json:"rank"`
	Suit string `json:"suit"`
}

// Player represents a player in the game and room
type Player struct {
	// Game-specific fields
	ID            string  `json:"id"`
	Name          string  `json:"name"`
	Cards         []*Card `json:"cards"`
	Score         int     `json:"score"`
	RevealedCards []int   `json:"revealedCards"`
	IsReady       bool    `json:"isReady"`
	HasPeeked     bool    `json:"hasPeeked"`

	// Room/persistence fields
	ClientID       string     `json:"clientId"`
	TotalScore     int        `json:"totalScore"`     // Running total across all games
	GamesPlayed    int        `json:"gamesPlayed"`
	GamesWon       int        `json:"gamesWon"`
	IsConnected    bool       `json:"isConnected"`
	DisconnectedAt *time.Time `json:"disconnectedAt"` // When player disconnected (nil if connected)
	JoinedAt       time.Time  `json:"joinedAt"`
}

// GameState represents the full game state
type GameState struct {
	ID                 string    `json:"id"`
	Players            []*Player `json:"players"`
	CurrentPlayerIndex int       `json:"currentPlayerIndex"`
	DrawPile           int       `json:"drawPile"`
	DiscardPile        []*Card   `json:"discardPile"`
	GamePhase          string    `json:"gamePhase"` // waiting, playing, peeking, knocked, ended
	KnockedPlayerID    *string   `json:"knockedPlayerID"`
	DrawnCard          *Card     `json:"drawnCard"`
	PeekedAtDrawPile   bool      `json:"peekedAtDrawPile"`
	AllPlayersPeeked   bool      `json:"allPlayersPeeked"`
}

// Client-to-server message types

// AuthenticateMessage is sent as the first message after WebSocket connection
type AuthenticateMessage struct {
	Type         string `json:"type"` // "authenticate"
	SessionToken string `json:"sessionToken,omitempty"` // Empty for new session, provided for reconnect
}

type CreateRoomMessage struct {
	Type string `json:"type"`
}

type CreateGameMessage struct {
	Type   string `json:"type"`
	RoomID string `json:"roomId"`
}

type JoinGameMessage struct {
	Type   string `json:"type"`
	RoomID string `json:"roomId"`
	GameID string `json:"gameId"` // Required - must specify which game to join
}

type StartGameMessage struct {
	Type string `json:"type"`
}

type PeekCardMessage struct {
	Type      string `json:"type"`
	CardIndex int    `json:"cardIndex"`
}

type DrawCardMessage struct {
	Type string `json:"type"`
}

type TakeFromDiscardMessage struct {
	Type string `json:"type"`
}

type SwapCardMessage struct {
	Type      string `json:"type"`
	CardIndex int    `json:"cardIndex"`
}

type DiscardDrawnMessage struct {
	Type string `json:"type"`
}

type KnockMessage struct {
	Type string `json:"type"`
}

type HideCardsMessage struct {
	Type string `json:"type"`
}

type StartNewGameMessage struct {
	Type string `json:"type"`
}

type GetRoomStateMessage struct {
	Type string `json:"type"`
}

// New message types for multi-game support
type ListGamesMessage struct {
	Type string `json:"type"`
}

type CreateGameInRoomMessage struct {
	Type string `json:"type"`
}

type LeaveGameMessage struct {
	Type string `json:"type"`
}

// Server-to-client message types

// AuthenticatedMessage is sent after successful authentication
type AuthenticatedMessage struct {
	Type         string `json:"type"` // "authenticated"
	SessionToken string `json:"sessionToken"` // Session token for future reconnects
	Reconnected  bool   `json:"reconnected"` // True if this was a reconnection
}

type GameJoinedMessage struct {
	Type      string     `json:"type"`
	PlayerID  string     `json:"playerId"`
	GameState *GameState `json:"gameState"`
}

type GameStateUpdateMessage struct {
	Type      string     `json:"type"`
	GameState *GameState `json:"gameState"`
}

type ErrorMessage struct {
	Type    string `json:"type"`
	Message string `json:"message"`
}

type GameStartedMessage struct {
	Type string `json:"type"`
}

type TurnChangedMessage struct {
	Type       string `json:"type"`
	PlayerName string `json:"playerName"`
}

type PlayerKnockedMessage struct {
	Type       string `json:"type"`
	PlayerName string `json:"playerName"`
}

type FinalScore struct {
	PlayerName string `json:"playerName"`
	Score      int    `json:"score"`
}

type GameEndedMessage struct {
	Type        string        `json:"type"`
	Winner      string        `json:"winner"`
	FinalScores []*FinalScore `json:"finalScores"`
}

type RoomJoinedMessage struct {
	Type         string `json:"type"`
	PlayerID     string `json:"playerId"`
	SessionToken string `json:"sessionToken"` // Session token for reconnection
	RoomState    *Room  `json:"roomState"`
}

type RoomStateUpdateMessage struct {
	Type      string `json:"type"`
	RoomState *Room  `json:"roomState"`
}

type NewGameStartedMessage struct {
	Type           string `json:"type"`
	GameID         string `json:"gameId"`
	PreviousGameID string `json:"previousGameId,omitempty"`
}

// Server-to-client message types for multi-game support
type GameListMessage struct {
	Type  string            `json:"type"`
	Games map[string]*Game  `json:"games"` // Games in current room
}

type GameListUpdateMessage struct {
	Type   string `json:"type"`
	Action string `json:"action"` // "added", "removed", "updated"
	GameID string `json:"gameId"`
}

// Generic message for parsing
type IncomingMessage struct {
	Type      string `json:"type"`
	RoomID    string `json:"roomId,omitempty"`
	GameID    string `json:"gameId,omitempty"`
	CardIndex int    `json:"cardIndex,omitempty"`
}

// Card constants
var (
	Suits = []string{"♠", "♥", "♦", "♣"}
	Ranks = []string{"A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"}
)

// GetCardValue returns the point value of a card
func GetCardValue(card *Card) int {
	switch card.Rank {
	case "A":
		return 1
	case "J":
		return 0 // Jack is worth 0 points
	case "Q", "K":
		return 10
	default:
		// Parse numeric ranks
		value := 0
		fmt.Sscanf(card.Rank, "%d", &value)
		return value
	}
}

// CreateDeck creates a standard 52-card deck
func CreateDeck() []*Card {
	deck := make([]*Card, 0, 52)
	for _, suit := range Suits {
		for _, rank := range Ranks {
			deck = append(deck, &Card{Rank: rank, Suit: suit})
		}
	}
	return deck
}

// ShuffleDeck shuffles a deck of cards
func ShuffleDeck(deck []*Card) {
	r := rand.New(rand.NewSource(time.Now().UnixNano()))
	r.Shuffle(len(deck), func(i, j int) {
		deck[i], deck[j] = deck[j], deck[i]
	})
}

// GenerateGameID generates a 6-character uppercase alphanumeric game ID
func GenerateGameID() string {
	const charset = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
	b := make([]byte, 6)
	for i := range b {
		b[i] = charset[rand.Intn(len(charset))]
	}
	return string(b)
}

// GenerateRoomID generates a 6-character uppercase alphanumeric room ID
func GenerateRoomID() string {
	return GenerateGameID() // Same format as game ID
}

// GeneratePlayerName generates a simple player name
func GeneratePlayerName(playerNumber int) string {
	return fmt.Sprintf("Player %d", playerNumber)
}

// ValidateCardIndex checks if a card index is valid
func ValidateCardIndex(index int) error {
	if index < 0 || index > 3 {
		return fmt.Errorf("invalid card index: %d", index)
	}
	return nil
}

// CalculatePlayerScore calculates the score for a player's revealed cards
func CalculatePlayerScore(player *Player) int {
	score := 0
	for _, idx := range player.RevealedCards {
		if idx >= 0 && idx < len(player.Cards) && player.Cards[idx] != nil {
			score += GetCardValue(player.Cards[idx])
		}
	}
	return score
}

// CreateHiddenCards creates an array of 4 nil cards for a player
func CreateHiddenCards() []*Card {
	return make([]*Card, 4)
}

// ParseIncomingMessage parses a generic incoming message
func ParseIncomingMessage(data []byte) (*IncomingMessage, error) {
	var msg IncomingMessage
	if err := json.Unmarshal(data, &msg); err != nil {
		return nil, fmt.Errorf("failed to parse message: %w", err)
	}
	return &msg, nil
}

// GetPlayerByClientID returns the player associated with a client ID
func (r *Room) GetPlayerByClientID(clientID string) *Player {
	for _, player := range r.Players {
		if player.ClientID == clientID {
			return player
		}
	}
	return nil
}