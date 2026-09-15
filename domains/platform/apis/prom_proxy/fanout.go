package prom_proxy

import "sync"

// A service page is 30-40 Prometheus queries, and they used to run one at a
// time: the page's latency was the sum of every query's (#1556). They are
// independent, so the sum was a choice.
//
// Four, from measuring the deployed Prometheus rather than from taste. Firing
// N of these query streams at api.muchq.com at once and dividing total
// queries by wall time, aggregate throughput on the default range went
// 82 q/s at N=1, 105 at N=2, 131 at N=4, 128 at N=8: about 1.6x available in
// total, all of it collected by four in flight, and nothing past that. A
// wider fan-out would not make a page faster — it would only take the same
// fixed capacity in bigger bites, so that one viewer's page load slows every
// other tile on the dashboard. That ceiling belongs to one small Prometheus
// on a shared host, which is why it is measured here and not assumed.
//
// It buys nothing on a 7d page, where the same sweep is flat at ~30 q/s from
// one in flight to eight: that Prometheus serializes week-long scans on
// something other than CPU, so 31 of them cost ~1.1s however they are
// scheduled. Scheduling is not the lever there; cheaper queries are. See the
// PR for #1556.
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
			fn(i)
		}(i)
	}
	wg.Wait()
}
