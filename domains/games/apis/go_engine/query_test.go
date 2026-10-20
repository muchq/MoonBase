package go_engine

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

func query() Query {
	return Query{Size: 9, Komi: 6.5, Moves: []string{"E5", "pass", "C3"}, Level: 5}
}

func TestAQueryInBoundsIsValid(t *testing.T) {
	assert.NoError(t, query().Validate())
	for _, size := range []int{9, 13, 19} {
		q := query()
		q.Size = size
		q.Moves = []string{"A1", string("ABCDEFGHJKLMNOPQRST"[size-1]) + "1"}
		assert.NoError(t, q.Validate(), size)
	}
	q := query()
	q.Komi = -150
	q.Level = 10
	q.Moves = nil
	q.Scoring = true
	assert.NoError(t, q.Validate())
}

// Each field becomes part of a GTP line, so nothing that could end one, or
// name a point off the board, reaches the engine.
func TestAQueryTheEngineMustNotSeeIsRefused(t *testing.T) {
	for name, edit := range map[string]func(*Query){
		"a size GTP has no rules for": func(q *Query) { q.Size = 10 },
		"komi off the half-point":     func(q *Query) { q.Komi = 6.25 },
		"komi past its bound":         func(q *Query) { q.Komi = 150.5 },
		"level zero":                  func(q *Query) { q.Level = 0 },
		"level eleven":                func(q *Query) { q.Level = 11 },
		"a newline":                   func(q *Query) { q.Moves = []string{"E5\nquit"} },
		"column I":                    func(q *Query) { q.Moves = []string{"I5"} },
		"a column off the board":      func(q *Query) { q.Moves = []string{"K5"} },
		"a row off the board":         func(q *Query) { q.Moves = []string{"A10"} },
		"row zero":                    func(q *Query) { q.Moves = []string{"A0"} },
		"resign as a move":            func(q *Query) { q.Moves = []string{"resign"} },
		"too many moves":              func(q *Query) { q.Moves = make([]string, maxMoves+1) },
	} {
		q := query()
		edit(&q)
		assert.Error(t, q.Validate(), name)
	}
}

func TestTheSideToMoveAlternatesFromBlack(t *testing.T) {
	q := query()
	assert.Equal(t, "white", q.ToMove())
	q.Moves = q.Moves[:2]
	assert.Equal(t, "black", q.ToMove())
}
