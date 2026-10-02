package chess_engine

import (
	"bufio"
	"context"
	"errors"
	"fmt"
	"io"
	"os/exec"
	"strings"
	"sync"
	"time"
)

// ErrNoMove: the position has no legal move (mate or stalemate).
var ErrNoMove = errors.New("no legal move")

// errClosed: the engine's output ended, as when its process exits.
var errClosed = errors.New("engine closed")

// handshakeTimeout bounds `uci` and `isready` answers, which a live
// engine gives at once.
const handshakeTimeout = 5 * time.Second

// UCI is one engine spoken to over the Universal Chess Interface: commands
// to its stdin, answers read line by line off its stdout. It serves one
// query at a time; Pool shares several.
type UCI struct {
	in     io.WriteCloser
	lines  chan string
	cmd    *exec.Cmd
	done   chan struct{} // closed by Close
	exited chan struct{} // closed when the reader returns
	once   sync.Once
}

// NewUCI starts a conversation and waits out the handshake. `cmd`, if
// any, is the process behind the pipes, killed on Close. Threads is pinned
// to 1 so Stockfish cannot fan out across every core — the deploy gives
// chess_engine half a CPU, and an unbound Threads is what made bots hang
// at full CPU without ever answering.
func NewUCI(in io.WriteCloser, out io.Reader, cmd *exec.Cmd) (*UCI, error) {
	u := &UCI{in: in, lines: make(chan string, 64), cmd: cmd, done: make(chan struct{}), exited: make(chan struct{})}
	go func() {
		defer close(u.exited)
		defer close(u.lines)
		scanner := bufio.NewScanner(out)
		for scanner.Scan() {
			select {
			case u.lines <- scanner.Text():
			case <-u.done:
				return
			}
		}
	}()
	ctx, cancel := context.WithTimeout(context.Background(), handshakeTimeout)
	defer cancel()
	if err := u.send("uci"); err != nil {
		u.Close()
		return nil, err
	}
	if _, err := u.await(ctx, func(line string) bool { return line == "uciok" }); err != nil {
		u.Close()
		return nil, err
	}
	for _, option := range []string{
		"setoption name Threads value 1",
		"setoption name Hash value 16",
	} {
		if err := u.send(option); err != nil {
			u.Close()
			return nil, err
		}
	}
	if err := u.ready(ctx); err != nil {
		u.Close()
		return nil, err
	}
	return u, nil
}

// Spawn starts `cmd` as an engine process.
func Spawn(cmd *exec.Cmd) (*UCI, error) {
	in, err := cmd.StdinPipe()
	if err != nil {
		return nil, err
	}
	out, err := cmd.StdoutPipe()
	if err != nil {
		return nil, err
	}
	if err := cmd.Start(); err != nil {
		return nil, err
	}
	return NewUCI(in, out, cmd)
}

// BestMove asks for the move: the strength, a new game, the position,
// then `go`, and the answer is the `bestmove` line. The context bounds the
// whole exchange; an engine that misses it should be closed, not reused.
func (u *UCI) BestMove(ctx context.Context, q Query) (string, error) {
	commands := []string{fmt.Sprintf("setoption name UCI_LimitStrength value %t", q.Elo != nil)}
	if q.Elo != nil {
		commands = append(commands, fmt.Sprintf("setoption name UCI_Elo value %d", *q.Elo))
	}
	commands = append(commands, "ucinewgame")
	for _, command := range commands {
		if err := u.send(command); err != nil {
			return "", err
		}
	}
	if err := u.ready(ctx); err != nil {
		return "", err
	}
	position := "position fen " + q.FEN
	if len(q.Moves) > 0 {
		position += " moves " + strings.Join(q.Moves, " ")
	}
	if err := u.send(position); err != nil {
		return "", err
	}
	if err := u.send(fmt.Sprintf("go movetime %d", q.MovetimeMs)); err != nil {
		return "", err
	}
	line, err := u.await(ctx, func(line string) bool { return strings.HasPrefix(line, "bestmove ") })
	if err != nil {
		return "", err
	}
	move := strings.Fields(line)[1]
	if move == "(none)" {
		return "", ErrNoMove
	}
	return move, nil
}

// Close ends the conversation and, with it, the process.
func (u *UCI) Close() error {
	u.once.Do(func() { close(u.done) })
	_ = u.send("quit")
	err := u.in.Close()
	if u.cmd != nil && u.cmd.Process != nil {
		_ = u.cmd.Process.Kill()
		_ = u.cmd.Wait()
	}
	return err
}

func (u *UCI) send(command string) error {
	_, err := io.WriteString(u.in, command+"\n")
	return err
}

func (u *UCI) ready(ctx context.Context) error {
	if err := u.send("isready"); err != nil {
		return err
	}
	_, err := u.await(ctx, func(line string) bool { return line == "readyok" })
	return err
}

// await reads lines until one matches, skipping the rest (info, id, option).
func (u *UCI) await(ctx context.Context, match func(string) bool) (string, error) {
	for {
		select {
		case line, ok := <-u.lines:
			if !ok {
				return "", errClosed
			}
			if match(line) {
				return line, nil
			}
		case <-ctx.Done():
			return "", ctx.Err()
		}
	}
}
