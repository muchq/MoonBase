package golf

import (
	"fmt"
	"testing"

	"github.com/muchq/moonbase/go/games_ws_backend/players"
)

// Helper function to add test players with consistent IDs
func addTestPlayerToGame(g *Game, clientID string) (*Player, error) {
	playerID := fmt.Sprintf("TestPlayer%s", clientID)
	return g.AddPlayer(clientID, playerID, playerID)
}

func TestNewGame(t *testing.T) {
	game := NewGame("TEST123", &players.DeterministicIDGenerator{})

	if game.state.ID != "TEST123" {
		t.Errorf("Expected game ID TEST123, got %s", game.state.ID)
	}

	if game.state.GamePhase != "waiting" {
		t.Errorf("Expected game phase waiting, got %s", game.state.GamePhase)
	}

	if len(game.state.Players) != 0 {
		t.Errorf("Expected 0 players, got %d", len(game.state.Players))
	}
}

func TestAddPlayer(t *testing.T) {
	game := NewGame("TEST123", &players.DeterministicIDGenerator{})

	player1, err := addTestPlayerToGame(game, "client1")
	if err != nil {
		t.Fatalf("Failed to add player1: %v", err)
	}

	if player1.Name != "TestPlayerclient1" {
		t.Errorf("Expected TestPlayerclient1, got %s", player1.Name)
	}

	player2, err := addTestPlayerToGame(game, "client2")
	if err != nil {
		t.Fatalf("Failed to add player2: %v", err)
	}

	if player2.Name != "TestPlayerclient2" {
		t.Errorf("Expected TestPlayerclient2, got %s", player2.Name)
	}

	if len(game.state.Players) != 2 {
		t.Errorf("Expected 2 players, got %d", len(game.state.Players))
	}
}

func TestStartGame(t *testing.T) {
	game := NewGame("TEST123", &players.DeterministicIDGenerator{})

	// Try to start with no players
	err := game.StartGame()
	if err == nil {
		t.Error("Expected error starting game with no players")
	}

	// Add players
	addTestPlayerToGame(game, "client1")
	addTestPlayerToGame(game, "client2")

	// Start game
	err = game.StartGame()
	if err != nil {
		t.Fatalf("Failed to start game: %v", err)
	}

	if game.state.GamePhase != "playing" {
		t.Errorf("Expected game phase playing, got %s", game.state.GamePhase)
	}

	// Check cards dealt
	for _, player := range game.state.Players {
		if len(player.Cards) != 4 {
			t.Errorf("Expected 4 cards for player %s, got %d", player.Name, len(player.Cards))
		}
	}

	// Check discard pile
	if len(game.state.DiscardPile) != 1 {
		t.Errorf("Expected 1 card in discard pile, got %d", len(game.state.DiscardPile))
	}
}

func TestCardValues(t *testing.T) {
	tests := []struct {
		card  *Card
		value int
	}{
		{&Card{Rank: "A", Suit: "♠"}, 1},
		{&Card{Rank: "2", Suit: "♥"}, 2},
		{&Card{Rank: "9", Suit: "♦"}, 9},
		{&Card{Rank: "10", Suit: "♣"}, 10},
		{&Card{Rank: "J", Suit: "♠"}, 0},
		{&Card{Rank: "Q", Suit: "♥"}, 10},
		{&Card{Rank: "K", Suit: "♦"}, 10},
	}

	for _, test := range tests {
		value := GetCardValue(test.card)
		if value != test.value {
			t.Errorf("Card %s%s: expected value %d, got %d",
				test.card.Rank, test.card.Suit, test.value, value)
		}
	}
}

func TestGenerateGameID(t *testing.T) {
	id := GenerateGameID()

	if len(id) != 6 {
		t.Errorf("Expected ID length 6, got %d", len(id))
	}

	// Check all characters are uppercase alphanumeric
	for _, char := range id {
		if !((char >= 'A' && char <= 'Z') || (char >= '0' && char <= '9')) {
			t.Errorf("Invalid character in game ID: %c", char)
		}
	}
}

