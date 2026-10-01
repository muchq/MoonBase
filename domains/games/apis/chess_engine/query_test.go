package chess_engine

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

// Every field reaches a line of UCI, so nothing in one may end a line or
// start another: a newline in a FEN would be a second command.
func TestAQueryIsCheckedBeforeItReachesTheEngine(t *testing.T) {
	elo := func(v int) *int { return &v }
	good := Query{FEN: "7k/4P3/6K1/8/8/8/8/8 w - - 0 1", Moves: []string{"e7e8q"}, MovetimeMs: 200, Elo: elo(1500)}
	assert.NoError(t, good.Validate())
	assert.NoError(t, Query{FEN: good.FEN, MovetimeMs: 10}.Validate())

	for name, bad := range map[string]Query{
		"empty fen":       {FEN: "", MovetimeMs: 200},
		"newline in fen":  {FEN: "7k/8/8/8/8/8/8/K7 w - - 0 1\nquit", MovetimeMs: 200},
		"command in fen":  {FEN: "7k/8/8/8/8/8/8/K7 w - - 0 1 moves", MovetimeMs: 200},
		"long fen":        {FEN: string(make([]byte, 200)), MovetimeMs: 200},
		"bad move":        {FEN: good.FEN, Moves: []string{"e7e9"}, MovetimeMs: 200},
		"move injection":  {FEN: good.FEN, Moves: []string{"e7e8\nquit"}, MovetimeMs: 200},
		"too many moves":  {FEN: good.FEN, Moves: make([]string, 1001), MovetimeMs: 200},
		"too quick":       {FEN: good.FEN, MovetimeMs: 9},
		"too slow":        {FEN: good.FEN, MovetimeMs: 5001},
		"elo below range": {FEN: good.FEN, MovetimeMs: 200, Elo: elo(1319)},
		"elo above range": {FEN: good.FEN, MovetimeMs: 200, Elo: elo(3191)},
	} {
		assert.Error(t, bad.Validate(), name)
	}
}
