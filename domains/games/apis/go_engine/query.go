package go_engine

import (
	"errors"
	"fmt"
	"math"
	"regexp"
	"strconv"
	"strings"
)

// The bounds a query must keep: GNU Go's levels, games_hub's boards and
// komi.
const (
	MinLevel = 1
	MaxLevel = 10
	maxKomi  = 150
	maxMoves = 1000
)

// gtpColumns are GTP's: A to T, skipping I.
const gtpColumns = "ABCDEFGHJKLMNOPQRST"

// Query asks for a move, or in the scoring the dead stones: the board,
// komi, every move so far as GTP vertices (Black's first, the colours
// alternating), and the level.
type Query struct {
	Size    int      `json:"size"`
	Komi    float64  `json:"komi"`
	Moves   []string `json:"moves"`
	Level   int      `json:"level"`
	Scoring bool     `json:"scoring"`
}

// Each move becomes part of a GTP line, so the pattern admits nothing
// that could end one; the board's own bounds are checked after.
var vertexPattern = regexp.MustCompile(`^(pass|[A-HJ-T]([1-9]|1[0-9]))$`)

// Validate refuses a query the engine must not be sent.
func (q Query) Validate() error {
	if q.Size != 9 && q.Size != 13 && q.Size != 19 {
		return errors.New("size: 9, 13 or 19")
	}
	if math.Abs(q.Komi) > maxKomi || q.Komi*2 != math.Trunc(q.Komi*2) {
		return fmt.Errorf("komi: a multiple of 0.5 within ±%d", maxKomi)
	}
	if q.Level < MinLevel || q.Level > MaxLevel {
		return fmt.Errorf("level: %d to %d", MinLevel, MaxLevel)
	}
	if len(q.Moves) > maxMoves {
		return fmt.Errorf("moves: at most %d", maxMoves)
	}
	for _, move := range q.Moves {
		if !vertexPattern.MatchString(move) || !q.onBoard(move) {
			return fmt.Errorf("moves: not a vertex on the board: %q", move)
		}
	}
	return nil
}

func (q Query) onBoard(vertex string) bool {
	if vertex == "pass" {
		return true
	}
	row, _ := strconv.Atoi(vertex[1:])
	return strings.IndexByte(gtpColumns, vertex[0]) < q.Size && row <= q.Size
}

// ToMove is the colour whose move is asked for.
func (q Query) ToMove() string {
	return colour(len(q.Moves))
}

func colour(ply int) string {
	if ply%2 == 0 {
		return "black"
	}
	return "white"
}