// Game State Management Tests

func TestPeekCard(t *testing.T) {
	game := NewGame("TEST123", &players.DeterministicIDGenerator{})
	addTestPlayerToGame(game, "client1")
	addTestPlayerToGame(game, "client2")
	game.StartGame()

	// Test peeking at first card
	err := game.PeekCard("client1", 0)
	if err != nil {
		t.Fatalf("Failed to peek at card: %v", err)
	}

	player := game.state.Players[0]
	if len(player.RevealedCards) != 1 {
		t.Errorf("Expected 1 revealed card, got %d", len(player.RevealedCards))
	}

	// Test peeking at second card
	err = game.PeekCard("client1", 2)
	if err != nil {
		t.Fatalf("Failed to peek at second card: %v", err)
	}

	if len(player.RevealedCards) != 2 {
		t.Errorf("Expected 2 revealed cards, got %d", len(player.RevealedCards))
	}

	// Test peeking at third card (should fail)
	err = game.PeekCard("client1", 3)
	if err == nil {
		t.Error("Expected error when peeking at third card")
	}

	// Test peeking at same card twice
	err = game.PeekCard("client1", 0)
	if err == nil {
		t.Error("Expected error when peeking at same card twice")
	}

	// Test invalid card index
	err = game.PeekCard("client1", 5)
	if err == nil {
		t.Error("Expected error with invalid card index")
	}
}

func TestDrawCard(t *testing.T) {
	game := NewGame("TEST123", &players.DeterministicIDGenerator{})
	addTestPlayerToGame(game, "client1")
	addTestPlayerToGame(game, "client2")
	game.StartGame()

	// Test drawing when it's player's turn
	deckSize := game.state.DrawPile
	err := game.DrawCard("client1")
	if err != nil {
		t.Fatalf("Failed to draw card: %v", err)
	}

	if game.state.DrawnCard == nil {
		t.Error("Expected drawn card to be set")
	}

	if game.state.DrawPile != deckSize-1 {
		t.Errorf("Expected draw pile to decrease by 1")
	}

	// Test drawing again (should fail)
	err = game.DrawCard("client1")
	if err == nil {
		t.Error("Expected error when drawing with card already drawn")
	}

	// Test drawing when not player's turn
	err = game.DrawCard("client2")
	if err == nil {
		t.Error("Expected error when drawing on wrong turn")
	}
}

func TestTakeFromDiscard(t *testing.T) {
	game := NewGame("TEST123", &players.DeterministicIDGenerator{})
	addTestPlayerToGame(game, "client1")
	addTestPlayerToGame(game, "client2")
	game.StartGame()

	discardTop := game.state.DiscardPile[len(game.state.DiscardPile)-1]

	err := game.TakeFromDiscard("client1")
	if err != nil {
		t.Fatalf("Failed to take from discard: %v", err)
	}

	if game.state.DrawnCard == nil {
		t.Error("Expected drawn card to be set")
	}

	if game.state.DrawnCard.Rank != discardTop.Rank || game.state.DrawnCard.Suit != discardTop.Suit {
		t.Error("Drawn card doesn't match top of discard pile")
	}

	if len(game.state.DiscardPile) != 0 {
		t.Error("Expected discard pile to be empty after taking")
	}
}

