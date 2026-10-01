package chess_engine

import (
	"errors"
	"fmt"
	"regexp"
)

// The bounds a query must keep. Stockfish's UCI_Elo spans 1320–3190; a
// query without one plays at full strength.
const (
	DefaultMovetimeMs = 200
	MinMovetimeMs     = 10
	MaxMovetimeMs     = 5000
	MinElo            = 1320
	MaxElo            = 3190
	maxMoves          = 1000
	maxFenLength      = 100
)

// Query asks for the move from a position: a FEN, the moves played from
// it, how long to think, and at what strength.
type Query struct {
	FEN        string   `json:"fen"`
	Moves      []string `json:"moves"`
	MovetimeMs int      `json:"movetimeMs"`
	Elo        *int     `json:"elo"`
}

// Each field becomes part of a UCI line, so the patterns admit nothing
// that could end one: a FEN's six fields, a move in UCI's spelling.
var (
	fenPattern  = regexp.MustCompile(`^[pnbrqkPNBRQK1-8/]+ [wb] (-|[KQkq]{1,4}) (-|[a-h][36]) \d{1,3} \d{1,4}$`)
	movePattern = regexp.MustCompile(`^[a-h][1-8][a-h][1-8][qrbn]?$`)
)

// Validate refuses a query the engine must not be sent.
func (q Query) Validate() error {
	if len(q.FEN) == 0 || len(q.FEN) > maxFenLength || !fenPattern.MatchString(q.FEN) {
		return errors.New("fen: not a position")
	}
	if len(q.Moves) > maxMoves {
		return fmt.Errorf("moves: at most %d", maxMoves)
	}
	for _, move := range q.Moves {
		if !movePattern.MatchString(move) {
			return fmt.Errorf("moves: not a UCI move: %q", move)
		}
	}
	if q.MovetimeMs < MinMovetimeMs || q.MovetimeMs > MaxMovetimeMs {
		return fmt.Errorf("movetimeMs: %d to %d", MinMovetimeMs, MaxMovetimeMs)
	}
	if q.Elo != nil && (*q.Elo < MinElo || *q.Elo > MaxElo) {
		return fmt.Errorf("elo: %d to %d", MinElo, MaxElo)
	}
	return nil
}
