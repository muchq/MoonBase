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