func TestSwapCard(t *testing.T) {
	game := NewGame("TEST123", &players.DeterministicIDGenerator{})
	addTestPlayerToGame(game, "client1")
	addTestPlayerToGame(game, "client2")
	game.StartGame()

	// Draw a card first
	game.DrawCard("client1")
	drawnCard := game.state.DrawnCard

	// Get the card that will be swapped
	player := game.state.Players[0]
	originalCard := player.Cards[1]

	// Swap with card at index 1
	err := game.SwapCard("client1", 1)
	if err != nil {
		t.Fatalf("Failed to swap card: %v", err)
	}

	// Check swap occurred
	if player.Cards[1] != drawnCard {
		t.Error("Card was not swapped correctly")
	}

	// Check original card is now on discard pile
	if len(game.state.DiscardPile) == 0 {
		t.Fatal("Discard pile is empty")
	}

	topDiscard := game.state.DiscardPile[len(game.state.DiscardPile)-1]
	if topDiscard != originalCard {
		t.Error("Original card not placed on discard pile")
	}

	// Check drawn card is cleared
	if game.state.DrawnCard != nil {
		t.Error("Drawn card should be cleared after swap")
	}

	// Check turn advanced
	if game.state.CurrentPlayerIndex != 1 {
		t.Error("Turn did not advance to next player")
	}
}

func TestDiscardDrawn(t *testing.T) {
	game := NewGame("TEST123", &players.DeterministicIDGenerator{})
	addTestPlayerToGame(game, "client1")
	addTestPlayerToGame(game, "client2")
	game.StartGame()

	// Draw a card first
	game.DrawCard("client1")
	drawnCard := game.state.DrawnCard

	// Discard the drawn card
	err := game.DiscardDrawn("client1")
	if err != nil {
		t.Fatalf("Failed to discard drawn card: %v", err)
	}

	// Check card is on discard pile
	topDiscard := game.state.DiscardPile[len(game.state.DiscardPile)-1]
	if topDiscard != drawnCard {
		t.Error("Drawn card not placed on discard pile")
	}

	// Check drawn card is cleared
	if game.state.DrawnCard != nil {
		t.Error("Drawn card should be cleared after discard")
	}

	// Check turn advanced
	if game.state.CurrentPlayerIndex != 1 {
		t.Error("Turn did not advance to next player")
	}
}

func TestKnock(t *testing.T) {
	game := NewGame("TEST123", &players.DeterministicIDGenerator{})
	addTestPlayerToGame(game, "client1")
	addTestPlayerToGame(game, "client2")
	game.StartGame()

	// Test knocking
	err := game.Knock("client1")
	if err != nil {
		t.Fatalf("Failed to knock: %v", err)
	}

	if game.state.GamePhase != "knocked" {
		t.Errorf("Expected game phase 'knocked', got %s", game.state.GamePhase)
	}

	if game.state.KnockedPlayerID == nil || *game.state.KnockedPlayerID != "TestPlayerclient1" {
		t.Error("Knocked player ID not set correctly")
	}

	// Test knocking again (should fail)
	err = game.Knock("client2")
	if err == nil {
		t.Error("Expected error when knocking after someone already knocked")
	}

	// Test knocking after drawing (should fail)
	game = NewGame("TEST456", &players.DeterministicIDGenerator{})
	addTestPlayerToGame(game, "client1")
	addTestPlayerToGame(game, "client2")
	game.StartGame()
	game.DrawCard("client1")

	err = game.Knock("client1")
	if err == nil {
		t.Error("Expected error when knocking after drawing")
	}
}

func TestGameEnd(t *testing.T) {
	game := NewGame("TEST123", &players.DeterministicIDGenerator{})
	addTestPlayerToGame(game, "client1")
	addTestPlayerToGame(game, "client2")
	game.StartGame()

	// Player 1 knocks (it's their turn)
	err := game.Knock("client1")
	if err != nil {
		t.Fatalf("Failed to knock: %v", err)
	}

	// Turn should advance to player 2 since player 1 knocked
	if game.state.CurrentPlayerIndex != 1 {
		t.Errorf("Expected current player index 1, got %d", game.state.CurrentPlayerIndex)
	}

	// Player 2 takes their final turn
	err = game.DrawCard("client2")
	if err != nil {
		t.Fatalf("Failed to draw card: %v", err)
	}

	err = game.DiscardDrawn("client2")
	if err != nil {
		t.Fatalf("Failed to discard: %v", err)
	}

	// Game should be ended
	if game.state.GamePhase != "ended" {
		t.Errorf("Expected game phase 'ended', got %s", game.state.GamePhase)
	}

	// All cards should be revealed
	for _, player := range game.state.Players {
		if len(player.RevealedCards) != 4 {
			t.Errorf("Expected all 4 cards revealed for %s, got %d",
				player.Name, len(player.RevealedCards))
		}
	}

	// Scores should be calculated
	for _, player := range game.state.Players {
		if player.Score == 0 {
			t.Errorf("Expected non-zero score for %s", player.Name)
		}
	}
}

