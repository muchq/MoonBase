package golf

import (
	"fmt"
	"sync"

	"github.com/muchq/moonbase/go/games_ws_backend/players"
)

// Game represents a single golf game instance
type Game struct {
	mu               sync.RWMutex
	state            *GameState
	deck             []*Card
	playersByClient  map[string]*Player // client ID -> player
	finalRoundPlayed map[string]bool    // track who has played their final turn
	idGenerator      players.PlayerIDGenerator
}

// NewGame creates a new game instance
func NewGame(gameID string, idGenerator players.PlayerIDGenerator) *Game {
	return &Game{
		state: &GameState{
			ID:                 gameID,
			Players:            make([]*Player, 0),
			CurrentPlayerIndex: 0,
			DrawPile:           0,
			DiscardPile:        make([]*Card, 0),
			GamePhase:          "waiting",
			KnockedPlayerID:    nil,
			DrawnCard:          nil,
			PeekedAtDrawPile:   false,
		},
		playersByClient:  make(map[string]*Player),
		finalRoundPlayed: make(map[string]bool),
		idGenerator:      idGenerator,
	}
}

// AddPlayer adds a new player to the game
func (g *Game) AddPlayer(clientID string) (*Player, error) {
	g.mu.Lock()
	defer g.mu.Unlock()

	if g.state.GamePhase != "waiting" {
		return nil, fmt.Errorf("game already started")
	}

	if len(g.state.Players) >= 4 {
		return nil, fmt.Errorf("game is full")
	}

	// Generate player ID and name
	playerNum := len(g.state.Players) + 1
	playerID := fmt.Sprintf("player_%s_%d", g.state.ID, playerNum)
	playerName := g.idGenerator.GenerateID()

	player := &Player{
		ID:            playerID,
		Name:          playerName,
		Cards:         CreateHiddenCards(),
		Score:         0,
		RevealedCards: make([]int, 0),
		IsReady:       false,
		HasPeeked:     false,
	}

	g.state.Players = append(g.state.Players, player)
	g.playersByClient[clientID] = player

	return player, nil
}

// RemovePlayer removes a player from the game
func (g *Game) RemovePlayer(clientID string) error {
	g.mu.Lock()
	defer g.mu.Unlock()

	player, exists := g.playersByClient[clientID]
	if !exists {
		return fmt.Errorf("player not found")
	}

	// Remove from players list
	for i, p := range g.state.Players {
		if p.ID == player.ID {
			g.state.Players = append(g.state.Players[:i], g.state.Players[i+1:]...)
			break
		}
	}

	delete(g.playersByClient, clientID)

	// If game is in progress and it's this player's turn, advance to next player
	if g.state.GamePhase == "playing" && len(g.state.Players) > 0 {
		g.state.CurrentPlayerIndex = g.state.CurrentPlayerIndex % len(g.state.Players)
	}

	return nil
}

// StartGame initializes the game with dealt cards
func (g *Game) StartGame() error {
	g.mu.Lock()
	defer g.mu.Unlock()

	if g.state.GamePhase != "waiting" {
		return fmt.Errorf("game already started")
	}

	if len(g.state.Players) < 2 {
		return fmt.Errorf("need at least 2 players to start")
	}

	// Create and shuffle deck
	g.deck = CreateDeck()
	ShuffleDeck(g.deck)

	// Deal 4 cards to each player
	for _, player := range g.state.Players {
		for i := 0; i < 4; i++ {
			if len(g.deck) > 0 {
				player.Cards[i] = g.deck[0]
				g.deck = g.deck[1:]
			}
		}
	}

	// Set up discard pile with one card
	if len(g.deck) > 0 {
		g.state.DiscardPile = append(g.state.DiscardPile, g.deck[0])
		g.deck = g.deck[1:]
	}

	g.state.DrawPile = len(g.deck)
	g.state.GamePhase = "playing"
	g.state.CurrentPlayerIndex = 0

	return nil
}

// PeekCard allows a player to peek at one of their cards
func (g *Game) PeekCard(clientID string, cardIndex int) error {
	g.mu.Lock()
	defer g.mu.Unlock()

	player, exists := g.playersByClient[clientID]
	if !exists {
		return fmt.Errorf("player not found")
	}

	if err := ValidateCardIndex(cardIndex); err != nil {
		return err
	}

	if g.state.GamePhase != "playing" && g.state.GamePhase != "peeking" {
		return fmt.Errorf("can only peek during playing phase")
	}

	// Check if player has already peeked at 2 cards
	if len(player.RevealedCards) >= 2 {
		return fmt.Errorf("already peeked at 2 cards")
	}

	// Check if already peeked at this card
	for _, idx := range player.RevealedCards {
		if idx == cardIndex {
			return fmt.Errorf("already peeked at this card")
		}
	}

	player.RevealedCards = append(player.RevealedCards, cardIndex)
	player.Score = CalculatePlayerScore(player)

	// Mark that player has peeked
	if len(player.RevealedCards) == 2 {
		player.HasPeeked = true

		// Check if all players have peeked
		allPeeked := true
		for _, p := range g.state.Players {
			if !p.HasPeeked {
				allPeeked = false
				break
			}
		}

		if allPeeked {
			// Set the flag that all players have peeked
			g.state.GamePhase = "peeking"
			g.state.AllPlayersPeeked = true
		}
	}

	return nil
}

