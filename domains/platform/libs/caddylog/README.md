# caddylog

Caddy's JSON access log, one line at a time, into the bounded vocabulary the
stats service keys on (#1150): four user-agent classes with a per-class
bounded name, the nine RFC 9110 methods plus `CUSTOM`, the scanner-probe
families, and a route factor that is the Caddyfile's own `path` matchers
plus `other`.

The marker lists and probe families are `stats/classify.go`'s, in the same
order, because the first match is the answer; `//domains/platform/libs/otel_contract`
pins the two sources equal. `ROUTES` is pinned to `deploy/consolidated/Caddyfile`
by this crate's own test, which compiles the Caddyfile in and reads every
`path` matcher out of it, so a route added to or removed from the gateway is a
test failure here until the list follows.

```rust
let line = caddylog::CaddyLine::parse(bytes)?;
let (class, name) = caddylog::agent_of(line.user_agent());
let probe = caddylog::probe_of(&line.request.uri); // Option<&str>
let route = caddylog::route_of(&line.request.uri); // a matcher or "other"
let method = caddylog::bounded_method(&line.request.method);
```

Client IPs are read (`client_ip()`, falling back to `remote_ip` on older
lines) and nothing here retains them.