func TestGetWinner(t *testing.T) {
	game := NewGame("TEST123", &players.DeterministicIDGenerator{})
	addTestPlayerToGame(game, "client1")
	addTestPlayerToGame(game, "client2")
	game.StartGame()

	// Manually set up a winning scenario
	// Give player 1 low cards (Aces)
	game.state.Players[0].Cards = []*Card{
		{Rank: "A", Suit: "♠"},
		{Rank: "A", Suit: "♥"},
		{Rank: "A", Suit: "♦"},
		{Rank: "A", Suit: "♣"},
	}

	// Give player 2 high cards (Kings)
	game.state.Players[1].Cards = []*Card{
		{Rank: "K", Suit: "♠"},
		{Rank: "K", Suit: "♥"},
		{Rank: "K", Suit: "♦"},
		{Rank: "K", Suit: "♣"},
	}

	// End the game
	game.state.GamePhase = "ended"
	game.calculateFinalScores()

	winner := game.GetWinner()
	if winner == nil {
		t.Fatal("No winner returned")
	}

	if winner.Name != "TestPlayerclient1" {
		t.Errorf("Expected TestPlayerclient1 to win, got %s", winner.Name)
	}

	if winner.Score != 0 { // 4 Aces = 2 pairs that cancel out = 0 points
		t.Errorf("Expected winner score 0, got %d", winner.Score)
	}
}

func TestRemovePlayer(t *testing.T) {
	game := NewGame("TEST123", &players.DeterministicIDGenerator{})
	addTestPlayerToGame(game, "client1")
	addTestPlayerToGame(game, "client2")
	addTestPlayerToGame(game, "client3")

	if len(game.state.Players) != 3 {
		t.Fatalf("Expected 3 players, got %d", len(game.state.Players))
	}

	// Remove player 2
	err := game.RemovePlayer("client2")
	if err != nil {
		t.Fatalf("Failed to remove player: %v", err)
	}

	if len(game.state.Players) != 2 {
		t.Errorf("Expected 2 players after removal, got %d", len(game.state.Players))
	}

	// Check remaining players
	for _, player := range game.state.Players {
		if player.Name == "Player 2" {
			t.Error("Player 2 should have been removed")
		}
	}

	// Try to remove non-existent player
	err = game.RemovePlayer("client999")
	if err == nil {
		t.Error("Expected error when removing non-existent player")
	}
}

func TestValidateCardIndex(t *testing.T) {
	tests := []struct {
		index   int
		wantErr bool
	}{
		{0, false},
		{1, false},
		{2, false},
		{3, false},
		{4, true},
		{-1, true},
		{10, true},
	}

	for _, tt := range tests {
		err := ValidateCardIndex(tt.index)
		if (err != nil) != tt.wantErr {
			t.Errorf("ValidateCardIndex(%d) error = %v, wantErr %v",
				tt.index, err, tt.wantErr)
		}
	}
}

