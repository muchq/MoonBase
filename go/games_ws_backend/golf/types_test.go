package golf

import (
	"encoding/json"
	"testing"
)

// Message Creation and Serialization Tests

func TestCreateGameMessage(t *testing.T) {
	msg := CreateGameMessage{
		Type: "createGame",
	}
	
	data, err := json.Marshal(msg)
	if err != nil {
		t.Fatalf("Failed to marshal CreateGameMessage: %v", err)
	}
	
	var parsed CreateGameMessage
	if err := json.Unmarshal(data, &parsed); err != nil {
		t.Fatalf("Failed to unmarshal CreateGameMessage: %v", err)
	}
	
	if parsed.Type != "createGame" {
		t.Errorf("Expected type 'createGame', got %s", parsed.Type)
	}
}

func TestJoinGameMessage(t *testing.T) {
	msg := JoinGameMessage{
		Type:   "joinGame",
		GameID: "ABC123",
	}
	
	data, err := json.Marshal(msg)
	if err != nil {
		t.Fatalf("Failed to marshal JoinGameMessage: %v", err)
	}
	
	var parsed JoinGameMessage
	if err := json.Unmarshal(data, &parsed); err != nil {
		t.Fatalf("Failed to unmarshal JoinGameMessage: %v", err)
	}
	
	if parsed.Type != "joinGame" {
		t.Errorf("Expected type 'joinGame', got %s", parsed.Type)
	}
	if parsed.GameID != "ABC123" {
		t.Errorf("Expected gameID 'ABC123', got %s", parsed.GameID)
	}
}

func TestGameJoinedMessage(t *testing.T) {
	gameState := &GameState{
		ID: "TEST123",
		Players: []*Player{
			{
				ID:            "player1",
				Name:          "Player 1",
				Cards:         CreateHiddenCards(),
				Score:         0,
				RevealedCards: []int{},
				IsReady:       false,
			},
		},
		CurrentPlayerIndex: 0,
		DrawPile:           44,
		DiscardPile:        []*Card{{Rank: "7", Suit: "♥"}},
		GamePhase:          "waiting",
		KnockedPlayerID:    nil,
		DrawnCard:          nil,
	}
	
	msg := GameJoinedMessage{
		Type:      "gameJoined",
		PlayerID:  "player1",
		GameState: gameState,
	}
	
	data, err := json.Marshal(msg)
	if err != nil {
		t.Fatalf("Failed to marshal GameJoinedMessage: %v", err)
	}
	
	var parsed GameJoinedMessage
	if err := json.Unmarshal(data, &parsed); err != nil {
		t.Fatalf("Failed to unmarshal GameJoinedMessage: %v", err)
	}
	
	if parsed.Type != "gameJoined" {
		t.Errorf("Expected type 'gameJoined', got %s", parsed.Type)
	}
	if parsed.PlayerID != "player1" {
		t.Errorf("Expected playerID 'player1', got %s", parsed.PlayerID)
	}
	if parsed.GameState.ID != "TEST123" {
		t.Errorf("Expected game ID 'TEST123', got %s", parsed.GameState.ID)
	}
}

func TestErrorMessage(t *testing.T) {
	msg := ErrorMessage{
		Type:    "error",
		Message: "Not your turn",
	}
	
	data, err := json.Marshal(msg)
	if err != nil {
		t.Fatalf("Failed to marshal ErrorMessage: %v", err)
	}
	
	var parsed ErrorMessage
	if err := json.Unmarshal(data, &parsed); err != nil {
		t.Fatalf("Failed to unmarshal ErrorMessage: %v", err)
	}
	
	if parsed.Type != "error" {
		t.Errorf("Expected type 'error', got %s", parsed.Type)
	}
	if parsed.Message != "Not your turn" {
		t.Errorf("Expected message 'Not your turn', got %s", parsed.Message)
	}
}

