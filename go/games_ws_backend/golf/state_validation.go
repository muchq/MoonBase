package golf

import (
	"fmt"
	"sync"
)

// StateValidator provides validation for game and room state invariants
type StateValidator struct {
	mu sync.RWMutex
}

// NewStateValidator creates a new state validator
func NewStateValidator() *StateValidator {
	return &StateValidator{}
}

// ValidateRoomInvariants checks that a room maintains valid state
func (sv *StateValidator) ValidateRoomInvariants(room *Room) error {
	if room == nil {
		return fmt.Errorf("room is nil")
	}
	
	// Check player count constraints
	if len(room.Players) == 0 {
		return fmt.Errorf("room has no players")
	}
	
	if len(room.Players) > 4 {
		return fmt.Errorf("room has too many players: %d (max 4)", len(room.Players))
	}
	
	// Check player uniqueness
	clientIDs := make(map[string]bool)
	playerIDs := make(map[string]bool)
	
	for _, player := range room.Players {
		if player == nil {
			return fmt.Errorf("room contains nil player")
		}
		
		// Check for duplicate client IDs
		if clientIDs[player.ClientID] {
			return fmt.Errorf("duplicate client ID in room: %s", player.ClientID)
		}
		clientIDs[player.ClientID] = true
		
		// Check for duplicate player IDs
		if playerIDs[player.ID] {
			return fmt.Errorf("duplicate player ID in room: %s", player.ID)
		}
		playerIDs[player.ID] = true
		
		// Validate player stats
		if player.GamesPlayed < 0 {
			return fmt.Errorf("player %s has negative games played: %d", player.ID, player.GamesPlayed)
		}
		
		if player.GamesWon < 0 {
			return fmt.Errorf("player %s has negative games won: %d", player.ID, player.GamesWon)
		}
		
		if player.GamesWon > player.GamesPlayed {
			return fmt.Errorf("player %s has more wins than games played: %d > %d", 
				player.ID, player.GamesWon, player.GamesPlayed)
		}
	}
	
	// Validate games in room
	gameIDs := make(map[string]bool)
	for gameID, game := range room.Games {
		if gameIDs[gameID] {
			return fmt.Errorf("duplicate game ID in room: %s", gameID)
		}
		gameIDs[gameID] = true
		
		if err := sv.ValidateGameInvariants(game); err != nil {
			return fmt.Errorf("invalid game %s in room: %w", gameID, err)
		}
		
		// Validate game belongs to room
		if game.GetRoomID() != room.ID {
			return fmt.Errorf("game %s reports different room ID: %s != %s", 
				gameID, game.GetRoomID(), room.ID)
		}
	}
	
	// Validate game history
	for i, result := range room.GameHistory {
		if result == nil {
			return fmt.Errorf("room has nil game result at index %d", i)
		}
		
		if result.GameID == "" {
			return fmt.Errorf("game result at index %d has empty game ID", i)
		}
		
		if result.Winner == "" {
			return fmt.Errorf("game result at index %d has empty winner", i)
		}
		
		if len(result.FinalScores) == 0 {
			return fmt.Errorf("game result at index %d has no final scores", i)
		}
	}
	
	return nil
}