func TestTurnValidation(t *testing.T) {
	game := NewGame("TEST123", &players.DeterministicIDGenerator{})
	addTestPlayerToGame(game, "client1")
	addTestPlayerToGame(game, "client2")
	game.StartGame()

	// Player 2 tries to draw (not their turn)
	err := game.DrawCard("client2")
	if err == nil {
		t.Error("Expected error when player 2 draws on player 1's turn")
	}

	// Player 1 draws (their turn)
	err = game.DrawCard("client1")
	if err != nil {
		t.Errorf("Player 1 should be able to draw on their turn: %v", err)
	}

	// Complete player 1's turn
	game.DiscardDrawn("client1")

	// Now player 2 should be able to draw
	err = game.DrawCard("client2")
	if err != nil {
		t.Errorf("Player 2 should be able to draw on their turn: %v", err)
	}
}

func TestGamePhaseValidation(t *testing.T) {
	game := NewGame("TEST123", &players.DeterministicIDGenerator{})
	addTestPlayerToGame(game, "client1")

	// Try to draw before game starts
	err := game.DrawCard("client1")
	if err == nil {
		t.Error("Expected error when drawing before game starts")
	}

	// Try to peek before game starts
	err = game.PeekCard("client1", 0)
	if err == nil {
		t.Error("Expected error when peeking before game starts")
	}

	// Start game with only one player (should fail)
	err = game.StartGame()
	if err == nil {
		t.Error("Expected error starting game with only one player")
	}

	// Add second player and start
	addTestPlayerToGame(game, "client2")
	game.StartGame()

	// Now drawing should work
	err = game.DrawCard("client1")
	if err != nil {
		t.Errorf("Should be able to draw after game starts: %v", err)
	}
}

func TestMaxPlayers(t *testing.T) {
	game := NewGame("TEST123", &players.DeterministicIDGenerator{})

	// Add 4 players (max)
	for i := 0; i < 4; i++ {
		_, err := addTestPlayerToGame(game, string(rune('a' + i)))
		if err != nil {
			t.Fatalf("Failed to add player %d: %v", i+1, err)
		}
	}

	// Try to add 5th player
	_, err := addTestPlayerToGame(game, "client5")
	if err == nil {
		t.Error("Expected error when adding 5th player")
	}
}

func TestGetState(t *testing.T) {
	game := NewGame("TEST123", &players.DeterministicIDGenerator{})
	addTestPlayerToGame(game, "client1")
	addTestPlayerToGame(game, "client2")
	game.StartGame()

	// Get state copy
	stateCopy := game.GetState()

	// Verify it's a copy by modifying it
	stateCopy.CurrentPlayerIndex = 99

	// Original should be unchanged
	if game.state.CurrentPlayerIndex == 99 {
		t.Error("GetState returned reference instead of copy")
	}

	// Verify player data is copied
	if len(stateCopy.Players) != len(game.state.Players) {
		t.Error("Player count mismatch in state copy")
	}

	// Modify player in copy
	if len(stateCopy.Players) > 0 {
		stateCopy.Players[0].Name = "Modified"
		if game.state.Players[0].Name == "Modified" {
			t.Error("Player data not properly copied")
		}
	}
}

// Concurrent Access Tests

func TestConcurrentPlayerJoins(t *testing.T) {
	game := NewGame("CONCURRENT1", &players.DeterministicIDGenerator{})

	// Try to add 10 players concurrently (should only allow 4)
	type result struct {
		player *Player
		err    error
	}

	results := make(chan result, 10)

	for i := 0; i < 10; i++ {
		go func(clientID string) {
			player, err := addTestPlayerToGame(game, clientID)
			results <- result{player, err}
		}(string(rune('a' + i)))
	}

	// Collect results
	successCount := 0
	for i := 0; i < 10; i++ {
		res := <-results
		if res.err == nil {
			successCount++
		}
	}

	// Should have exactly 4 successful joins
	if successCount != 4 {
		t.Errorf("Expected 4 successful joins, got %d", successCount)
	}

	// Verify game has exactly 4 players
	if len(game.state.Players) != 4 {
		t.Errorf("Expected 4 players in game, got %d", len(game.state.Players))
	}
}