func TestTurnChangedMessage(t *testing.T) {
	msg := TurnChangedMessage{
		Type:       "turnChanged",
		PlayerName: "Player 2",
	}
	
	data, err := json.Marshal(msg)
	if err != nil {
		t.Fatalf("Failed to marshal TurnChangedMessage: %v", err)
	}
	
	var parsed TurnChangedMessage
	if err := json.Unmarshal(data, &parsed); err != nil {
		t.Fatalf("Failed to unmarshal TurnChangedMessage: %v", err)
	}
	
	if parsed.Type != "turnChanged" {
		t.Errorf("Expected type 'turnChanged', got %s", parsed.Type)
	}
	if parsed.PlayerName != "Player 2" {
		t.Errorf("Expected playerName 'Player 2', got %s", parsed.PlayerName)
	}
}

func TestGameEndedMessage(t *testing.T) {
	msg := GameEndedMessage{
		Type:   "gameEnded",
		Winner: "Player 1",
		FinalScores: []*FinalScore{
			{PlayerName: "Player 1", Score: 8},
			{PlayerName: "Player 2", Score: 15},
		},
	}
	
	data, err := json.Marshal(msg)
	if err != nil {
		t.Fatalf("Failed to marshal GameEndedMessage: %v", err)
	}
	
	var parsed GameEndedMessage
	if err := json.Unmarshal(data, &parsed); err != nil {
		t.Fatalf("Failed to unmarshal GameEndedMessage: %v", err)
	}
	
	if parsed.Type != "gameEnded" {
		t.Errorf("Expected type 'gameEnded', got %s", parsed.Type)
	}
	if parsed.Winner != "Player 1" {
		t.Errorf("Expected winner 'Player 1', got %s", parsed.Winner)
	}
	if len(parsed.FinalScores) != 2 {
		t.Fatalf("Expected 2 final scores, got %d", len(parsed.FinalScores))
	}
	if parsed.FinalScores[0].Score != 8 {
		t.Errorf("Expected Player 1 score 8, got %d", parsed.FinalScores[0].Score)
	}
}

func TestCardSerialization(t *testing.T) {
	card := &Card{
		Rank: "K",
		Suit: "♠",
	}
	
	data, err := json.Marshal(card)
	if err != nil {
		t.Fatalf("Failed to marshal Card: %v", err)
	}
	
	var parsed Card
	if err := json.Unmarshal(data, &parsed); err != nil {
		t.Fatalf("Failed to unmarshal Card: %v", err)
	}
	
	if parsed.Rank != "K" {
		t.Errorf("Expected rank 'K', got %s", parsed.Rank)
	}
	if parsed.Suit != "♠" {
		t.Errorf("Expected suit '♠', got %s", parsed.Suit)
	}
}

func TestPlayerSerialization(t *testing.T) {
	player := &Player{
		ID:   "player123",
		Name: "Test Player",
		Cards: []*Card{
			{Rank: "A", Suit: "♥"},
			{Rank: "7", Suit: "♦"},
			nil,
			nil,
		},
		Score:         8,
		RevealedCards: []int{0, 1},
		IsReady:       true,
	}
	
	data, err := json.Marshal(player)
	if err != nil {
		t.Fatalf("Failed to marshal Player: %v", err)
	}
	
	var parsed Player
	if err := json.Unmarshal(data, &parsed); err != nil {
		t.Fatalf("Failed to unmarshal Player: %v", err)
	}
	
	if parsed.ID != "player123" {
		t.Errorf("Expected ID 'player123', got %s", parsed.ID)
	}
	if parsed.Name != "Test Player" {
		t.Errorf("Expected name 'Test Player', got %s", parsed.Name)
	}
	if len(parsed.Cards) != 4 {
		t.Fatalf("Expected 4 cards, got %d", len(parsed.Cards))
	}
	if parsed.Cards[0] == nil || parsed.Cards[0].Rank != "A" {
		t.Error("First card not serialized correctly")
	}
	if parsed.Cards[2] != nil {
		t.Error("Nil card should remain nil")
	}
	if len(parsed.RevealedCards) != 2 {
		t.Errorf("Expected 2 revealed cards, got %d", len(parsed.RevealedCards))
	}
}

