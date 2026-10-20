package go_engine

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

type stubAsker struct {
	got    Query
	answer string
	err    error
	calls  int
}

func (s *stubAsker) Ask(_ context.Context, q Query) (string, error) {
	s.calls++
	s.got = q
	return s.answer, s.err
}

func post(h http.Handler, body string) *httptest.ResponseRecorder {
	rec := httptest.NewRecorder()
	h.ServeHTTP(rec, httptest.NewRequest(http.MethodPost, "/go_engine/v1/genmove", strings.NewReader(body)))
	return rec
}

func TestGenmoveAnswersTheEngine(t *testing.T) {
	stub := &stubAsker{answer: "C3"}
	rec := post(NewRouter(stub), `{"size":9,"komi":5.5,"moves":["E5"],"level":3}`)
	require.Equal(t, http.StatusOK, rec.Code, rec.Body.String())
	assert.JSONEq(t, `{"answer":"C3"}`, rec.Body.String())
	assert.Equal(t, Query{Size: 9, Komi: 5.5, Moves: []string{"E5"}, Level: 3}, stub.got)
}

func TestARequestTheEngineMustNotSeeIsRefusedBeforeIt(t *testing.T) {
	stub := &stubAsker{answer: "C3"}
	for _, body := range []string{
		`not json`,
		`{"size":9,"komi":5.5,"moves":["E5\nquit"],"level":3}`,
		`{"size":9,"komi":5.5,"level":30}`,
		`{"size":9,"komi":5.5,"level":3,"surprise":true}`,
	} {
		assert.Equal(t, http.StatusBadRequest, post(NewRouter(stub), body).Code, body)
	}
	assert.Equal(t, 0, stub.calls)
}

func TestAnIllegalGameAndAnEngineFailureAreToldApart(t *testing.T) {
	illegal := post(NewRouter(&stubAsker{err: ErrIllegal}), `{"size":9,"komi":5.5,"moves":["E5","E5"],"level":3}`)
	assert.Equal(t, http.StatusUnprocessableEntity, illegal.Code)
	down := post(NewRouter(&stubAsker{err: errors.New("engine died")}), `{"size":9,"komi":5.5,"level":3}`)
	assert.Equal(t, http.StatusServiceUnavailable, down.Code)
}

func TestHealth(t *testing.T) {
	rec := httptest.NewRecorder()
	NewRouter(&stubAsker{}).ServeHTTP(rec, httptest.NewRequest(http.MethodGet, "/health", nil))
	assert.Equal(t, http.StatusOK, rec.Code)
}

// The bytes games_hub sends and reads back, character for character as
// go_engine_cpp's genmove_wire_test pins them: a rename on either side
// fails both.
func TestTheWireIsPinned(t *testing.T) {
	stub := &stubAsker{answer: "C3"}
	rec := post(NewRouter(stub), `{"komi":6.5,"level":7,"moves":["E5","pass"],"size":9}`)
	require.Equal(t, http.StatusOK, rec.Code, rec.Body.String())
	assert.Equal(t, "{\"answer\":\"C3\"}\n", rec.Body.String())
	assert.Equal(t, Query{Size: 9, Komi: 6.5, Moves: []string{"E5", "pass"}, Level: 7}, stub.got)

	stub.answer = "E5 D4"
	scoring := post(NewRouter(stub), `{"komi":6.5,"level":7,"moves":["E5","pass","pass"],"scoring":true,"size":9}`)
	require.Equal(t, http.StatusOK, scoring.Code)
	assert.Equal(t, "{\"answer\":\"E5 D4\"}\n", scoring.Body.String())
	assert.True(t, stub.got.Scoring)
}