func TestConcurrentGameActions(t *testing.T) {
	game := NewGame("CONCURRENT2", &players.DeterministicIDGenerator{})
	addTestPlayerToGame(game, "client1")
	addTestPlayerToGame(game, "client2")
	game.StartGame()

	// Multiple goroutines trying to perform actions
	done := make(chan bool, 5)

	// Goroutine 1: Try to draw card
	go func() {
		game.DrawCard("client1")
		done <- true
	}()

	// Goroutine 2: Try to peek cards
	go func() {
		game.PeekCard("client1", 0)
		done <- true
	}()

	// Goroutine 3: Try to peek another card
	go func() {
		game.PeekCard("client1", 1)
		done <- true
	}()

	// Goroutine 4: Try to draw (wrong turn)
	go func() {
		game.DrawCard("client2")
		done <- true
	}()

	// Goroutine 5: Get state
	go func() {
		state := game.GetState()
		if state == nil {
			t.Error("GetState returned nil during concurrent access")
		}
		done <- true
	}()

	// Wait for all goroutines
	for i := 0; i < 5; i++ {
		<-done
	}

	// Verify game state is consistent
	state := game.GetState()
	if state == nil {
		t.Fatal("Game state is nil")
	}

	// Should have drawn card or not, but state should be consistent
	player1 := state.Players[0]
	if len(player1.RevealedCards) > 2 {
		t.Error("Player peeked at more than 2 cards")
	}
}

func TestConcurrentStateReads(t *testing.T) {
	game := NewGame("CONCURRENT3", &players.DeterministicIDGenerator{})
	addTestPlayerToGame(game, "client1")
	addTestPlayerToGame(game, "client2")
	game.StartGame()

	// Many concurrent reads
	done := make(chan bool, 100)

	for i := 0; i < 100; i++ {
		go func() {
			state := game.GetState()
			if state == nil {
				t.Error("GetState returned nil")
			}
			if len(state.Players) != 2 {
				t.Error("Inconsistent player count")
			}
			done <- true
		}()
	}

	// Wait for all reads
	for i := 0; i < 100; i++ {
		<-done
	}
}

func TestConcurrentTurnActions(t *testing.T) {
	game := NewGame("CONCURRENT4", &players.DeterministicIDGenerator{})
	addTestPlayerToGame(game, "client1")
	addTestPlayerToGame(game, "client2")
	game.StartGame()

	// Both players try to draw at the same time
	errors := make(chan error, 2)

	go func() {
		err := game.DrawCard("client1")
		errors <- err
	}()

	go func() {
		err := game.DrawCard("client2")
		errors <- err
	}()

	// Collect results
	err1 := <-errors
	err2 := <-errors

	// Exactly one should succeed (player 1's turn)
	if err1 == nil && err2 == nil {
		t.Error("Both players drew cards - turn validation failed")
	}

	if err1 != nil && err2 != nil {
		t.Error("Neither player could draw - expected player 1 to succeed")
	}

	// Verify only one card was drawn
	if game.state.DrawnCard == nil {
		t.Error("No card was drawn")
	}
}

func TestConcurrentKnocking(t *testing.T) {
	game := NewGame("CONCURRENT5", &players.DeterministicIDGenerator{})
	addTestPlayerToGame(game, "client1")
	addTestPlayerToGame(game, "client2")
	addTestPlayerToGame(game, "client3")
	game.StartGame()

	// Multiple players try to knock simultaneously
	results := make(chan error, 3)

	go func() {
		results <- game.Knock("client1")
	}()

	go func() {
		// Player 2 draws first (not their turn, should fail)
		game.DrawCard("client2")
		results <- game.Knock("client2")
	}()

	go func() {
		// Player 3 just tries to knock
		results <- game.Knock("client3")
	}()

	// Collect results
	successCount := 0
	for i := 0; i < 3; i++ {
		if err := <-results; err == nil {
			successCount++
		}
	}

	// Only one player should successfully knock
	if successCount != 1 {
		t.Errorf("Expected 1 successful knock, got %d", successCount)
	}

	// Verify game is in knocked phase
	if game.state.GamePhase != "knocked" {
		t.Errorf("Expected game phase 'knocked', got %s", game.state.GamePhase)
	}

	// Verify knocked player is set
	if game.state.KnockedPlayerID == nil {
		t.Error("Knocked player ID not set")
	}
}

