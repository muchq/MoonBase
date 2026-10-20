package go_engine

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

// ErrIllegal: the engine refused a move of the game, so the game is not
// one it can play on.
var ErrIllegal = errors.New("illegal move")

// errClosed: the engine's output ended, as when its process exits.
var errClosed = errors.New("engine closed")

// handshakeTimeout bounds the first answer, which a live engine gives at
// once.
const handshakeTimeout = 5 * time.Second

// GnugoArgs runs GNU Go as a GTP engine.
func GnugoArgs() []string { return []string{"--mode", "gtp"} }

// GTP is one engine spoken to over the Go Text Protocol: commands to its
// stdin, each answered on its stdout by "=" or "?" and a blank line. It
// serves one query at a time; Pool shares several.
type GTP struct {
	in     io.WriteCloser
	lines  chan string
	cmd    *exec.Cmd
	done   chan struct{} // closed by Close
	exited chan struct{} // closed when the reader returns
	once   sync.Once
}

// NewGTP starts a conversation and waits out a first answer. `cmd`, if
// any, is the process behind the pipes, killed on Close.
func NewGTP(in io.WriteCloser, out io.Reader, cmd *exec.Cmd) (*GTP, error) {
	g := &GTP{in: in, lines: make(chan string, 64), cmd: cmd, done: make(chan struct{}), exited: make(chan struct{})}
	go func() {
		defer close(g.exited)
		defer close(g.lines)
		scanner := bufio.NewScanner(out)
		for scanner.Scan() {
			select {
			case g.lines <- scanner.Text():
			case <-g.done:
				return
			}
		}
	}()
	ctx, cancel := context.WithTimeout(context.Background(), handshakeTimeout)
	defer cancel()
	if _, err := g.exchange(ctx, []string{"protocol_version"}); err != nil {
		g.Close()
		return nil, err
	}
	return g, nil
}

// Spawn starts `cmd` as an engine process.
func Spawn(cmd *exec.Cmd) (*GTP, error) {
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
	return NewGTP(in, out, cmd)
}

// Ask sets the game up afresh, replays it, and asks: genmove for the side
// to move, or in the scoring final_status_list dead. A vertex or "resign"
// comes back as GNU Go spells it, a pass as "pass", the dead stones space
// separated. The context bounds the whole exchange; an engine that misses
// it should be closed, not reused.
func (g *GTP) Ask(ctx context.Context, q Query) (string, error) {
	commands := []string{
		fmt.Sprintf("boardsize %d", q.Size),
		"clear_board",
		fmt.Sprintf("komi %g", q.Komi),
		fmt.Sprintf("level %d", q.Level),
	}
	for ply, move := range q.Moves {
		commands = append(commands, fmt.Sprintf("play %s %s", colour(ply), move))
	}
	if q.Scoring {
		commands = append(commands, "final_status_list dead")
	} else {
		commands = append(commands, "genmove "+q.ToMove())
	}
	answer, err := g.exchange(ctx, commands)
	if err != nil {
		return "", err
	}
	if !q.Scoring && strings.EqualFold(answer, "pass") {
		return "pass", nil
	}
	return answer, nil
}

// Close ends the conversation and, with it, the process.
func (g *GTP) Close() error {
	g.once.Do(func() { close(g.done) })
	_, _ = io.WriteString(g.in, "quit\n")
	err := g.in.Close()
	if g.cmd != nil && g.cmd.Process != nil {
		_ = g.cmd.Process.Kill()
		_ = g.cmd.Wait()
	}
	return err
}

// exchange sends every command at once and reads an answer for each: the
// last command's, its lines joined by spaces. A refusal of any is
// ErrIllegal, read through so the next exchange starts clean.
func (g *GTP) exchange(ctx context.Context, commands []string) (string, error) {
	if _, err := io.WriteString(g.in, strings.Join(commands, "\n")+"\n"); err != nil {
		return "", err
	}
	var answer string
	refused := false
	for range commands {
		ok, body, err := g.response(ctx)
		if err != nil {
			return "", err
		}
		refused = refused || !ok
		answer = body
	}
	if refused {
		return "", ErrIllegal
	}
	return answer, nil
}

// response reads one answer: "=" or "?", its text, then a blank line.
func (g *GTP) response(ctx context.Context) (bool, string, error) {
	var text []string
	ok := true
	for {
		select {
		case line, open := <-g.lines:
			if !open {
				return false, "", errClosed
			}
			if line == "" {
				if len(text) == 0 {
					continue
				}
				return ok, strings.Join(strings.Fields(strings.Join(text, " ")), " "), nil
			}
			if len(text) == 0 {
				ok = strings.HasPrefix(line, "=")
				line = strings.TrimLeft(line, "=?")
				text = append(text, line, " ")
				continue
			}
			text = append(text, line)
		case <-ctx.Done():
			return false, "", ctx.Err()
		}
	}
}