// DrawCard draws a card from the deck
func (g *Game) DrawCard(clientID string) error {
	g.mu.Lock()
	defer g.mu.Unlock()

	if err := g.validateTurn(clientID); err != nil {
		return err
	}

	if g.state.DrawnCard != nil {
		return fmt.Errorf("already have a drawn card")
	}

	if len(g.deck) == 0 {
		return fmt.Errorf("deck is empty")
	}

	g.state.DrawnCard = g.deck[0]
	g.deck = g.deck[1:]
	g.state.DrawPile = len(g.deck)
	g.state.PeekedAtDrawPile = true

	return nil
}

// TakeFromDiscard takes the top card from the discard pile
func (g *Game) TakeFromDiscard(clientID string) error {
	g.mu.Lock()
	defer g.mu.Unlock()

	if err := g.validateTurn(clientID); err != nil {
		return err
	}

	if g.state.DrawnCard != nil {
		return fmt.Errorf("already have a drawn card")
	}

	if len(g.state.DiscardPile) == 0 {
		return fmt.Errorf("discard pile is empty")
	}

	if g.state.PeekedAtDrawPile {
		return fmt.Errorf("cannot swap for discard after peeking")
	}

	// Take the top card
	g.state.DrawnCard = g.state.DiscardPile[len(g.state.DiscardPile)-1]
	g.state.DiscardPile = g.state.DiscardPile[:len(g.state.DiscardPile)-1]

	return nil
}

// SwapCard swaps the drawn card with one of the player's cards
func (g *Game) SwapCard(clientID string, cardIndex int) error {
	g.mu.Lock()
	defer g.mu.Unlock()

	if err := g.validateTurn(clientID); err != nil {
		return err
	}

	if g.state.DrawnCard == nil {
		return fmt.Errorf("no drawn card to swap")
	}

	if err := ValidateCardIndex(cardIndex); err != nil {
		return err
	}

	player := g.playersByClient[clientID]

	// Swap cards
	oldCard := player.Cards[cardIndex]
	player.Cards[cardIndex] = g.state.DrawnCard
	g.state.DrawnCard = nil

	// Add old card to discard pile
	if oldCard != nil {
		g.state.DiscardPile = append(g.state.DiscardPile, oldCard)
	}

	// Update revealed cards if this position was revealed
	for _, idx := range player.RevealedCards {
		if idx == cardIndex {
			player.Score = CalculatePlayerScore(player)
			break
		}
	}

	return g.endTurn(clientID)
}

// DiscardDrawn discards the drawn card without swapping
func (g *Game) DiscardDrawn(clientID string) error {
	g.mu.Lock()
	defer g.mu.Unlock()

	if err := g.validateTurn(clientID); err != nil {
		return err
	}

	if g.state.DrawnCard == nil {
		return fmt.Errorf("no drawn card to discard")
	}

	// Add drawn card to discard pile
	g.state.DiscardPile = append(g.state.DiscardPile, g.state.DrawnCard)
	g.state.DrawnCard = nil

	return g.endTurn(clientID)
}

// Knock signals the last round
func (g *Game) Knock(clientID string) error {
	g.mu.Lock()
	defer g.mu.Unlock()

	if err := g.validateTurn(clientID); err != nil {
		return err
	}

	if g.state.DrawnCard != nil {
		return fmt.Errorf("cannot knock after drawing")
	}

	if g.state.PeekedAtDrawPile {
		return fmt.Errorf("cannot knock after peeking")
	}

	if g.state.GamePhase == "knocked" {
		return fmt.Errorf("someone already knocked")
	}

	player := g.playersByClient[clientID]
	g.state.KnockedPlayerID = &player.ID
	g.state.GamePhase = "knocked"

	// Don't mark the knocking player as having played - they knocked instead of playing

	// Advance to next player
	g.state.CurrentPlayerIndex = (g.state.CurrentPlayerIndex + 1) % len(g.state.Players)

	return nil
}

