package go_engine

import (
	"bufio"
	"context"
	"io"
	"os"
	"os/exec"
	"strings"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// fakeEngine plays the engine's side of a GTP conversation over pipes:
// every command recorded and answered "=", but genmove and
// final_status_list answered with `answer`, and play refused with
// `refusal` when set.
type fakeEngine struct {
	commands chan string
	answer   string
	refusal  string
}

func startFake(t *testing.T, fake *fakeEngine) *GTP {
	t.Helper()
	toEngine, engineIn := io.Pipe()
	engineOut, fromEngine := io.Pipe()
	fake.commands = make(chan string, 1024)
	go func() {
		defer fromEngine.Close()
		scanner := bufio.NewScanner(toEngine)
		for scanner.Scan() {
			line := scanner.Text()
			fake.commands <- line
			switch {
			case line == "quit":
				return
			case strings.HasPrefix(line, "genmove ") || strings.HasPrefix(line, "final_status_list "):
				io.WriteString(fromEngine, "= "+fake.answer+"\n\n")
			case strings.HasPrefix(line, "play ") && fake.refusal != "":
				io.WriteString(fromEngine, "? "+fake.refusal+"\n\n")
			default:
				io.WriteString(fromEngine, "=\n\n")
			}
		}
	}()
	engine, err := NewGTP(engineIn, engineOut, nil)
	require.NoError(t, err)
	t.Cleanup(func() { engine.Close() })
	return engine
}

// The commands sent after the handshake, through the asking one.
func (f *fakeEngine) sent(t *testing.T) []string {
	t.Helper()
	var sent []string
	for {
		select {
		case line := <-f.commands:
			if line == "protocol_version" {
				continue
			}
			sent = append(sent, line)
			if strings.HasPrefix(line, "genmove ") || strings.HasPrefix(line, "final_status_list ") {
				return sent
			}
		case <-time.After(time.Second):
			t.Fatalf("no ask among %v", sent)
		}
	}
}

func TestAnAskSetsTheGameUpThenAsksTheSideToMove(t *testing.T) {
	fake := &fakeEngine{answer: "C3"}
	engine := startFake(t, fake)
	answer, err := engine.Ask(context.Background(), Query{Size: 9, Komi: 5.5, Moves: []string{"E5", "pass", "D4"}, Level: 3})
	require.NoError(t, err)
	assert.Equal(t, "C3", answer)
	assert.Equal(t, []string{
		"boardsize 9", "clear_board", "komi 5.5", "level 3",
		"play black E5", "play white pass", "play black D4",
		"genmove white",
	}, fake.sent(t))
}

func TestInTheScoringTheAskIsForTheDeadStones(t *testing.T) {
	fake := &fakeEngine{answer: "E5 D4\nC3"}
	engine := startFake(t, fake)
	answer, err := engine.Ask(context.Background(), Query{Size: 9, Komi: 6.5, Moves: []string{"E5", "pass", "pass"}, Level: 3, Scoring: true})
	require.NoError(t, err)
	assert.Equal(t, "E5 D4 C3", answer)
	sent := fake.sent(t)
	assert.Equal(t, "final_status_list dead", sent[len(sent)-1])
}

// GNU Go names a pass PASS; the hub reads any case, but one spelling is
// simpler to pin.
func TestAPassIsAnsweredInLowerCase(t *testing.T) {
	engine := startFake(t, &fakeEngine{answer: "PASS"})
	answer, err := engine.Ask(context.Background(), Query{Size: 9, Komi: 6.5, Level: 1})
	require.NoError(t, err)
	assert.Equal(t, "pass", answer)
}

func TestAMoveTheEngineRefusesIsAnIllegalGame(t *testing.T) {
	engine := startFake(t, &fakeEngine{answer: "C3", refusal: "illegal move"})
	_, err := engine.Ask(context.Background(), Query{Size: 9, Komi: 6.5, Moves: []string{"E5"}, Level: 1})
	assert.ErrorIs(t, err, ErrIllegal)
}

func TestAnEngineThatStopsAnsweringMissesTheDeadline(t *testing.T) {
	toEngine, engineIn := io.Pipe()
	engineOut, fromEngine := io.Pipe()
	go func() {
		scanner := bufio.NewScanner(toEngine)
		scanner.Scan()
		io.WriteString(fromEngine, "= 2\n\n")
		for scanner.Scan() {
		}
	}()
	engine, err := NewGTP(engineIn, engineOut, nil)
	require.NoError(t, err)
	defer engine.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 50*time.Millisecond)
	defer cancel()
	_, err = engine.Ask(ctx, Query{Size: 9, Komi: 6.5, Level: 1})
	assert.ErrorIs(t, err, context.DeadlineExceeded)
}

// GNU Go itself, built from source as the image ships it.
func gnugo(t *testing.T) *GTP {
	t.Helper()
	path := os.Getenv("GNUGO")
	require.NotEmpty(t, path, "GNUGO names the binary")
	engine, err := Spawn(exec.Command(path, GnugoArgs()...))
	require.NoError(t, err)
	t.Cleanup(func() { engine.Close() })
	return engine
}

func TestGnuGoAnswersAVertexOnTheBoard(t *testing.T) {
	engine := gnugo(t)
	for _, size := range []int{9, 13, 19} {
		answer, err := engine.Ask(context.Background(), Query{Size: size, Komi: 6.5, Moves: []string{"C3"}, Level: 1})
		require.NoError(t, err, size)
		q := Query{Size: size, Komi: 6.5, Moves: []string{"C3", answer}, Level: 1}
		assert.NoError(t, q.Validate(), "%d: %q", size, answer)
	}
}

// One stone in the middle of a board that is otherwise White's: GNU Go
// counts it dead, and an ask after passes names it.
func TestGnuGoNamesTheDeadStones(t *testing.T) {
	engine := gnugo(t)
	moves := []string{"E5", "C3", "pass", "C7", "pass", "G3", "pass", "G7", "pass", "pass"}
	answer, err := engine.Ask(context.Background(), Query{Size: 9, Komi: 6.5, Moves: moves, Level: 5, Scoring: true})
	require.NoError(t, err)
	assert.Equal(t, "E5", answer)
}

func TestGnuGoRefusesAPlayOnAStone(t *testing.T) {
	engine := gnugo(t)
	_, err := engine.Ask(context.Background(), Query{Size: 9, Komi: 6.5, Moves: []string{"E5", "E5"}, Level: 1})
	assert.ErrorIs(t, err, ErrIllegal)
	// The engine is sound after a refusal: the next ask starts clean.
	answer, err := engine.Ask(context.Background(), Query{Size: 9, Komi: 6.5, Moves: []string{"E5"}, Level: 1})
	require.NoError(t, err)
	assert.NotEqual(t, "E5", answer)
}
