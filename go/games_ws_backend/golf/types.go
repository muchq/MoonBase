package golf

import (
	"encoding/json"
	"fmt"
	"math/rand"
	"time"
)

// Card represents a playing card
type Card struct {
	Rank string `json:"rank"`
	Suit string `json:"suit"`
}

// Player represents a player in the game
type Player struct {
	ID            string  `json:"id"`
	Name          string  `json:"name"`
	Cards         []*Card `json:"cards"`
	Score         int     `json:"score"`
	RevealedCards []int   `json:"revealedCards"`
	IsReady       bool    `json:"isReady"`
	HasPeeked     bool    `json:"hasPeeked"`
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
type CreateGameMessage struct {
	Type string `json:"type"`
}

type JoinGameMessage struct {
	Type   string `json:"type"`
	GameID string `json:"gameId"`
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

// Server-to-client message types
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

// Generic message for parsing
type IncomingMessage struct {
	Type      string `json:"type"`
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