// GetState returns a copy of the current game state
func (g *Game) GetState() *GameState {
	g.mu.RLock()
	defer g.mu.RUnlock()

	// Deep copy the state
	stateCopy := &GameState{
		ID:                 g.state.ID,
		Players:            make([]*Player, len(g.state.Players)),
		CurrentPlayerIndex: g.state.CurrentPlayerIndex,
		DrawPile:           g.state.DrawPile,
		DiscardPile:        make([]*Card, len(g.state.DiscardPile)),
		GamePhase:          g.state.GamePhase,
		KnockedPlayerID:    g.state.KnockedPlayerID,
		DrawnCard:          g.state.DrawnCard,
		PeekedAtDrawPile:   g.state.PeekedAtDrawPile,
		AllPlayersPeeked:   g.state.AllPlayersPeeked,
	}

	// Copy players
	for i, player := range g.state.Players {
		playerCopy := &Player{
			ID:            player.ID,
			Name:          player.Name,
			Cards:         make([]*Card, 4),
			Score:         0, // Hide score during gameplay
			RevealedCards: make([]int, len(player.RevealedCards)),
			IsReady:       player.IsReady,
			HasPeeked:     player.HasPeeked,
		}

		// Only show scores when game has ended
		if g.state.GamePhase == "ended" {
			playerCopy.Score = player.Score
		}

		copy(playerCopy.Cards, player.Cards)
		copy(playerCopy.RevealedCards, player.RevealedCards)
		stateCopy.Players[i] = playerCopy
	}

	// Copy discard pile
	copy(stateCopy.DiscardPile, g.state.DiscardPile)

	return stateCopy
}

// GetPlayerByClientID returns the player associated with a client ID
func (g *Game) GetPlayerByClientID(clientID string) *Player {
	g.mu.RLock()
	defer g.mu.RUnlock()
	return g.playersByClient[clientID]
}

// GetStateForPlayer returns a personalized view of the game state for a specific player
func (g *Game) GetStateForPlayer(clientID string) *GameState {
	g.mu.RLock()
	defer g.mu.RUnlock()

	viewingPlayer := g.playersByClient[clientID]
	if viewingPlayer == nil {
		return g.GetState() // Fallback to full state if player not found
	}

	// Deep copy the state
	stateCopy := &GameState{
		ID:                 g.state.ID,
		Players:            make([]*Player, len(g.state.Players)),
		CurrentPlayerIndex: g.state.CurrentPlayerIndex,
		DrawPile:           g.state.DrawPile,
		DiscardPile:        make([]*Card, len(g.state.DiscardPile)),
		GamePhase:          g.state.GamePhase,
		KnockedPlayerID:    g.state.KnockedPlayerID,
		DrawnCard:          nil, // Will be set below only for current player
		PeekedAtDrawPile:   g.state.PeekedAtDrawPile,
		AllPlayersPeeked:   g.state.AllPlayersPeeked,
	}

	// Only show drawn card to the current player
	if g.state.DrawnCard != nil && g.state.Players[g.state.CurrentPlayerIndex].ID == viewingPlayer.ID {
		stateCopy.DrawnCard = g.state.DrawnCard
	}

	// Copy players with visibility rules
	for i, player := range g.state.Players {
		playerCopy := &Player{
			ID:            player.ID,
			Name:          player.Name,
			Cards:         make([]*Card, 4),
			Score:         0, // Hide score during gameplay
			RevealedCards: make([]int, len(player.RevealedCards)),
			IsReady:       player.IsReady,
			HasPeeked:     player.HasPeeked,
		}

		// Only show scores when game has ended
		if g.state.GamePhase == "ended" {
			playerCopy.Score = player.Score
		}

		// Only copy card data if it's the viewing player and cards should be shown
		if player.ID == viewingPlayer.ID && g.ShouldShowCards(clientID) {
			copy(playerCopy.Cards, player.Cards)
			copy(playerCopy.RevealedCards, player.RevealedCards)
		} else {
			// For other players, cards remain nil (hidden)
			// and RevealedCards is empty
		}

		stateCopy.Players[i] = playerCopy
	}

	// Copy discard pile (visible to all)
	copy(stateCopy.DiscardPile, g.state.DiscardPile)

	return stateCopy
}

// validateTurn checks if it's the player's turn
func (g *Game) validateTurn(clientID string) error {
	player, exists := g.playersByClient[clientID]
	if !exists {
		return fmt.Errorf("player not found")
	}

	if g.state.GamePhase != "playing" && g.state.GamePhase != "knocked" {
		return fmt.Errorf("game not in playing phase")
	}

	currentPlayer := g.state.Players[g.state.CurrentPlayerIndex]
	if currentPlayer.ID != player.ID {
		return fmt.Errorf("not your turn")
	}

	return nil
}

