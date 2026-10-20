package go_engine

import (
	"context"
	"encoding/json"
	"errors"
	"log"
	"net/http"
	"time"

	"github.com/muchq/moonbase/domains/platform/libs/mucks"
)

// Asker is what the handler asks; Pool is the real one.
type Asker interface {
	Ask(ctx context.Context, q Query) (string, error)
}

// askTimeout is how long a query may take — waiting for an engine,
// replaying the game, the answer — before it is given up. On a full core
// GNU Go's level-10 19×19 middle-game move took up to 1.7 s, and its dead
// stones at a game's end (both sides passed) up to 1.4 s; the deploy's half
// a core doubles that.
const askTimeout = 10 * time.Second

type genmoveResponse struct {
	Answer string `json:"answer"`
}

// NewRouter serves POST /go_engine/v1/genmove: a Query in, {"answer"}
// out. 400 for a query the engine must not see, 422 for a game the engine
// refuses a move of, 503 when no engine answered in time.
func NewRouter(engine Asker) *mucks.Mucks {
	router := mucks.NewJsonMucks()
	router.HandleFunc("GET /health", func(w http.ResponseWriter, _ *http.Request) {
		json.NewEncoder(w).Encode(map[string]string{"status": "ok"})
	})
	router.HandleFunc("POST /go_engine/v1/genmove", func(w http.ResponseWriter, r *http.Request) {
		decoder := json.NewDecoder(http.MaxBytesReader(w, r.Body, 64<<10))
		decoder.DisallowUnknownFields()
		var q Query
		if err := decoder.Decode(&q); err != nil {
			mucks.JsonError(w, mucks.NewBadRequest(err.Error()))
			return
		}
		if err := q.Validate(); err != nil {
			mucks.JsonError(w, mucks.NewBadRequest(err.Error()))
			return
		}
		ctx, cancel := context.WithTimeout(r.Context(), askTimeout)
		defer cancel()
		started := time.Now()
		answer, err := engine.Ask(ctx, q)
		took := time.Since(started).Round(time.Millisecond)
		switch {
		case errors.Is(err, ErrIllegal):
			log.Printf("genmove size=%d moves=%d level=%d scoring=%t: illegal after %s",
				q.Size, len(q.Moves), q.Level, q.Scoring, took)
			mucks.JsonError(w, mucks.Problem{StatusCode: 422, ErrorCode: 422, Message: "Illegal Game", Detail: "the engine refused a move of the game"})
		case err != nil:
			log.Printf("genmove size=%d moves=%d level=%d scoring=%t: %v after %s",
				q.Size, len(q.Moves), q.Level, q.Scoring, err, took)
			mucks.JsonError(w, mucks.Problem{StatusCode: 503, ErrorCode: 503, Message: "Engine Unavailable", Detail: "no engine answered in time"})
		default:
			log.Printf("genmove size=%d moves=%d level=%d scoring=%t: %q after %s",
				q.Size, len(q.Moves), q.Level, q.Scoring, answer, took)
			json.NewEncoder(w).Encode(genmoveResponse{Answer: answer})
		}
	})
	return router
}