func TestGameStateSerialization(t *testing.T) {
	knockedID := "player1"
	gameState := &GameState{
		ID: "GAME123",
		Players: []*Player{
			{
				ID:            "player1",
				Name:          "Player 1",
				Cards:         CreateHiddenCards(),
				Score:         0,
				RevealedCards: []int{},
				IsReady:       true,
			},
			{
				ID:            "player2",
				Name:          "Player 2",
				Cards:         CreateHiddenCards(),
				Score:         0,
				RevealedCards: []int{0, 3},
				IsReady:       true,
			},
		},
		CurrentPlayerIndex: 1,
		DrawPile:           38,
		DiscardPile: []*Card{
			{Rank: "Q", Suit: "♣"},
		},
		GamePhase:       "knocked",
		KnockedPlayerID: &knockedID,
		DrawnCard:       &Card{Rank: "5", Suit: "♠"},
	}
	
	data, err := json.Marshal(gameState)
	if err != nil {
		t.Fatalf("Failed to marshal GameState: %v", err)
	}
	
	var parsed GameState
	if err := json.Unmarshal(data, &parsed); err != nil {
		t.Fatalf("Failed to unmarshal GameState: %v", err)
	}
	
	if parsed.ID != "GAME123" {
		t.Errorf("Expected ID 'GAME123', got %s", parsed.ID)
	}
	if len(parsed.Players) != 2 {
		t.Fatalf("Expected 2 players, got %d", len(parsed.Players))
	}
	if parsed.CurrentPlayerIndex != 1 {
		t.Errorf("Expected current player index 1, got %d", parsed.CurrentPlayerIndex)
	}
	if parsed.DrawPile != 38 {
		t.Errorf("Expected draw pile 38, got %d", parsed.DrawPile)
	}
	if len(parsed.DiscardPile) != 1 {
		t.Errorf("Expected 1 card in discard pile, got %d", len(parsed.DiscardPile))
	}
	if parsed.GamePhase != "knocked" {
		t.Errorf("Expected game phase 'knocked', got %s", parsed.GamePhase)
	}
	if parsed.KnockedPlayerID == nil || *parsed.KnockedPlayerID != "player1" {
		t.Error("Knocked player ID not serialized correctly")
	}
	if parsed.DrawnCard == nil || parsed.DrawnCard.Rank != "5" {
		t.Error("Drawn card not serialized correctly")
	}
}

func TestMessageRoundTrip(t *testing.T) {
	// Test that we can parse our own generated messages
	tests := []struct {
		name string
		msg  interface{}
		typ  string
	}{
		{
			name: "create game",
			msg:  &CreateGameMessage{Type: "createGame"},
			typ:  "createGame",
		},
		{
			name: "join game",
			msg:  &JoinGameMessage{Type: "joinGame", GameID: "XYZ789"},
			typ:  "joinGame",
		},
		{
			name: "start game",
			msg:  &StartGameMessage{Type: "startGame"},
			typ:  "startGame",
		},
		{
			name: "peek card",
			msg:  &PeekCardMessage{Type: "peekCard", CardIndex: 2},
			typ:  "peekCard",
		},
		{
			name: "draw card",
			msg:  &DrawCardMessage{Type: "drawCard"},
			typ:  "drawCard",
		},
		{
			name: "knock",
			msg:  &KnockMessage{Type: "knock"},
			typ:  "knock",
		},
	}
	
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			// Marshal to JSON
			data, err := json.Marshal(tt.msg)
			if err != nil {
				t.Fatalf("Failed to marshal message: %v", err)
			}
			
			// Parse as incoming message
			parsed, err := ParseIncomingMessage(data)
			if err != nil {
				t.Fatalf("Failed to parse message: %v", err)
			}
			
			if parsed.Type != tt.typ {
				t.Errorf("Expected type %s, got %s", tt.typ, parsed.Type)
			}
		})
	}
}

