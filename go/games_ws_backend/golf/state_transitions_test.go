package golf

import (
	"fmt"
	"testing"

	"github.com/muchq/moonbase/go/games_ws_backend/players"
)

// Helper function to add test players with consistent IDs
func addTestPlayer(g *Game, clientID string) (*Player, error) {
	playerID := fmt.Sprintf("TestPlayer%s", clientID)
	return g.AddPlayer(clientID, playerID, playerID)
}

// TestStateTransitions_GamePhases tests all valid game phase transitions
func TestStateTransitions_GamePhases(t *testing.T) {
	tests := []struct {
		name          string
		setupFunc     func() *Game
		action        func(*Game) error
		expectedPhase string
		expectError   bool
		errorContains string
	}{
		// Waiting -> Playing transitions
		{
			name: "waiting to playing - valid with 2 players",
			setupFunc: func() *Game {
				g := NewGame("TEST1", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				return g
			},
			action: func(g *Game) error {
				return g.StartGame()
			},
			expectedPhase: "playing",
			expectError:   false,
		},
		{
			name: "waiting to playing - invalid with 1 player",
			setupFunc: func() *Game {
				g := NewGame("TEST2", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				return g
			},
			action: func(g *Game) error {
				return g.StartGame()
			},
			expectedPhase: "waiting",
			expectError:   true,
			errorContains: "at least 2 players",
		},
		{
			name: "waiting to playing - invalid with 0 players",
			setupFunc: func() *Game {
				return NewGame("TEST3", &players.DeterministicIDGenerator{})
			},
			action: func(g *Game) error {
				return g.StartGame()
			},
			expectedPhase: "waiting",
			expectError:   true,
			errorContains: "at least 2 players",
		},
		// Playing -> Knocked transitions
		{
			name: "playing to knocked - valid knock",
			setupFunc: func() *Game {
				g := NewGame("TEST4", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				g.StartGame()
				return g
			},
			action: func(g *Game) error {
				return g.Knock("p1")
			},
			expectedPhase: "knocked",
			expectError:   false,
		},
		{
			name: "playing to knocked - invalid knock after draw",
			setupFunc: func() *Game {
				g := NewGame("TEST5", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				g.StartGame()
				g.DrawCard("p1")
				return g
			},
			action: func(g *Game) error {
				return g.Knock("p1")
			},
			expectedPhase: "playing",
			expectError:   true,
			errorContains: "cannot knock after drawing",
		},
		// Knocked -> Ended transitions
		{
			name: "knocked to ended - when knocker's turn comes again",
			setupFunc: func() *Game {
				g := NewGame("TEST6", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				g.StartGame()
				g.Knock("p1") // p1 knocks, turn goes to p2
				g.DrawCard("p2")
				return g
			},
			action: func(g *Game) error {
				return g.DiscardDrawn("p2") // p2 finishes turn, game should end
			},
			expectedPhase: "ended",
			expectError:   false,
		},
		// Invalid transitions
		{
			name: "ended state - no transitions allowed",
			setupFunc: func() *Game {
				g := NewGame("TEST7", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				g.StartGame()
				g.Knock("p1")
				g.DrawCard("p2")
				g.DiscardDrawn("p2")
				return g
			},
			action: func(g *Game) error {
				return g.DrawCard("p1")
			},
			expectedPhase: "ended",
			expectError:   true,
			errorContains: "game not in playing phase",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			game := tt.setupFunc()
			err := tt.action(game)

			if tt.expectError {
				if err == nil {
					t.Errorf("Expected error containing '%s', got nil", tt.errorContains)
				} else if tt.errorContains != "" && !contains(err.Error(), tt.errorContains) {
					t.Errorf("Expected error containing '%s', got '%s'", tt.errorContains, err.Error())
				}
			} else {
				if err != nil {
					t.Errorf("Expected no error, got: %v", err)
				}
			}

			if game.state.GamePhase != tt.expectedPhase {
				t.Errorf("Expected phase '%s', got '%s'", tt.expectedPhase, game.state.GamePhase)
			}
		})
	}
}

// TestStateTransitions_DrawOperations tests all draw pile and discard pile operations
func TestStateTransitions_DrawOperations(t *testing.T) {
	tests := []struct {
		name          string
		setupFunc     func() *Game
		action        func(*Game) error
		expectError   bool
		errorContains string
		validate      func(*testing.T, *Game)
	}{
		// Draw from draw pile
		{
			name: "draw from pile - valid on player's turn",
			setupFunc: func() *Game {
				g := NewGame("TEST1", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				g.StartGame()
				return g
			},
			action: func(g *Game) error {
				return g.DrawCard("p1")
			},
			expectError: false,
			validate: func(t *testing.T, g *Game) {
				if g.state.DrawnCard == nil {
					t.Error("Expected drawn card to be set")
				}
				if g.state.DrawPile >= 44 {
					t.Error("Draw pile should decrease")
				}
			},
		},
		{
			name: "draw from pile - invalid when not your turn",
			setupFunc: func() *Game {
				g := NewGame("TEST2", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				g.StartGame()
				return g
			},
			action: func(g *Game) error {
				return g.DrawCard("p2") // p1's turn
			},
			expectError:   true,
			errorContains: "not your turn",
		},
		{
			name: "draw from pile - invalid when already drawn",
			setupFunc: func() *Game {
				g := NewGame("TEST3", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				g.StartGame()
				g.DrawCard("p1")
				return g
			},
			action: func(g *Game) error {
				return g.DrawCard("p1")
			},
			expectError:   true,
			errorContains: "already have a drawn card",
		},
		{
			name: "draw from pile - invalid when game not started",
			setupFunc: func() *Game {
				g := NewGame("TEST4", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				return g
			},
			action: func(g *Game) error {
				return g.DrawCard("p1")
			},
			expectError:   true,
			errorContains: "game not in playing phase",
		},
		{
			name: "draw from pile - invalid when game is over",
			setupFunc: func() *Game {
				g := NewGame("TEST5", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				g.StartGame()
				g.state.GamePhase = "ended"
				return g
			},
			action: func(g *Game) error {
				return g.DrawCard("p1")
			},
			expectError:   true,
			errorContains: "game not in playing phase",
		},
		// Take from discard pile
		{
			name: "take from discard - valid on player's turn",
			setupFunc: func() *Game {
				g := NewGame("TEST6", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				g.StartGame()
				return g
			},
			action: func(g *Game) error {
				return g.TakeFromDiscard("p1")
			},
			expectError: false,
			validate: func(t *testing.T, g *Game) {
				if g.state.DrawnCard == nil {
					t.Error("Expected drawn card to be set")
				}
				if len(g.state.DiscardPile) != 0 {
					t.Error("Discard pile should be empty after taking")
				}
			},
		},
		{
			name: "take from discard - invalid when not your turn",
			setupFunc: func() *Game {
				g := NewGame("TEST7", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				g.StartGame()
				return g
			},
			action: func(g *Game) error {
				return g.TakeFromDiscard("p2")
			},
			expectError:   true,
			errorContains: "not your turn",
		},
		{
			name: "take from discard - invalid when already have drawn card",
			setupFunc: func() *Game {
				g := NewGame("TEST8", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				g.StartGame()
				g.DrawCard("p1")
				return g
			},
			action: func(g *Game) error {
				return g.TakeFromDiscard("p1")
			},
			expectError:   true,
			errorContains: "already have a drawn card",
		},
		{
			name: "take from discard - invalid when discard is empty",
			setupFunc: func() *Game {
				g := NewGame("TEST9", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				g.StartGame()
				g.state.DiscardPile = []*Card{} // Empty it
				return g
			},
			action: func(g *Game) error {
				return g.TakeFromDiscard("p1")
			},
			expectError:   true,
			errorContains: "discard pile is empty",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			game := tt.setupFunc()
			err := tt.action(game)

			if tt.expectError {
				if err == nil {
					t.Errorf("Expected error containing '%s', got nil", tt.errorContains)
				} else if tt.errorContains != "" && !contains(err.Error(), tt.errorContains) {
					t.Errorf("Expected error containing '%s', got '%s'", tt.errorContains, err.Error())
				}
			} else {
				if err != nil {
					t.Errorf("Expected no error, got: %v", err)
				}
			}

			if tt.validate != nil {
				tt.validate(t, game)
			}
		})
	}
}

// TestStateTransitions_SwapOperations tests card swapping mechanics
func TestStateTransitions_SwapOperations(t *testing.T) {
	tests := []struct {
		name          string
		setupFunc     func() *Game
		action        func(*Game) error
		expectError   bool
		errorContains string
		validate      func(*testing.T, *Game)
	}{
		{
			name: "swap card - valid after drawing",
			setupFunc: func() *Game {
				g := NewGame("TEST1", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				g.StartGame()
				g.DrawCard("p1")
				return g
			},
			action: func(g *Game) error {
				return g.SwapCard("p1", 0)
			},
			expectError: false,
			validate: func(t *testing.T, g *Game) {
				if g.state.DrawnCard != nil {
					t.Error("Drawn card should be cleared after swap")
				}
				if g.state.CurrentPlayerIndex != 1 {
					t.Error("Turn should advance after swap")
				}
				if len(g.state.DiscardPile) != 2 {
					t.Error("Discard pile should have 2 cards after swap")
				}
			},
		},
		{
			name: "swap card - invalid without drawn card",
			setupFunc: func() *Game {
				g := NewGame("TEST2", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				g.StartGame()
				return g
			},
			action: func(g *Game) error {
				return g.SwapCard("p1", 0)
			},
			expectError:   true,
			errorContains: "no drawn card to swap",
		},
		{
			name: "swap card - invalid card index negative",
			setupFunc: func() *Game {
				g := NewGame("TEST3", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				g.StartGame()
				g.DrawCard("p1")
				return g
			},
			action: func(g *Game) error {
				return g.SwapCard("p1", -1)
			},
			expectError:   true,
			errorContains: "invalid card index",
		},
		{
			name: "swap card - invalid card index too high",
			setupFunc: func() *Game {
				g := NewGame("TEST4", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				g.StartGame()
				g.DrawCard("p1")
				return g
			},
			action: func(g *Game) error {
				return g.SwapCard("p1", 4)
			},
			expectError:   true,
			errorContains: "invalid card index",
		},
		{
			name: "swap card - invalid when not your turn",
			setupFunc: func() *Game {
				g := NewGame("TEST5", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				g.StartGame()
				g.DrawCard("p1")
				return g
			},
			action: func(g *Game) error {
				return g.SwapCard("p2", 0)
			},
			expectError:   true,
			errorContains: "not your turn",
		},
		{
			name: "discard drawn - valid after drawing",
			setupFunc: func() *Game {
				g := NewGame("TEST6", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				g.StartGame()
				g.DrawCard("p1")
				return g
			},
			action: func(g *Game) error {
				return g.DiscardDrawn("p1")
			},
			expectError: false,
			validate: func(t *testing.T, g *Game) {
				if g.state.DrawnCard != nil {
					t.Error("Drawn card should be cleared after discard")
				}
				if g.state.CurrentPlayerIndex != 1 {
					t.Error("Turn should advance after discard")
				}
				if len(g.state.DiscardPile) != 2 {
					t.Error("Discard pile should have 2 cards")
				}
			},
		},
		{
			name: "discard drawn - invalid without drawn card",
			setupFunc: func() *Game {
				g := NewGame("TEST7", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				g.StartGame()
				return g
			},
			action: func(g *Game) error {
				return g.DiscardDrawn("p1")
			},
			expectError:   true,
			errorContains: "no drawn card to discard",
		},
		{
			name: "discard drawn - invalid when not your turn",
			setupFunc: func() *Game {
				g := NewGame("TEST8", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				g.StartGame()
				g.DrawCard("p1")
				return g
			},
			action: func(g *Game) error {
				return g.DiscardDrawn("p2")
			},
			expectError:   true,
			errorContains: "not your turn",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			game := tt.setupFunc()
			err := tt.action(game)

			if tt.expectError {
				if err == nil {
					t.Errorf("Expected error containing '%s', got nil", tt.errorContains)
				} else if tt.errorContains != "" && !contains(err.Error(), tt.errorContains) {
					t.Errorf("Expected error containing '%s', got '%s'", tt.errorContains, err.Error())
				}
			} else {
				if err != nil {
					t.Errorf("Expected no error, got: %v", err)
				}
			}

			if tt.validate != nil {
				tt.validate(t, game)
			}
		})
	}
}

// TestStateTransitions_KnockMechanics tests all knock-related state transitions
func TestStateTransitions_KnockMechanics(t *testing.T) {
	tests := []struct {
		name          string
		setupFunc     func() *Game
		action        func(*Game) error
		expectError   bool
		errorContains string
		validate      func(*testing.T, *Game)
	}{
		{
			name: "knock - valid on player's turn before drawing",
			setupFunc: func() *Game {
				g := NewGame("TEST1", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				g.StartGame()
				return g
			},
			action: func(g *Game) error {
				return g.Knock("p1")
			},
			expectError: false,
			validate: func(t *testing.T, g *Game) {
				if g.state.GamePhase != "knocked" {
					t.Error("Game should be in knocked phase")
				}
				if g.state.KnockedPlayerID == nil || *g.state.KnockedPlayerID != "TestPlayerp1" {
					t.Error("Knocked player ID not set correctly")
				}
				if g.state.CurrentPlayerIndex != 1 {
					t.Error("Turn should advance after knock")
				}
			},
		},
		{
			name: "knock - invalid when not your turn",
			setupFunc: func() *Game {
				g := NewGame("TEST2", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				g.StartGame()
				return g
			},
			action: func(g *Game) error {
				return g.Knock("p2")
			},
			expectError:   true,
			errorContains: "not your turn",
		},
		{
			name: "knock - invalid after drawing",
			setupFunc: func() *Game {
				g := NewGame("TEST3", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				g.StartGame()
				g.DrawCard("p1")
				return g
			},
			action: func(g *Game) error {
				return g.Knock("p1")
			},
			expectError:   true,
			errorContains: "cannot knock after drawing",
		},
		{
			name: "knock - invalid when already knocked",
			setupFunc: func() *Game {
				g := NewGame("TEST4", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				addTestPlayer(g, "p3")
				g.StartGame()
				g.Knock("p1") // p1 knocks, turn goes to p2
				return g
			},
			action: func(g *Game) error {
				return g.Knock("p2")
			},
			expectError:   true,
			errorContains: "someone already knocked",
		},
		{
			name: "knock - invalid when game not started",
			setupFunc: func() *Game {
				g := NewGame("TEST5", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				return g
			},
			action: func(g *Game) error {
				return g.Knock("p1")
			},
			expectError:   true,
			errorContains: "game not in playing phase",
		},
		{
			name: "knock - invalid when game is over",
			setupFunc: func() *Game {
				g := NewGame("TEST6", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				g.StartGame()
				g.state.GamePhase = "ended"
				return g
			},
			action: func(g *Game) error {
				return g.Knock("p1")
			},
			expectError:   true,
			errorContains: "game not in playing phase",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			game := tt.setupFunc()
			err := tt.action(game)

			if tt.expectError {
				if err == nil {
					t.Errorf("Expected error containing '%s', got nil", tt.errorContains)
				} else if tt.errorContains != "" && !contains(err.Error(), tt.errorContains) {
					t.Errorf("Expected error containing '%s', got '%s'", tt.errorContains, err.Error())
				}
			} else {
				if err != nil {
					t.Errorf("Expected no error, got: %v", err)
				}
			}

			if tt.validate != nil {
				tt.validate(t, game)
			}
		})
	}
}

// TestStateTransitions_TurnAdvancement tests turn progression through the game
func TestStateTransitions_TurnAdvancement(t *testing.T) {
	tests := []struct {
		name     string
		setup    func() *Game
		validate func(*testing.T, *Game)
	}{
		{
			name: "turn advances after draw and discard",
			setup: func() *Game {
				g := NewGame("TEST1", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				g.StartGame()
				// Player 1's turn
				g.DrawCard("p1")
				g.DiscardDrawn("p1")
				return g
			},
			validate: func(t *testing.T, g *Game) {
				if g.state.CurrentPlayerIndex != 1 {
					t.Errorf("Expected turn to be player 2 (index 1), got %d", g.state.CurrentPlayerIndex)
				}
			},
		},
		{
			name: "turn advances after draw and swap",
			setup: func() *Game {
				g := NewGame("TEST2", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				g.StartGame()
				g.DrawCard("p1")
				g.SwapCard("p1", 0)
				return g
			},
			validate: func(t *testing.T, g *Game) {
				if g.state.CurrentPlayerIndex != 1 {
					t.Errorf("Expected turn to be player 2 (index 1), got %d", g.state.CurrentPlayerIndex)
				}
			},
		},
		{
			name: "turn wraps around to first player",
			setup: func() *Game {
				g := NewGame("TEST3", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				g.StartGame()
				// P1 turn
				g.DrawCard("p1")
				g.DiscardDrawn("p1")
				// P2 turn
				g.DrawCard("p2")
				g.DiscardDrawn("p2")
				return g
			},
			validate: func(t *testing.T, g *Game) {
				if g.state.CurrentPlayerIndex != 0 {
					t.Errorf("Expected turn to wrap to player 1 (index 0), got %d", g.state.CurrentPlayerIndex)
				}
			},
		},
		{
			name: "turn advances after knock",
			setup: func() *Game {
				g := NewGame("TEST4", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				addTestPlayer(g, "p3")
				g.StartGame()
				g.Knock("p1")
				return g
			},
			validate: func(t *testing.T, g *Game) {
				if g.state.CurrentPlayerIndex != 1 {
					t.Errorf("Expected turn to advance after knock, got %d", g.state.CurrentPlayerIndex)
				}
				if g.state.KnockedPlayerID == nil {
					t.Error("Knocked player ID should be set")
				}
			},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			game := tt.setup()
			tt.validate(t, game)
		})
	}
}

// TestStateTransitions_GameEndConditions tests all ways a game can end
func TestStateTransitions_GameEndConditions(t *testing.T) {
	tests := []struct {
		name     string
		setup    func() *Game
		validate func(*testing.T, *Game)
	}{
		{
			name: "game ends when knocker's turn comes again - 2 players",
			setup: func() *Game {
				g := NewGame("TEST1", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				g.StartGame()
				g.Knock("p1")        // P1 knocks, turn to P2
				g.DrawCard("p2")     // P2 takes final turn
				g.DiscardDrawn("p2") // P2 ends turn, game should end
				return g
			},
			validate: func(t *testing.T, g *Game) {
				if g.state.GamePhase != "ended" {
					t.Errorf("Expected game to be ended, got phase %s", g.state.GamePhase)
				}
				// All cards should be revealed
				for _, p := range g.state.Players {
					if len(p.RevealedCards) != 4 {
						t.Errorf("Player %s should have all 4 cards revealed, has %d", p.Name, len(p.RevealedCards))
					}
				}
			},
		},
		{
			name: "game ends when knocker's turn comes again - 3 players",
			setup: func() *Game {
				g := NewGame("TEST2", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				addTestPlayer(g, "p3")
				g.StartGame()
				g.Knock("p1")        // P1 knocks, turn to P2
				g.DrawCard("p2")     // P2 takes turn
				g.DiscardDrawn("p2") // Turn to P3
				g.DrawCard("p3")     // P3 takes turn
				g.DiscardDrawn("p3") // Back to P1 (knocker), game ends
				return g
			},
			validate: func(t *testing.T, g *Game) {
				if g.state.GamePhase != "ended" {
					t.Errorf("Expected game to be ended, got phase %s", g.state.GamePhase)
				}
			},
		},
		{
			name: "game ends when knocker's turn comes again - 4 players",
			setup: func() *Game {
				g := NewGame("TEST3", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				addTestPlayer(g, "p3")
				addTestPlayer(g, "p4")
				g.StartGame()
				g.Knock("p1")        // P1 knocks, turn to P2
				g.DrawCard("p2")     // P2 takes turn
				g.DiscardDrawn("p2") // Turn to P3
				g.DrawCard("p3")     // P3 takes turn
				g.DiscardDrawn("p3") // Turn to P4
				g.DrawCard("p4")     // P4 takes turn
				g.DiscardDrawn("p4") // Back to P1 (knocker), game ends
				return g
			},
			validate: func(t *testing.T, g *Game) {
				if g.state.GamePhase != "ended" {
					t.Errorf("Expected game to be ended, got phase %s", g.state.GamePhase)
				}
			},
		},
		{
			name: "game continues if not back to knocker yet",
			setup: func() *Game {
				g := NewGame("TEST4", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				addTestPlayer(g, "p3")
				g.StartGame()
				g.Knock("p1")        // P1 knocks, turn to P2
				g.DrawCard("p2")     // P2 takes turn
				g.DiscardDrawn("p2") // Turn to P3, game still going
				return g
			},
			validate: func(t *testing.T, g *Game) {
				if g.state.GamePhase != "knocked" {
					t.Errorf("Expected game to still be in knocked phase, got %s", g.state.GamePhase)
				}
				if g.state.CurrentPlayerIndex != 2 {
					t.Errorf("Expected turn to be P3 (index 2), got %d", g.state.CurrentPlayerIndex)
				}
			},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			game := tt.setup()
			tt.validate(t, game)
		})
	}
}

// TestStateTransitions_PlayerManagement tests adding/removing players
func TestStateTransitions_PlayerManagement(t *testing.T) {
	tests := []struct {
		name          string
		setupFunc     func() *Game
		action        func(*Game) error
		expectError   bool
		errorContains string
		validate      func(*testing.T, *Game)
	}{
		{
			name: "add player to empty game",
			setupFunc: func() *Game {
				return NewGame("TEST1", &players.DeterministicIDGenerator{})
			},
			action: func(g *Game) error {
				_, err := addTestPlayer(g, "p1")
				return err
			},
			expectError: false,
			validate: func(t *testing.T, g *Game) {
				if len(g.state.Players) != 1 {
					t.Errorf("Expected 1 player, got %d", len(g.state.Players))
				}
			},
		},
		{
			name: "add 4th player - should succeed",
			setupFunc: func() *Game {
				g := NewGame("TEST2", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				addTestPlayer(g, "p3")
				return g
			},
			action: func(g *Game) error {
				_, err := addTestPlayer(g, "p4")
				return err
			},
			expectError: false,
			validate: func(t *testing.T, g *Game) {
				if len(g.state.Players) != 4 {
					t.Errorf("Expected 4 players, got %d", len(g.state.Players))
				}
			},
		},
		{
			name: "add 5th player - should fail",
			setupFunc: func() *Game {
				g := NewGame("TEST3", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				addTestPlayer(g, "p3")
				addTestPlayer(g, "p4")
				return g
			},
			action: func(g *Game) error {
				_, err := addTestPlayer(g, "p5")
				return err
			},
			expectError:   true,
			errorContains: "game is full",
		},
		{
			name: "add player after game started - should fail",
			setupFunc: func() *Game {
				g := NewGame("TEST4", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				g.StartGame()
				return g
			},
			action: func(g *Game) error {
				_, err := addTestPlayer(g, "p3")
				return err
			},
			expectError:   true,
			errorContains: "game already started",
		},
		// Note: The Go implementation doesn't track duplicate client IDs
		// This is handled at a higher level (hub/websocket layer)
		{
			name: "remove player from waiting game",
			setupFunc: func() *Game {
				g := NewGame("TEST6", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				return g
			},
			action: func(g *Game) error {
				return g.RemovePlayer("p1")
			},
			expectError: false,
			validate: func(t *testing.T, g *Game) {
				if len(g.state.Players) != 1 {
					t.Errorf("Expected 1 player after removal, got %d", len(g.state.Players))
				}
			},
		},
		{
			name: "remove non-existent player",
			setupFunc: func() *Game {
				g := NewGame("TEST7", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				return g
			},
			action: func(g *Game) error {
				return g.RemovePlayer("p2")
			},
			expectError:   true,
			errorContains: "not found",
		},
		{
			name: "remove player after game started - should succeed but be careful",
			setupFunc: func() *Game {
				g := NewGame("TEST8", &players.DeterministicIDGenerator{})
				addTestPlayer(g, "p1")
				addTestPlayer(g, "p2")
				addTestPlayer(g, "p3")
				g.StartGame()
				return g
			},
			action: func(g *Game) error {
				return g.RemovePlayer("p2")
			},
			expectError: false,
			validate: func(t *testing.T, g *Game) {
				if len(g.state.Players) != 2 {
					t.Errorf("Expected 2 players after removal, got %d", len(g.state.Players))
				}
				// Turn index should be adjusted if necessary
				if g.state.CurrentPlayerIndex >= len(g.state.Players) {
					t.Error("Current player index out of bounds after removal")
				}
			},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			game := tt.setupFunc()
			err := tt.action(game)

			if tt.expectError {
				if err == nil {
					t.Errorf("Expected error containing '%s', got nil", tt.errorContains)
				} else if tt.errorContains != "" && !contains(err.Error(), tt.errorContains) {
					t.Errorf("Expected error containing '%s', got '%s'", tt.errorContains, err.Error())
				}
			} else {
				if err != nil {
					t.Errorf("Expected no error, got: %v", err)
				}
			}

			if tt.validate != nil {
				tt.validate(t, game)
			}
		})
	}
}

// TestStateTransitions_CompleteGameFlow tests a complete game from start to finish
func TestStateTransitions_CompleteGameFlow(t *testing.T) {
	game := NewGame("COMPLETE", &players.DeterministicIDGenerator{})

	// Add players
	p1, err := addTestPlayer(game, "client1")
	if err != nil {
		t.Fatalf("Failed to add player 1: %v", err)
	}
	if p1.Name != "TestPlayerclient1" {
		t.Errorf("Expected TestPlayerclient1, got %s", p1.Name)
	}

	p2, err := addTestPlayer(game, "client2")
	if err != nil {
		t.Fatalf("Failed to add player 2: %v", err)
	}
	if p2.Name != "TestPlayerclient2" {
		t.Errorf("Expected TestPlayerclient2, got %s", p2.Name)
	}

	// Verify waiting state
	if game.state.GamePhase != "waiting" {
		t.Errorf("Expected waiting phase, got %s", game.state.GamePhase)
	}

	// Start game
	err = game.StartGame()
	if err != nil {
		t.Fatalf("Failed to start game: %v", err)
	}

	// Verify playing state
	if game.state.GamePhase != "playing" {
		t.Errorf("Expected playing phase, got %s", game.state.GamePhase)
	}

	// Each player has 4 cards
	for _, player := range game.state.Players {
		if len(player.Cards) != 4 {
			t.Errorf("Player %s should have 4 cards, has %d", player.Name, len(player.Cards))
		}
	}

	// Discard pile has 1 card
	if len(game.state.DiscardPile) != 1 {
		t.Errorf("Discard pile should have 1 card, has %d", len(game.state.DiscardPile))
	}

	// Player 1 peeks at 2 cards
	err = game.PeekCard("client1", 0)
	if err != nil {
		t.Fatalf("Failed to peek at card 0: %v", err)
	}
	err = game.PeekCard("client1", 1)
	if err != nil {
		t.Fatalf("Failed to peek at card 1: %v", err)
	}

	// Can't peek at 3rd card
	err = game.PeekCard("client1", 2)
	if err == nil {
		t.Error("Should not be able to peek at 3rd card")
	}

	// Player 1 takes turn - draw and discard
	err = game.DrawCard("client1")
	if err != nil {
		t.Fatalf("Failed to draw card: %v", err)
	}

	if game.state.DrawnCard == nil {
		t.Error("Should have drawn card")
	}

	err = game.DiscardDrawn("client1")
	if err != nil {
		t.Fatalf("Failed to discard: %v", err)
	}

	// Should be player 2's turn
	if game.state.CurrentPlayerIndex != 1 {
		t.Errorf("Should be player 2's turn (index 1), got %d", game.state.CurrentPlayerIndex)
	}

	// Player 2 takes from discard
	err = game.TakeFromDiscard("client2")
	if err != nil {
		t.Fatalf("Failed to take from discard: %v", err)
	}

	err = game.SwapCard("client2", 2)
	if err != nil {
		t.Fatalf("Failed to swap card: %v", err)
	}

	// Back to player 1
	if game.state.CurrentPlayerIndex != 0 {
		t.Errorf("Should be player 1's turn (index 0), got %d", game.state.CurrentPlayerIndex)
	}

	// Player 1 knocks
	err = game.Knock("client1")
	if err != nil {
		t.Fatalf("Failed to knock: %v", err)
	}

	if game.state.GamePhase != "knocked" {
		t.Errorf("Should be in knocked phase, got %s", game.state.GamePhase)
	}

	// Player 2 takes final turn
	err = game.DrawCard("client2")
	if err != nil {
		t.Fatalf("Failed to draw on final turn: %v", err)
	}

	err = game.DiscardDrawn("client2")
	if err != nil {
		t.Fatalf("Failed to discard on final turn: %v", err)
	}

	// Game should be ended
	if game.state.GamePhase != "ended" {
		t.Errorf("Game should be ended, got phase %s", game.state.GamePhase)
	}

	// All cards revealed
	for _, player := range game.state.Players {
		if len(player.RevealedCards) != 4 {
			t.Errorf("Player %s should have all 4 cards revealed", player.Name)
		}
	}

	// Winner should be determined
	winner := game.GetWinner()
	if winner == nil {
		t.Error("Should have a winner")
	}
}

// Helper function to check if a string contains a substring
func contains(s, substr string) bool {
	return len(s) >= len(substr) && (s == substr || len(substr) == 0 ||
		(len(s) > 0 && len(substr) > 0 && findSubstring(s, substr) != -1))
}

func findSubstring(s, substr string) int {
	for i := 0; i <= len(s)-len(substr); i++ {
		if s[i:i+len(substr)] == substr {
			return i
		}
	}
	return -1
}
