# caddylog

Caddy's JSON access log, one line at a time, into the bounded vocabulary the
stats service keys on (#1150): four user-agent classes with a per-class
name, the nine RFC 9110 methods plus `CUSTOM`, the scanner-probe families,
and a route factor that is the Caddyfile's own `path` matchers, per site,
plus `other`.

The marker lists and probe families are `stats/classify.go`'s, in the same
order, because the first match is the answer. Two pins keep the two
classifiers one classifier: `//domains/platform/libs/otel_contract` reads
both sources and asserts the lists, the class spellings and the method
list equal, and `testdata/*.tsv` is a corpus both suites replay, so a line
lands in the same bucket whichever language reads it. `ROUTES` is pinned to
`deploy/consolidated/Caddyfile` by this crate's own test, which compiles
the Caddyfile in and reads every `path` matcher out of it by site, so a
`path` matcher added to or removed from a site is a test failure here
until the table follows. Only `path` matchers: a site routed by
`path_regexp` or a bare `handle`, which is all of git.muchq.com, is
route-blind on purpose. `SITES` is pinned the same way, to the Caddyfile's
site blocks, so `site_of` is a host factor for every site, route-blind ones
included.

```rust
let line = caddylog::CaddyLine::parse(bytes)?;
let (class, name) = caddylog::agent_of(line.user_agent());
let probe = caddylog::probe_of(&line.request.uri); // Option<&str>
let site = caddylog::site_of(&line.request.host); // a Caddyfile site or "other"
let route = caddylog::route_of(&line.request.host, &line.request.uri); // a matcher or "other"
let method = caddylog::bounded_method(&line.request.method);
```

Two bounds are the caller's. A line is whatever the caller hands `parse`,
so the reader caps line length (the stats pipeline uses 1 MiB). And an
agent name is bounded in length, not in count: for the `Bot` and `Other`
classes it is the client's own product token, so a consumer that keys
anything on those names caps them, as the stats aggregator does.

`client_ip()` reads the address a line carries (`client_ip`, or `remote_ip`
on older lines) and hands the caller a borrowed copy; this crate stores
and aggregates nothing, and where the address goes from there is the
caller's decision.
