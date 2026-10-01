package chess_engine

import (
	"context"
	"errors"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

type stubMover struct {
	got   Query
	move  string
	err   error
	calls int
}

func (s *stubMover) BestMove(_ context.Context, q Query) (string, error) {
	s.calls++
	s.got = q
	return s.move, s.err
}

func post(h http.Handler, body string) *httptest.ResponseRecorder {
	rec := httptest.NewRecorder()
	h.ServeHTTP(rec, httptest.NewRequest(http.MethodPost, "/chess_engine/v1/bestmove", strings.NewReader(body)))
	return rec
}

func TestBestMoveAnswersTheEnginesMove(t *testing.T) {
	stub := &stubMover{move: "e7e8q"}
	rec := post(NewRouter(stub), `{"fen":"7k/4P3/6K1/8/8/8/8/8 w - - 0 1","moves":["g6f6"],"movetimeMs":120,"elo":1600}`)
	require.Equal(t, http.StatusOK, rec.Code, rec.Body.String())
	assert.JSONEq(t, `{"uci":"e7e8q"}`, rec.Body.String())
	require.NotNil(t, stub.got.Elo)
	assert.Equal(t, 1600, *stub.got.Elo)
	assert.Equal(t, []string{"g6f6"}, stub.got.Moves)
	assert.Equal(t, 120, stub.got.MovetimeMs)
}

func TestAnUnsetMovetimeIsTheDefault(t *testing.T) {
	stub := &stubMover{move: "g6f6"}
	rec := post(NewRouter(stub), `{"fen":"7k/4P3/6K1/8/8/8/8/8 w - - 0 1"}`)
	require.Equal(t, http.StatusOK, rec.Code, rec.Body.String())
	assert.Equal(t, DefaultMovetimeMs, stub.got.MovetimeMs)
	assert.Nil(t, stub.got.Elo)
}

func TestARequestTheEngineMustNotSeeIsRefusedBeforeIt(t *testing.T) {
	stub := &stubMover{move: "e7e8q"}
	for _, body := range []string{
		`not json`,
		`{"fen":"7k/8/8/8/8/8/8/K7 w - - 0 1\nquit"}`,
		`{"fen":"7k/4P3/6K1/8/8/8/8/8 w - - 0 1","elo":9000}`,
		`{"fen":"7k/4P3/6K1/8/8/8/8/8 w - - 0 1","surprise":true}`,
	} {
		assert.Equal(t, http.StatusBadRequest, post(NewRouter(stub), body).Code, body)
	}
	assert.Equal(t, 0, stub.calls)
}

func TestNoMoveAndAnEngineFailureAreToldApart(t *testing.T) {
	over := post(NewRouter(&stubMover{err: ErrNoMove}), `{"fen":"7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"}`)
	assert.Equal(t, http.StatusUnprocessableEntity, over.Code)
	down := post(NewRouter(&stubMover{err: errors.New("engine died")}), `{"fen":"7k/4P3/6K1/8/8/8/8/8 w - - 0 1"}`)
	assert.Equal(t, http.StatusServiceUnavailable, down.Code)
}

func TestHealth(t *testing.T) {
	rec := httptest.NewRecorder()
	NewRouter(&stubMover{}).ServeHTTP(rec, httptest.NewRequest(http.MethodGet, "/health", nil))
	assert.Equal(t, http.StatusOK, rec.Code)
}

// The bytes games_hub sends and reads back, character for character as
// chess_engine_cpp's best_move_wire_test pins them: a rename on either side
// fails both.
func TestTheWireIsPinned(t *testing.T) {
	stub := &stubMover{move: "e7e8q"}
	rec := post(NewRouter(stub), `{"elo":1500,"fen":"7k/4P3/6K1/8/8/8/8/8 w - - 0 1","moves":["g6f6","h8g8"],"movetimeMs":300}`)
	require.Equal(t, http.StatusOK, rec.Code, rec.Body.String())
	assert.Equal(t, "{\"uci\":\"e7e8q\"}\n", rec.Body.String())
	require.NotNil(t, stub.got.Elo)
	assert.Equal(t, Query{FEN: "7k/4P3/6K1/8/8/8/8/8 w - - 0 1", Moves: []string{"g6f6", "h8g8"}, MovetimeMs: 300, Elo: stub.got.Elo}, stub.got)
	assert.Equal(t, 1500, *stub.got.Elo)

	full := post(NewRouter(stub), `{"fen":"7k/4P3/6K1/8/8/8/8/8 w - - 0 1","movetimeMs":300}`)
	require.Equal(t, http.StatusOK, full.Code)
	assert.Nil(t, stub.got.Elo)
}