func TestCreateDeck(t *testing.T) {
	deck := CreateDeck()
	
	if len(deck) != 52 {
		t.Errorf("Expected 52 cards, got %d", len(deck))
	}
	
	// Check for duplicates
	cardMap := make(map[string]bool)
	for _, card := range deck {
		key := card.Rank + card.Suit
		if cardMap[key] {
			t.Errorf("Duplicate card found: %s%s", card.Rank, card.Suit)
		}
		cardMap[key] = true
	}
	
	// Check all suits and ranks are present
	suitCount := make(map[string]int)
	rankCount := make(map[string]int)
	
	for _, card := range deck {
		suitCount[card.Suit]++
		rankCount[card.Rank]++
	}
	
	for _, suit := range Suits {
		if suitCount[suit] != 13 {
			t.Errorf("Expected 13 cards of suit %s, got %d", suit, suitCount[suit])
		}
	}
	
	for _, rank := range Ranks {
		if rankCount[rank] != 4 {
			t.Errorf("Expected 4 cards of rank %s, got %d", rank, rankCount[rank])
		}
	}
}

func TestShuffleDeck(t *testing.T) {
	deck1 := CreateDeck()
	deck2 := CreateDeck()
	
	// Create copies for comparison
	original := make([]*Card, 52)
	copy(original, deck1)
	
	ShuffleDeck(deck1)
	ShuffleDeck(deck2)
	
	// Check that deck is shuffled (extremely unlikely to be in same order)
	sameOrder1 := true
	sameOrder2 := true
	
	for i := 0; i < 52; i++ {
		if original[i].Rank != deck1[i].Rank || original[i].Suit != deck1[i].Suit {
			sameOrder1 = false
		}
		if deck1[i].Rank != deck2[i].Rank || deck1[i].Suit != deck2[i].Suit {
			sameOrder2 = false
		}
	}
	
	if sameOrder1 {
		t.Error("Deck was not shuffled (same as original)")
	}
	
	if !sameOrder2 {
		// Good - two shuffles produced different results
	}
	
	// Verify all cards still present
	if len(deck1) != 52 {
		t.Errorf("Shuffled deck has wrong size: %d", len(deck1))
	}
}

func TestGeneratePlayerName(t *testing.T) {
	tests := []struct {
		playerNum int
		expected  string
	}{
		{1, "Player 1"},
		{2, "Player 2"},
		{3, "Player 3"},
		{4, "Player 4"},
		{100, "Player 100"},
	}
	
	for _, tt := range tests {
		name := GeneratePlayerName(tt.playerNum)
		if name != tt.expected {
			t.Errorf("GeneratePlayerName(%d) = %s, expected %s", 
				tt.playerNum, name, tt.expected)
		}
	}
}

func TestCalculatePlayerScore(t *testing.T) {
	player := &Player{
		Cards: []*Card{
			{Rank: "A", Suit: "♠"},  // 1
			{Rank: "5", Suit: "♥"},  // 5
			{Rank: "K", Suit: "♦"},  // 10
			{Rank: "7", Suit: "♣"},  // 7
		},
		RevealedCards: []int{0, 1, 2}, // Total: 1 + 5 + 10 = 16
	}
	
	score := CalculatePlayerScore(player)
	if score != 16 {
		t.Errorf("Expected score 16, got %d", score)
	}
	
	// Test with no revealed cards
	player.RevealedCards = []int{}
	score = CalculatePlayerScore(player)
	if score != 0 {
		t.Errorf("Expected score 0 with no revealed cards, got %d", score)
	}
	
	// Test with nil cards
	player.Cards[1] = nil
	player.RevealedCards = []int{0, 1, 2}
	score = CalculatePlayerScore(player)
	if score != 11 { // 1 + 0 + 10
		t.Errorf("Expected score 11 with nil card, got %d", score)
	}
}

func TestNilHandling(t *testing.T) {
	// Test that nil cards are properly handled in JSON
	player := &Player{
		ID:    "test",
		Name:  "Test",
		Cards: []*Card{nil, nil, nil, nil},
		Score: 0,
	}
	
	data, err := json.Marshal(player)
	if err != nil {
		t.Fatalf("Failed to marshal player with nil cards: %v", err)
	}
	
	var parsed Player
	if err := json.Unmarshal(data, &parsed); err != nil {
		t.Fatalf("Failed to unmarshal player with nil cards: %v", err)
	}
	
	for i, card := range parsed.Cards {
		if card != nil {
			t.Errorf("Expected nil card at index %d", i)
		}
	}
}