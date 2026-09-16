package prom_proxy

import (
	"log"
	"sync"
)

// A service page is 30-40 independent Prometheus queries. This is how many of
// them run at once.
//
// Four, measured against the deployed Prometheus rather than chosen: aggregate
// throughput rises to four in flight and is flat at eight. Past four a wider
// fan-out cannot make a page faster — it only takes the same fixed capacity in
// bigger bites, so one viewer's page load slows every other tile on the
// dashboard. That capacity belongs to one small Prometheus on a shared host,
// and it is not the kind of number to guess at: the sweep behind it, and why
// it buys nothing at all on a 7d page, are in #1556.
//
// Per request, not per process: N concurrent page requests put 4N queries in
// flight. The cache is what keeps N small.
const maxConcurrentQueries = 4

// runBounded calls fn for every index below n, at most maxConcurrentQueries at
// a time, and returns once all of them have finished.
//
// fn runs on an arbitrary goroutine, so it must write only to storage indexed
// by its own i; callers assemble the response from that afterwards, on the
// request's goroutine. That split is what keeps the response deterministic —
// the order tiles and series appear in is fixed by the caller's indexing, not
// by which query answered first.
func runBounded(n int, fn func(i int)) {
	slots := make(chan struct{}, maxConcurrentQueries)
	var wg sync.WaitGroup
	for i := 0; i < n; i++ {
		slots <- struct{}{}
		wg.Add(1)
		go func(i int) {
			defer wg.Done()
			defer func() { <-slots }()
			// net/http recovers a panic on the goroutine it runs the handler
			// on, and nowhere else. Uncontained here, one bad query would take
			// the process down and every other in-flight request with it,
			// where inline it cost a single connection. The job's slot is left
			// as it was, which the callers already read as "no answer".
			defer func() {
				if r := recover(); r != nil {
					log.Printf("prometheus query %d panicked: %v", i, r)
				}
			}()
			fn(i)
		}(i)
	}
	wg.Wait()
}