// ValidateGameInvariants checks that a game maintains valid state
func (sv *StateValidator) ValidateGameInvariants(game *Game) error {
	if game == nil {
		return fmt.Errorf("game is nil")
	}
	
	game.mu.RLock()
	defer game.mu.RUnlock()
	
	if game.state == nil {
		return fmt.Errorf("game state is nil")
	}
	
	// Validate player count
	if len(game.state.Players) > 4 {
		return fmt.Errorf("game has too many players: %d (max 4)", len(game.state.Players))
	}
	
	// Validate current player index
	if len(game.state.Players) > 0 {
		if game.state.CurrentPlayerIndex < 0 {
			return fmt.Errorf("current player index is negative: %d", game.state.CurrentPlayerIndex)
		}
		
		if game.state.CurrentPlayerIndex >= len(game.state.Players) {
			return fmt.Errorf("current player index %d out of bounds for %d players", 
				game.state.CurrentPlayerIndex, len(game.state.Players))
		}
	}
	
	// Validate game phase
	validPhases := map[string]bool{
		"waiting": true, "playing": true, "peeking": true, "knocked": true, "ended": true,
	}
	if !validPhases[game.state.GamePhase] {
		return fmt.Errorf("invalid game phase: %s", game.state.GamePhase)
	}
	
	// Phase-specific validations
	switch game.state.GamePhase {
	case "waiting":
		if len(game.state.DiscardPile) != 0 {
			return fmt.Errorf("waiting game should have empty discard pile, has %d cards", 
				len(game.state.DiscardPile))
		}
		
		if game.state.DrawPile != 0 {
			return fmt.Errorf("waiting game should have draw pile = 0, has %d", game.state.DrawPile)
		}
		
	case "playing", "peeking", "knocked", "ended":
		if len(game.state.Players) < 2 {
			return fmt.Errorf("started game must have at least 2 players, has %d", 
				len(game.state.Players))
		}
		
		// Validate deck state
		expectedDeckSize := 52 - (len(game.state.Players) * 4) - len(game.state.DiscardPile)
		if game.state.DrawnCard != nil {
			expectedDeckSize--
		}
		
		if game.state.DrawPile != expectedDeckSize {
			return fmt.Errorf("draw pile size %d doesn't match expected %d", 
				game.state.DrawPile, expectedDeckSize)
		}
	}
	
	// Validate players in started games
	if game.state.GamePhase != "waiting" {
		playerIDs := make(map[string]bool)
		
		for _, player := range game.state.Players {
			if player == nil {
				return fmt.Errorf("game contains nil player")
			}
			
			// Check for duplicate player IDs
			if playerIDs[player.ID] {
				return fmt.Errorf("duplicate player ID in game: %s", player.ID)
			}
			playerIDs[player.ID] = true
			
			// Validate card count
			if len(player.Cards) != 4 {
				return fmt.Errorf("player %s has %d cards, expected 4", player.ID, len(player.Cards))
			}
			
			// Validate revealed cards
			if len(player.RevealedCards) > 2 {
				return fmt.Errorf("player %s has %d revealed cards, max 2", 
					player.ID, len(player.RevealedCards))
			}
			
			// Check revealed card indices are valid
			for _, idx := range player.RevealedCards {
				if idx < 0 || idx > 3 {
					return fmt.Errorf("player %s has invalid revealed card index: %d", player.ID, idx)
				}
			}
			
			// Check for duplicate revealed indices
			revealedSet := make(map[int]bool)
			for _, idx := range player.RevealedCards {
				if revealedSet[idx] {
					return fmt.Errorf("player %s has duplicate revealed card index: %d", player.ID, idx)
				}
				revealedSet[idx] = true
			}
			
			// Validate score is non-negative
			if player.Score < 0 {
				return fmt.Errorf("player %s has negative score: %d", player.ID, player.Score)
			}
		}
	}
	
	// Validate knocked state
	if game.state.GamePhase == "knocked" || game.state.GamePhase == "ended" {
		if game.state.KnockedPlayerID == nil {
			return fmt.Errorf("knocked/ended game must have KnockedPlayerID set")
		}
		
		// Check knocked player exists
		foundKnocker := false
		for _, player := range game.state.Players {
			if player.ID == *game.state.KnockedPlayerID {
				foundKnocker = true
				break
			}
		}
		
		if !foundKnocker {
			return fmt.Errorf("knocked player ID %s not found in game", *game.state.KnockedPlayerID)
		}
	} else {
		if game.state.KnockedPlayerID != nil {
			return fmt.Errorf("non-knocked game should not have KnockedPlayerID set")
		}
	}
	
	// Validate discard pile
	if len(game.state.DiscardPile) < 0 {
		return fmt.Errorf("discard pile cannot have negative size")
	}
	
	// In started games, discard pile should have at least 1 card initially
	if game.state.GamePhase == "playing" && len(game.state.DiscardPile) == 0 && game.state.DrawnCard == nil {
		return fmt.Errorf("playing game should have cards in discard pile or drawn card")
	}
	
	return nil
}

