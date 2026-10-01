package chess_engine

import (
	"context"
	"encoding/json"
	"errors"
	"net/http"
	"time"

	"github.com/muchq/moonbase/domains/platform/libs/mucks"
)

// Mover is what the handler asks; Pool is the real one.
type Mover interface {
	BestMove(ctx context.Context, q Query) (string, error)
}

// slack is how long past its movetime a query may take — waiting for an
// engine, the handshake, the answer — before it is given up.
const slack = 2 * time.Second

type bestMoveResponse struct {
	UCI string `json:"uci"`
}

// NewRouter serves POST /chess_engine/v1/bestmove: a Query in, {"uci"}
// out. 400 for a query the engine must not see, 422 for a position with
// no move, 503 when no engine answered in time.
func NewRouter(engine Mover) *mucks.Mucks {
	router := mucks.NewJsonMucks()
	router.HandleFunc("GET /health", func(w http.ResponseWriter, _ *http.Request) {
		json.NewEncoder(w).Encode(map[string]string{"status": "ok"})
	})
	router.HandleFunc("POST /chess_engine/v1/bestmove", func(w http.ResponseWriter, r *http.Request) {
		decoder := json.NewDecoder(http.MaxBytesReader(w, r.Body, 64<<10))
		decoder.DisallowUnknownFields()
		var q Query
		if err := decoder.Decode(&q); err != nil {
			mucks.JsonError(w, mucks.NewBadRequest(err.Error()))
			return
		}
		if q.MovetimeMs == 0 {
			q.MovetimeMs = DefaultMovetimeMs
		}
		if err := q.Validate(); err != nil {
			mucks.JsonError(w, mucks.NewBadRequest(err.Error()))
			return
		}
		ctx, cancel := context.WithTimeout(r.Context(), time.Duration(q.MovetimeMs)*time.Millisecond+slack)
		defer cancel()
		move, err := engine.BestMove(ctx, q)
		switch {
		case errors.Is(err, ErrNoMove):
			mucks.JsonError(w, mucks.Problem{StatusCode: 422, ErrorCode: 422, Message: "No Move", Detail: "the position has no legal move"})
		case err != nil:
			mucks.JsonError(w, mucks.Problem{StatusCode: 503, ErrorCode: 503, Message: "Engine Unavailable", Detail: "no engine answered in time"})
		default:
			json.NewEncoder(w).Encode(bestMoveResponse{UCI: move})
		}
	})
	return router
}