func TestScoreVisibilityDuringGame(t *testing.T) {
	game := NewGame("TEST_SCORE", &players.DeterministicIDGenerator{})
	addTestPlayerToGame(game, "client1")
	addTestPlayerToGame(game, "client2")
	game.StartGame()

	// Get state for player 1 during gameplay
	state := game.GetStateForPlayer("client1")

	// All players should have score 0 during gameplay
	for _, player := range state.Players {
		if player.Score != 0 {
			t.Errorf("Player %s has score %d during gameplay, expected 0", player.Name, player.Score)
		}
	}

	// Player 1 peeks at cards
	game.PeekCard("client1", 0)
	game.PeekCard("client1", 1)

	// Get state again - scores should still be hidden
	state = game.GetStateForPlayer("client1")
	for _, player := range state.Players {
		if player.Score != 0 {
			t.Errorf("Player %s has score %d after peeking, expected 0", player.Name, player.Score)
		}
	}

	// Player 1 knocks
	game.Knock("client1")

	// Player 2 takes final turn
	game.DrawCard("client2")
	game.DiscardDrawn("client2")

	// Game should be ended now
	if game.state.GamePhase != "ended" {
		t.Fatalf("Expected game to be ended, but phase is %s", game.state.GamePhase)
	}

	// Get state after game end - scores should now be visible
	state = game.GetStateForPlayer("client1")
	hasNonZeroScore := false
	for _, player := range state.Players {
		if player.Score != 0 {
			hasNonZeroScore = true
		}
	}

	if !hasNonZeroScore {
		t.Error("All scores are still 0 after game ended - scores should be visible")
	}

	// Verify internal state has actual scores
	for _, player := range game.state.Players {
		if player.Score == 0 {
			// This might be legitimate if they have pairs that cancel, but unlikely for both players
			t.Logf("Warning: Player %s has score 0 in internal state", player.Name)
		}
	}
}

func TestRaceConditionProtection(t *testing.T) {
	game := NewGame("RACE1", &players.DeterministicIDGenerator{})

	// Start many operations concurrently
	done := make(chan bool, 20)

	// Add players
	for i := 0; i < 5; i++ {
		go func(id string) {
			addTestPlayerToGame(game, id)
			done <- true
		}(string(rune('a' + i)))
	}

	// Try to start game multiple times
	for i := 0; i < 5; i++ {
		go func() {
			game.StartGame()
			done <- true
		}()
	}

	// Try to remove players
	for i := 0; i < 5; i++ {
		go func(id string) {
			game.RemovePlayer(id)
			done <- true
		}(string(rune('a' + i)))
	}

	// Get state many times
	for i := 0; i < 5; i++ {
		go func() {
			game.GetState()
			done <- true
		}()
	}

	// Wait for all operations
	for i := 0; i < 20; i++ {
		<-done
	}

	// Game should still be in a valid state
	state := game.GetState()
	if state == nil {
		t.Fatal("Game state is nil after concurrent operations")
	}

	// Player count should be reasonable (0-4)
	if len(state.Players) > 4 {
		t.Errorf("Too many players: %d", len(state.Players))
	}
}

