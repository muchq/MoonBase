package go_engine

import (
	"bufio"
	"context"
	"io"
	"strings"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// An engine over pipes that answers the handshake, refuses any play when
// `refuse`, and otherwise dies at its first ask.
func pipeEngine(refuse bool) (*GTP, error) {
	toEngine, engineIn := io.Pipe()
	engineOut, fromEngine := io.Pipe()
	go func() {
		defer toEngine.Close()
		defer fromEngine.Close()
		scanner := bufio.NewScanner(toEngine)
		for scanner.Scan() {
			line := scanner.Text()
			switch {
			case line == "protocol_version":
				io.WriteString(fromEngine, "= 2\n\n")
			case strings.HasPrefix(line, "play ") && refuse:
				io.WriteString(fromEngine, "? illegal move\n\n")
			case strings.HasPrefix(line, "genmove "):
				if !refuse {
					return
				}
				io.WriteString(fromEngine, "= C3\n\n")
			default:
				io.WriteString(fromEngine, "=\n\n")
			}
		}
	}()
	return NewGTP(engineIn, engineOut, nil)
}

func TestAnEngineThatFailsIsReplacedOnNextUse(t *testing.T) {
	spawned := 0
	pool, err := NewPool(1, func() (*GTP, error) { spawned++; return pipeEngine(false) })
	require.NoError(t, err)
	defer pool.Close()
	q := Query{Size: 9, Komi: 6.5, Level: 1}
	_, err = pool.Ask(context.Background(), q)
	assert.Error(t, err)
	_, err = pool.Ask(context.Background(), q)
	assert.Error(t, err)
	assert.Equal(t, 2, spawned)
}

// A game the engine refuses is the game's fault: the engine stays.
func TestAnIllegalGameKeepsTheEngine(t *testing.T) {
	spawned := 0
	pool, err := NewPool(1, func() (*GTP, error) { spawned++; return pipeEngine(true) })
	require.NoError(t, err)
	defer pool.Close()
	q := Query{Size: 9, Komi: 6.5, Moves: []string{"E5"}, Level: 1}
	for range 2 {
		_, err = pool.Ask(context.Background(), q)
		assert.ErrorIs(t, err, ErrIllegal)
	}
	assert.Equal(t, 1, spawned)
}
