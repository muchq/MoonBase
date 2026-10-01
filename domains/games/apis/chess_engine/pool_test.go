package chess_engine

import (
	"bufio"
	"context"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// The test binary run as an engine process: FAKE_UCI says how it answers
// `go` — a move, no move, never, never while chattering, or by exiting.
func TestHelperProcess(t *testing.T) {
	mode := os.Getenv("FAKE_UCI")
	if mode == "" {
		return
	}
	in := bufio.NewScanner(os.Stdin)
	for in.Scan() {
		line := in.Text()
		switch {
		case line == "uci":
			fmt.Println("uciok")
		case line == "isready":
			fmt.Println("readyok")
		case strings.HasPrefix(line, "go "):
			switch mode {
			case "move":
				fmt.Println("bestmove e7e8q")
			case "none":
				fmt.Println("bestmove (none)")
			case "hang":
				select {}
			case "chatty":
				for i := 0; ; i++ {
					fmt.Printf("info depth %d\n", i)
				}
			case "die":
				os.Exit(3)
			}
		case line == "quit":
			os.Exit(0)
		}
	}
	os.Exit(0)
}

// Spawns fake engines, the nth in modes[n] (the last mode thereafter).
func fakeSpawner(modes ...string) (func() (*UCI, error), *atomic.Int32) {
	var n atomic.Int32
	return func() (*UCI, error) {
		i := int(n.Add(1)) - 1
		mode := modes[min(i, len(modes)-1)]
		cmd := exec.Command(os.Args[0], "-test.run=^TestHelperProcess$")
		cmd.Env = append(os.Environ(), "FAKE_UCI="+mode)
		return Spawn(cmd)
	}, &n
}

var kpk = Query{FEN: "7k/4P3/6K1/8/8/8/8/8 w - - 0 1", MovetimeMs: 20}

func TestAPoolAnswersFromItsEngines(t *testing.T) {
	spawn, spawned := fakeSpawner("move")
	pool, err := NewPool(2, spawn)
	require.NoError(t, err)
	defer pool.Close()
	var wg sync.WaitGroup
	for range 6 {
		wg.Add(1)
		go func() {
			defer wg.Done()
			move, err := pool.BestMove(context.Background(), kpk)
			assert.NoError(t, err)
			assert.Equal(t, "e7e8q", move)
		}()
	}
	wg.Wait()
	assert.Equal(t, int32(2), spawned.Load(), "engines are reused, not spawned per request")
}

// A hung engine costs its request, not the service: it is killed at the
// deadline and the slot gets a fresh one.
func TestAHungEngineIsReplaced(t *testing.T) {
	spawn, spawned := fakeSpawner("hang", "move")
	pool, err := NewPool(1, spawn)
	require.NoError(t, err)
	defer pool.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 200*time.Millisecond)
	_, err = pool.BestMove(ctx, kpk)
	cancel()
	assert.True(t, errors.Is(err, context.DeadlineExceeded), "got %v", err)
	move, err := pool.BestMove(context.Background(), kpk)
	require.NoError(t, err)
	assert.Equal(t, "e7e8q", move)
	assert.Equal(t, int32(2), spawned.Load())
}

func TestADeadEngineIsReplaced(t *testing.T) {
	spawn, spawned := fakeSpawner("die", "move")
	pool, err := NewPool(1, spawn)
	require.NoError(t, err)
	defer pool.Close()
	_, err = pool.BestMove(context.Background(), kpk)
	assert.Error(t, err)
	move, err := pool.BestMove(context.Background(), kpk)
	require.NoError(t, err)
	assert.Equal(t, "e7e8q", move)
	assert.Equal(t, int32(2), spawned.Load())
}

// A request waiting on a busy pool gives up at its own deadline.
func TestABusyPoolHoldsAWaiterOnlyUntilItsDeadline(t *testing.T) {
	spawn, _ := fakeSpawner("hang")
	pool, err := NewPool(1, spawn)
	require.NoError(t, err)
	defer pool.Close()
	busy, release := context.WithCancel(context.Background())
	go pool.BestMove(busy, kpk)
	time.Sleep(50 * time.Millisecond)
	ctx, cancel := context.WithTimeout(context.Background(), 50*time.Millisecond)
	defer cancel()
	_, err = pool.BestMove(ctx, kpk)
	assert.True(t, errors.Is(err, context.DeadlineExceeded), "got %v", err)
	release()
}

// Mate or stalemate is an answer, not a fault: the engine stays.
func TestNoMoveKeepsTheEngine(t *testing.T) {
	spawn, spawned := fakeSpawner("none")
	pool, err := NewPool(1, spawn)
	require.NoError(t, err)
	defer pool.Close()
	for range 2 {
		_, err = pool.BestMove(context.Background(), kpk)
		assert.ErrorIs(t, err, ErrNoMove)
	}
	_, _ = pool.BestMove(context.Background(), kpk)
	assert.Equal(t, int32(1), spawned.Load())
}

// A request already out of time when it gets an engine leaves it be.
func TestAnExpiredRequestKeepsTheEngine(t *testing.T) {
	spawn, spawned := fakeSpawner("move")
	pool, err := NewPool(1, spawn)
	require.NoError(t, err)
	defer pool.Close()
	expired, cancel := context.WithCancel(context.Background())
	cancel()
	for range 20 {
		_, err = pool.BestMove(expired, kpk)
		assert.ErrorIs(t, err, context.Canceled)
	}
	move, err := pool.BestMove(context.Background(), kpk)
	require.NoError(t, err)
	assert.Equal(t, "e7e8q", move)
	assert.Equal(t, int32(1), spawned.Load())
}

// An engine still searching when its deadline passes fills the line
// buffer; closing it must still end the reader, not strand it on a send.
func TestClosingAChattyEngineEndsItsReader(t *testing.T) {
	cmd := exec.Command(os.Args[0], "-test.run=^TestHelperProcess$")
	cmd.Env = append(os.Environ(), "FAKE_UCI=chatty")
	engine, err := Spawn(cmd)
	require.NoError(t, err)
	ctx, cancel := context.WithTimeout(context.Background(), 100*time.Millisecond)
	defer cancel()
	_, err = engine.BestMove(ctx, kpk)
	require.ErrorIs(t, err, context.DeadlineExceeded)
	engine.Close()
	select {
	case <-engine.exited:
	case <-time.After(2 * time.Second):
		t.Fatal("the reader is stuck after Close")
	}
}
