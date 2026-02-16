package toyfish

import (
	"testing"
	s "github.com/muchq/moonbase/domains/games/libs/toyfish/settings"
    "github.com/stretchr/testify/assert"
    "github.com/stretchr/testify/require"
)

func mockSettings(fen string) *s.Settings {
	// Minimal settings for testing pawn moves
	coords := make([]string, 120)
	for i := range coords {
		coords[i] = "xx"
	}

    files := "abcdefgh"
    ranks := "87654321"

    for r, rank := range ranks {
        for f, file := range files {
            idx := 21 + r*10 + f
            coords[idx] = string(file) + string(rank)
        }
    }

	return &s.Settings{
		Fen:         fen,
		Coordinates: coords,
		Directions: map[s.Piece][]int{
            "P": {-10, -20, -9, -11},
            "p": {10, 20, 9, 11},
        },
        Rank2: []int{81, 82, 83, 84, 85, 86, 87, 88},
        Rank7: []int{31, 32, 33, 34, 35, 36, 37, 38},
	}
}

func TestEnPassant(t *testing.T) {
    // FEN: White pawn at e5, Black pawn at d5 (just moved d7-d5).
    // En Passant target is d6.
    // e5 is index 55 (Rank 5, File e -> 5th char -> index 4). 21 + 3*10 + 4 = 55. Correct.
    // d5 is index 54.
    // d6 is index 44.

    fen := "8/8/8/3pP3/8/8/8/8 w - d6 0 1"
    settings := mockSettings(fen)

    game, err := NewGame(settings)
    require.NoError(t, err)

    // Check EnPassantSquare parsing
    assert.Equal(t, 44, game.EnPassantSquare, "EnPassantSquare should be parsed correctly (d6 -> 44)")

    moves := game.GenerateMoves()

    // Look for move e5xd6 (55 -> 44)
    found := false
    for _, m := range moves {
        if m.Source == 55 && m.Target == 44 {
            found = true
            assert.NotNil(t, m.CapturedPiece, "CapturedPiece should not be nil")
            assert.Equal(t, int8('p'), m.CapturedPiece.FenRepr, "Captured piece should be black pawn")
            // The captured piece is at d5 (54), not d6 (44)
            // But Move struct doesn't expose captured piece location directly,
            // however we can verify it's the correct piece object (or at least correct type/side).
            break
        }
    }

    assert.True(t, found, "En Passant move e5xd6 should be generated")
}
