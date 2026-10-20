package go_engine

import (
	"context"
	"errors"
	"sync"
)

// Pool shares a fixed number of engine processes among requests, one
// request per engine at a time. An engine that fails or misses its
// deadline is killed and its slot spawns a fresh one on next use, so a
// hung or crashed process costs one request, never the service.
type Pool struct {
	slots chan *GTP // nil: spawn on next use
	spawn func() (*GTP, error)
	once  sync.Once
}

// NewPool starts `size` engines up front, so a broken engine binary fails
// the service at start rather than its first request.
func NewPool(size int, spawn func() (*GTP, error)) (*Pool, error) {
	p := &Pool{slots: make(chan *GTP, size), spawn: spawn}
	for range size {
		engine, err := spawn()
		if err != nil {
			p.Close()
			return nil, err
		}
		p.slots <- engine
	}
	return p, nil
}

func (p *Pool) Ask(ctx context.Context, q Query) (string, error) {
	var engine *GTP
	select {
	case engine = <-p.slots:
	case <-ctx.Done():
		return "", ctx.Err()
	}
	// Out of time already: the engine is sound, so it goes back unasked.
	if err := ctx.Err(); err != nil {
		p.slots <- engine
		return "", err
	}
	if engine == nil {
		fresh, err := p.spawn()
		if err != nil {
			p.slots <- nil
			return "", err
		}
		engine = fresh
	}
	answer, err := engine.Ask(ctx, q)
	if err != nil && !errors.Is(err, ErrIllegal) {
		engine.Close()
		engine = nil
	}
	p.slots <- engine
	return answer, err
}

// Close kills every idle engine.
func (p *Pool) Close() {
	p.once.Do(func() {
		for {
			select {
			case engine := <-p.slots:
				if engine != nil {
					engine.Close()
				}
			default:
				return
			}
		}
	})
}
