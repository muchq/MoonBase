package prom_proxy

import (
	"sync"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func TestRunBounded_RunsEveryIndexOnce(t *testing.T) {
	const n = 3 * maxConcurrentQueries
	seen := make([]int, n)
	runBounded(n, func(i int) { seen[i]++ })
	for i, count := range seen {
		assert.Equal(t, 1, count, "index %d ran %d times", i, count)
	}
}

// Both halves of the contract in one probe: work overlaps, and never more
// than maxConcurrentQueries of it. The hold is what makes a serial
// implementation show a peak of 1 rather than passing on timing luck.
func TestRunBounded_OverlapsUpToTheCeiling(t *testing.T) {
	var mu sync.Mutex
	inFlight, peak := 0, 0
	runBounded(4*maxConcurrentQueries, func(int) {
		mu.Lock()
		inFlight++
		if inFlight > peak {
			peak = inFlight
		}
		mu.Unlock()
		time.Sleep(time.Millisecond)
		mu.Lock()
		inFlight--
		mu.Unlock()
	})
	mu.Lock()
	defer mu.Unlock()
	assert.Equal(t, 0, inFlight, "runBounded returned before its work finished")
	assert.Greater(t, peak, 1, "nothing ran concurrently")
	assert.LessOrEqual(t, peak, maxConcurrentQueries, "more than the ceiling ran at once")
}

// The reason callers can assemble their response after the call without any
// synchronization of their own.
func TestRunBounded_WaitsForEveryJob(t *testing.T) {
	const n = 2 * maxConcurrentQueries
	done := make([]bool, n)
	runBounded(n, func(i int) {
		time.Sleep(time.Millisecond)
		done[i] = true
	})
	for i, finished := range done {
		require.True(t, finished, "index %d had not finished when runBounded returned", i)
	}
}

func TestRunBounded_ZeroJobs(t *testing.T) {
	called := false
	runBounded(0, func(int) { called = true })
	assert.False(t, called, "runBounded called fn with no jobs to run")
}

// net/http recovers a panic on the goroutine it runs the handler on, and
// nowhere else: a panic on a goroutine runBounded spawned would take the
// process down, and every other in-flight request with it. Containing it
// leaves the job's slot untouched, which is the same "no answer" a failed
// query leaves.
func TestRunBounded_ContainsAPanickingJob(t *testing.T) {
	const n = 3 * maxConcurrentQueries
	done := make([]bool, n)
	runBounded(n, func(i int) {
		if i%3 == 0 {
			panic("query blew up")
		}
		done[i] = true
	})
	for i := range done {
		assert.Equal(t, i%3 != 0, done[i], "index %d", i)
	}
}

// Every other assertion about the ceiling is written in terms of the ceiling,
// so the constant itself is the one thing they cannot hold: raise it to 40 and
// a games_hub page fires all 31 of its queries at once, against the one small
// Prometheus the number was measured on, with the suite still green. The same
// reason cacheTTL is pinned next door.
func TestRunBounded_TheCeilingIsTheMeasuredOne(t *testing.T) {
	assert.Equal(t, 4, maxConcurrentQueries,
		"measured against the deployed Prometheus (#1556) — raising it is a deliberate act")
}

// Containment without a trace is its own failure: the job's slot stays at the
// zero a real answer could have had, so the log line is all that separates a
// crashed query from a quiet one.
func TestRunBounded_LogsThePanicItContains(t *testing.T) {
	logged := captureLog(t, func() {
		runBounded(3, func(i int) {
			if i == 2 {
				panic("query blew up")
			}
		})
	})
	assert.Contains(t, logged, "panicked")
	assert.Contains(t, logged, "query blew up")
	assert.Contains(t, logged, "2", "the log does not say which query it was")
}
