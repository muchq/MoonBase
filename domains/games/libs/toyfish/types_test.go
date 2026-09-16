package toyfish

import (
	"encoding/json"
	"os"
	"testing"

	s "github.com/muchq/moonbase/domains/games/libs/toyfish/settings"
)

func TestCastling(t *testing.T) {
	// Manually load settings since NewSettings() path is relative to repo root
	// and go test runs in package dir.
	content, err := os.ReadFile("settings/settings.json")
	if err != nil {
		t.Fatalf("Failed to read settings/settings.json: %v", err)
	}
	var settings s.Settings
	err = json.Unmarshal(content, &settings)
	if err != nil {
		t.Fatalf("Failed to unmarshal settings: %v", err)
	}

	// 1. White King Side Castling
	settings.Fen = "4k3/8/8/8/8/8/8/4K2R w K - 0 1"
	game, err := NewGame(&settings)
	if err != nil {
		t.Fatalf("NewGame failed: %v", err)
	}
	if game.Side != White {
		t.Errorf("Expected Side White, got %v", game.Side)
	}

	moves := game.GenerateMoves()
	found := false
	for _, m := range moves {
		if m.Source == 95 && m.Target == 97 {
			found = true
			break
		}
	}
	if !found {
		t.Errorf("Expected White King Side castling move (95->97) not found")
	}

	// 2. Blocked Path
	settings.Fen = "4k3/8/8/8/8/8/8/4KB1R w K - 0 1" // Bishop on f1
	game, _ = NewGame(&settings)
	moves = game.GenerateMoves()
	for _, m := range moves {
		if m.Source == 95 && m.Target == 97 {
			t.Errorf("Castling should be blocked by piece on f1")
		}
	}

	// 3. King in Check
	settings.Fen = "4r3/8/8/8/8/8/8/4K2R w K - 0 1" // Rook on e8 attacks e1
	game, _ = NewGame(&settings)
	if !game.IsAttacked(95, Black) {
		t.Errorf("e1 should be attacked by Black Rook at e8")
	}
	moves = game.GenerateMoves()
	for _, m := range moves {
		if m.Source == 95 && m.Target == 97 {
			t.Errorf("Castling should be illegal when in check")
		}
	}

	// 4. Castling Through Check
	settings.Fen = "5r2/8/8/8/8/8/8/4K2R w K - 0 1" // Rook on f8 attacks f1(96)
	game, _ = NewGame(&settings)
	if !game.IsAttacked(96, Black) {
		t.Errorf("f1 should be attacked by Black Rook at f8")
	}
	moves = game.GenerateMoves()
	for _, m := range moves {
		if m.Source == 95 && m.Target == 97 {
			t.Errorf("Castling should be illegal through check")
		}
	}

	// 5. Castling Into Check
	settings.Fen = "6r1/8/8/8/8/8/8/4K2R w K - 0 1" // Rook on g8 attacks g1(97)
	game, _ = NewGame(&settings)
	if !game.IsAttacked(97, Black) {
		t.Errorf("g1 should be attacked by Black Rook at g8")
	}
	moves = game.GenerateMoves()
	for _, m := range moves {
		if m.Source == 95 && m.Target == 97 {
			t.Errorf("Castling should be illegal into check")
		}
	}

	// 6. Black Queen Side Castling
	settings.Fen = "r3k3/8/8/8/8/8/8/4K3 b q - 0 1"
	game, _ = NewGame(&settings)
	if game.Side != Black {
		t.Errorf("Expected Side Black, got %v", game.Side)
	}
	moves = game.GenerateMoves()
	found = false
	for _, m := range moves {
		if m.Source == 25 && m.Target == 23 {
			found = true
			break
		}
	}
	if !found {
		t.Errorf("Expected Black Queen Side castling move (25->23) not found")
	}

	// 7. White Queen Side Castling
	settings.Fen = "4k3/8/8/8/8/8/8/R3K3 w Q - 0 1"
	game, _ = NewGame(&settings)
	moves = game.GenerateMoves()
	found = false
	for _, m := range moves {
		if m.Source == 95 && m.Target == 93 {
			found = true
			break
		}
	}
	if !found {
		t.Errorf("Expected White Queen Side castling move (95->93) not found")
	}

	// 8. Black King Side Castling
	settings.Fen = "4k2r/8/8/8/8/8/8/4K3 b k - 0 1"
	game, _ = NewGame(&settings)
	moves = game.GenerateMoves()
	found = false
	for _, m := range moves {
		if m.Source == 25 && m.Target == 27 {
			found = true
			break
		}
	}
	if !found {
		t.Errorf("Expected Black King Side castling move (25->27) not found")
	}
}