// endTurn advances to the next player's turn
func (g *Game) endTurn(clientID string) error {
	player := g.playersByClient[clientID]

	// If in knocked phase, track who has played their final turn
	if g.state.GamePhase == "knocked" {
		g.finalRoundPlayed[player.ID] = true

		// Check if all OTHER players have had their final turn
		// The knocking player doesn't get another turn
		allPlayed := true
		for _, p := range g.state.Players {
			if p.ID != *g.state.KnockedPlayerID && !g.finalRoundPlayed[p.ID] {
				allPlayed = false
				break
			}
		}

		if allPlayed {
			g.state.GamePhase = "ended"
			// Calculate final scores
			g.calculateFinalScores()
			return nil
		}
	}

	// Reset peeked state for next turn
	g.state.PeekedAtDrawPile = false

	// Advance to next player
	g.state.CurrentPlayerIndex = (g.state.CurrentPlayerIndex + 1) % len(g.state.Players)
	return nil
}

// calculateFinalScores reveals all cards and calculates final scores
func (g *Game) calculateFinalScores() {
	for _, player := range g.state.Players {
		// Reveal all cards
		player.RevealedCards = []int{0, 1, 2, 3}
		// Calculate total score with pair cancellation
		player.Score = g.calculatePlayerFinalScore(player)
	}
}

// calculatePlayerFinalScore calculates score with pair cancellation
func (g *Game) calculatePlayerFinalScore(player *Player) int {
	// Count occurrences of each rank
	rankCounts := make(map[string]int)
	for i := 0; i < 4; i++ {
		if player.Cards[i] != nil {
			rankCounts[player.Cards[i].Rank]++
		}
	}

	// Calculate score with pairs canceling out
	score := 0
	for i := 0; i < 4; i++ {
		if player.Cards[i] != nil {
			rank := player.Cards[i].Rank
			// Only count cards that don't have a pair
			if rankCounts[rank] == 1 || rankCounts[rank] == 3 {
				score += GetCardValue(player.Cards[i])
			}
			// If there are 2 or 4 of the same rank, they cancel out
		}
	}

	return score
}

// GetWinner returns the player with the lowest score
func (g *Game) GetWinner() *Player {
	g.mu.RLock()
	defer g.mu.RUnlock()

	if g.state.GamePhase != "ended" || len(g.state.Players) == 0 {
		return nil
	}

	// Find the minimum score
	minScore := g.state.Players[0].Score
	for _, player := range g.state.Players[1:] {
		if player.Score < minScore {
			minScore = player.Score
		}
	}

	// Get all players with the minimum score
	var winners []*Player
	for _, player := range g.state.Players {
		if player.Score == minScore {
			winners = append(winners, player)
		}
	}

	// Special rule: if the knocker is among the winners, only they win
	if g.state.KnockedPlayerID != nil {
		for _, winner := range winners {
			if winner.ID == *g.state.KnockedPlayerID {
				return winner
			}
		}
	}

	// Otherwise, return the first winner (or handle ties differently if needed)
	if len(winners) > 0 {
		return winners[0]
	}

	return nil
}

// GetFinalScores returns the final scores of all players
func (g *Game) GetFinalScores() []*FinalScore {
	g.mu.RLock()
	defer g.mu.RUnlock()

	scores := make([]*FinalScore, 0, len(g.state.Players))
	for _, player := range g.state.Players {
		scores = append(scores, &FinalScore{
			PlayerName: player.Name,
			Score:      player.Score,
		})
	}

	return scores
}

// HidePeekedCards hides all peeked cards after countdown
func (g *Game) HidePeekedCards() {
	g.mu.Lock()
	defer g.mu.Unlock()

	if g.state.GamePhase == "peeking" {
		// Hide all cards
		for _, player := range g.state.Players {
			player.RevealedCards = make([]int, 0)
		}
		g.state.GamePhase = "playing"
		g.state.AllPlayersPeeked = false
	}
}

// ShouldShowCards determines if cards should be shown to a player
func (g *Game) ShouldShowCards(clientID string) bool {
	g.mu.RLock()
	defer g.mu.RUnlock()

	player := g.playersByClient[clientID]
	if player == nil {
		return false
	}

	// Show revealed cards during initial peeking phase
	if len(player.RevealedCards) > 0 {
		// Always show during peeking countdown phase
		if g.state.GamePhase == "peeking" {
			return true
		}
		// Show during playing phase if we haven't started the countdown yet
		if g.state.GamePhase == "playing" && !g.state.AllPlayersPeeked {
			return true
		}
	}

	// Show cards at game end
	if g.state.GamePhase == "ended" {
		return true
	}

	// Show cards when player has drawn a card (about to discard)
	if g.state.DrawnCard != nil && g.state.Players[g.state.CurrentPlayerIndex].ID == player.ID {
		return true
	}

	return false
}