func TestDrawnCardPrivacy(t *testing.T) {
	game := NewGame("TEST_PRIVACY", &players.DeterministicIDGenerator{})
	addTestPlayerToGame(game, "client1")
	addTestPlayerToGame(game, "client2")
	addTestPlayerToGame(game, "client3")
	game.StartGame()

	// Player 1 draws a card
	err := game.DrawCard("client1")
	if err != nil {
		t.Fatalf("Failed to draw card: %v", err)
	}

	// Verify that the drawn card exists in the game state
	if game.state.DrawnCard == nil {
		t.Fatal("No drawn card in game state after drawing")
	}

	// Get the actual drawn card for comparison
	actualDrawnCard := game.state.DrawnCard

	// Get state for player 1 (who drew the card)
	stateForPlayer1 := game.GetStateForPlayer("client1")
	if stateForPlayer1.DrawnCard == nil {
		t.Error("Player 1 should see the drawn card")
	}
	if stateForPlayer1.DrawnCard != actualDrawnCard {
		t.Error("Player 1 should see the correct drawn card")
	}

	// Get state for player 2 (who did not draw)
	stateForPlayer2 := game.GetStateForPlayer("client2")
	if stateForPlayer2.DrawnCard != nil {
		t.Errorf("Player 2 should NOT see the drawn card, but sees: %v", stateForPlayer2.DrawnCard)
	}

	// Get state for player 3 (who did not draw)
	stateForPlayer3 := game.GetStateForPlayer("client3")
	if stateForPlayer3.DrawnCard != nil {
		t.Errorf("Player 3 should NOT see the drawn card, but sees: %v", stateForPlayer3.DrawnCard)
	}

	// Complete the turn
	err = game.DiscardDrawn("client1")
	if err != nil {
		t.Fatalf("Failed to discard drawn card: %v", err)
	}

	// Now it's player 2's turn, they draw a card
	err = game.DrawCard("client2")
	if err != nil {
		t.Fatalf("Failed to draw card for player 2: %v", err)
	}

	// Verify the new drawn card exists
	if game.state.DrawnCard == nil {
		t.Fatal("No drawn card in game state after player 2 draws")
	}
	newDrawnCard := game.state.DrawnCard

	// Get state for each player again
	stateForPlayer1 = game.GetStateForPlayer("client1")
	stateForPlayer2 = game.GetStateForPlayer("client2")
	stateForPlayer3 = game.GetStateForPlayer("client3")

	// Player 1 should NOT see the card drawn by player 2
	if stateForPlayer1.DrawnCard != nil {
		t.Errorf("Player 1 should NOT see the card drawn by player 2, but sees: %v", stateForPlayer1.DrawnCard)
	}

	// Player 2 should see their own drawn card
	if stateForPlayer2.DrawnCard == nil {
		t.Error("Player 2 should see their own drawn card")
	}
	if stateForPlayer2.DrawnCard != newDrawnCard {
		t.Error("Player 2 should see the correct drawn card")
	}

	// Player 3 should NOT see the card drawn by player 2
	if stateForPlayer3.DrawnCard != nil {
		t.Errorf("Player 3 should NOT see the card drawn by player 2, but sees: %v", stateForPlayer3.DrawnCard)
	}

	// Test with taking from discard pile
	err = game.DiscardDrawn("client2")
	if err != nil {
		t.Fatalf("Failed to discard for player 2: %v", err)
	}

	// Player 3's turn - they take from discard
	err = game.TakeFromDiscard("client3")
	if err != nil {
		t.Fatalf("Failed to take from discard: %v", err)
	}

	// Verify drawn card privacy when taken from discard
	stateForPlayer1 = game.GetStateForPlayer("client1")
	stateForPlayer2 = game.GetStateForPlayer("client2")
	stateForPlayer3 = game.GetStateForPlayer("client3")

	// Only player 3 should see the drawn card
	if stateForPlayer1.DrawnCard != nil {
		t.Error("Player 1 should NOT see card taken from discard by player 3")
	}
	if stateForPlayer2.DrawnCard != nil {
		t.Error("Player 2 should NOT see card taken from discard by player 3")
	}
	if stateForPlayer3.DrawnCard == nil {
		t.Error("Player 3 should see the card they took from discard")
	}
}