// ValidateHubState checks the overall hub state for consistency
func (sv *StateValidator) ValidateHubState(hub *GolfHub) error {
	if hub == nil {
		return fmt.Errorf("hub is nil")
	}
	
	hub.mu.RLock()
	defer hub.mu.RUnlock()
	
	// Validate rooms
	roomIDs := make(map[string]bool)
	for roomID, room := range hub.rooms {
		if roomIDs[roomID] {
			return fmt.Errorf("duplicate room ID: %s", roomID)
		}
		roomIDs[roomID] = true
		
		if err := sv.ValidateRoomInvariants(room); err != nil {
			return fmt.Errorf("invalid room %s: %w", roomID, err)
		}
		
		if room.ID != roomID {
			return fmt.Errorf("room ID mismatch: map key %s vs room.ID %s", roomID, room.ID)
		}
	}
	
	// Validate client contexts
	for client, ctx := range hub.clientContexts {
		if client == nil {
			return fmt.Errorf("hub has nil client in contexts")
		}
		
		if ctx == nil {
			return fmt.Errorf("hub has nil context for client")
		}
		
		// If client is in a room, room must exist
		if ctx.RoomID != "" {
			if _, exists := hub.rooms[ctx.RoomID]; !exists {
				return fmt.Errorf("client context references non-existent room: %s", ctx.RoomID)
			}
			
			// If client is in a game, game must exist in the room
			if ctx.GameID != "" {
				room := hub.rooms[ctx.RoomID]
				if _, exists := room.Games[ctx.GameID]; !exists {
					return fmt.Errorf("client context references non-existent game %s in room %s", 
						ctx.GameID, ctx.RoomID)
				}
			}
		}
		
		// Validate timestamps
		if ctx.JoinedAt.After(ctx.LastAction) {
			return fmt.Errorf("client joined after last action: %v > %v", ctx.JoinedAt, ctx.LastAction)
		}
	}
	
	return nil
}

// ValidateGameTransition checks if a game state transition is valid
func (sv *StateValidator) ValidateGameTransition(oldPhase, newPhase string, playerCount int) error {
	validTransitions := map[string][]string{
		"waiting": {"playing"},
		"playing": {"peeking", "knocked"},
		"peeking": {"playing"},
		"knocked": {"ended"},
		"ended":   {}, // Terminal state
	}
	
	validNext, exists := validTransitions[oldPhase]
	if !exists {
		return fmt.Errorf("unknown game phase: %s", oldPhase)
	}
	
	for _, valid := range validNext {
		if valid == newPhase {
			return nil
		}
	}
	
	return fmt.Errorf("invalid phase transition: %s -> %s", oldPhase, newPhase)
}

// ValidateCardOperation checks if a card operation is valid for the current game state
func (sv *StateValidator) ValidateCardOperation(game *Game, operation string, playerID string, cardIndex int) error {
	if game == nil {
		return fmt.Errorf("game is nil")
	}
	
	game.mu.RLock()
	defer game.mu.RUnlock()
	
	// Find the player
	var player *Player
	for _, p := range game.state.Players {
		if p.ClientID == playerID {
			player = p
			break
		}
	}
	
	if player == nil {
		return fmt.Errorf("player %s not found in game", playerID)
	}
	
	// Operation-specific validations
	switch operation {
	case "peek":
		if game.state.GamePhase != "playing" && game.state.GamePhase != "peeking" {
			return fmt.Errorf("can only peek during playing phase")
		}
		
		if len(player.RevealedCards) >= 2 {
			return fmt.Errorf("player already peeked at maximum number of cards")
		}
		
		if cardIndex < 0 || cardIndex > 3 {
			return fmt.Errorf("invalid card index for peek: %d", cardIndex)
		}
		
		// Check if already peeked at this card
		for _, idx := range player.RevealedCards {
			if idx == cardIndex {
				return fmt.Errorf("already peeked at card %d", cardIndex)
			}
		}
		
	case "swap":
		if game.state.GamePhase != "playing" && game.state.GamePhase != "knocked" {
			return fmt.Errorf("can only swap during playing/knocked phases")
		}
		
		if game.state.DrawnCard == nil {
			return fmt.Errorf("no drawn card to swap")
		}
		
		if cardIndex < 0 || cardIndex > 3 {
			return fmt.Errorf("invalid card index for swap: %d", cardIndex)
		}
		
		// Check if it's player's turn
		currentPlayer := game.state.Players[game.state.CurrentPlayerIndex]
		if currentPlayer.ClientID != playerID {
			return fmt.Errorf("not player's turn")
		}
		
	default:
		return fmt.Errorf("unknown card operation: %s", operation)
	}
	
	return nil
}