package chess_engine

import (
	"bufio"
	"context"
	"errors"
	"io"
	"strings"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// fakeEngine plays the engine's side of a UCI conversation over pipes:
// it answers the handshake and isready, records every command it is
// sent, and answers `go` with whatever reply says.
type fakeEngine struct {
	commands chan string
	reply    func(goLine string) []string
}

func startFake(t *testing.T, reply func(goLine string) []string) (*UCI, *fakeEngine) {
	t.Helper()
	toEngine, engineIn := io.Pipe()
	engineOut, fromEngine := io.Pipe()
	fake := &fakeEngine{commands: make(chan string, 64), reply: reply}
	go func() {
		defer fromEngine.Close()
		scanner := bufio.NewScanner(toEngine)
		for scanner.Scan() {
			line := scanner.Text()
			fake.commands <- line
			switch {
			case line == "uci":
				io.WriteString(fromEngine, "id name Fake\noption name UCI_Elo type spin default 1320 min 1320 max 3190\nuciok\n")
			case line == "isready":
				io.WriteString(fromEngine, "readyok\n")
			case strings.HasPrefix(line, "go "):
				for _, out := range fake.reply(line) {
					io.WriteString(fromEngine, out+"\n")
				}
			case line == "quit":
				return
			}
		}
	}()
	engine, err := NewUCI(engineIn, engineOut, nil)
	require.NoError(t, err)
	t.Cleanup(func() { engine.Close() })
	return engine, fake
}

// The commands sent after the handshake, up to and including the go.
func (f *fakeEngine) sentThroughGo(t *testing.T) []string {
	t.Helper()
	var sent []string
	for {
		select {
		case line := <-f.commands:
			if line == "uci" || (len(sent) == 0 && line == "isready") {
				continue
			}
			sent = append(sent, line)
			if strings.HasPrefix(line, "go ") {
				return sent
			}
		case <-time.After(2 * time.Second):
			t.Fatalf("no go command; sent so far: %v", sent)
		}
	}
}

func bestmove(uci string) func(string) []string {
	return func(string) []string {
		return []string{"info depth 1 score cp 900", "bestmove " + uci + " ponder h8g8"}
	}
}

func TestAPositionWithMovesAndAStrengthIsAskedAsUCI(t *testing.T) {
	engine, fake := startFake(t, bestmove("e7e8q"))
	elo := 1500
	move, err := engine.BestMove(context.Background(), Query{
		FEN: "7k/4P3/6K1/8/8/8/8/8 w - - 0 1", Moves: []string{"g6f6", "h8g8"}, MovetimeMs: 150, Elo: &elo,
	})
	require.NoError(t, err)
	assert.Equal(t, "e7e8q", move)
	assert.Equal(t, []string{
		"setoption name UCI_LimitStrength value true",
		"setoption name UCI_Elo value 1500",
		"ucinewgame",
		"isready",
		"position fen 7k/4P3/6K1/8/8/8/8/8 w - - 0 1 moves g6f6 h8g8",
		"go movetime 150",
	}, fake.sentThroughGo(t))
}

func TestFullStrengthAndNoMovesAskThePositionAlone(t *testing.T) {
	engine, fake := startFake(t, bestmove("g6f6"))
	_, err := engine.BestMove(context.Background(), Query{FEN: "7k/4P3/6K1/8/8/8/8/8 w - - 0 1", MovetimeMs: 50})
	require.NoError(t, err)
	assert.Equal(t, []string{
		"setoption name UCI_LimitStrength value false",
		"ucinewgame",
		"isready",
		"position fen 7k/4P3/6K1/8/8/8/8/8 w - - 0 1",
		"go movetime 50",
	}, fake.sentThroughGo(t))
}

func TestAPositionWithNoMoveIsSaidSo(t *testing.T) {
	engine, _ := startFake(t, func(string) []string { return []string{"bestmove (none)"} })
	_, err := engine.BestMove(context.Background(), Query{FEN: "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1", MovetimeMs: 50})
	assert.ErrorIs(t, err, ErrNoMove)
}

// An engine that never answers is the caller's deadline, not a hang.
func TestASilentEngineEndsAtTheDeadline(t *testing.T) {
	engine, _ := startFake(t, func(string) []string { return nil })
	ctx, cancel := context.WithTimeout(context.Background(), 100*time.Millisecond)
	defer cancel()
	_, err := engine.BestMove(ctx, Query{FEN: "7k/4P3/6K1/8/8/8/8/8 w - - 0 1", MovetimeMs: 50})
	assert.True(t, errors.Is(err, context.DeadlineExceeded), "got %v", err)
}
