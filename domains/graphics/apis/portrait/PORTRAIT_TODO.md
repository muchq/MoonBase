# Portrait — remaining work

The smithy-cpp migration (https://github.com/muchq/MoonBase/issues/1168) is
through phase 4: `:portrait` (and the `ghcr.io/muchq/portrait` image) now ship
the smithy-cpp server, guarded by golden wire fixtures
(`portrait_smithy_wire_test`), real-render handler tests
(`smithy_handler_test`), middleware tests (`smithy_middleware_test`), and the
old-vs-new differential replay (`portrait_parity_test`).

Resolved along the way, from the old list here: per-IP rate limiting with
429 + Retry-After, request timeouts and body-size limits (Beast transport),
drained SIGTERM shutdown, machine-readable API spec (the Smithy model at
`model/portrait.smithy`), a generated typed client, real 0-255 color
validation, multi-threaded serving, and the tracy::Tracer RNG race.

## Post-soak cleanup (after one release on the smithy binary)

- [ ] Delete the meerkat path: `:portrait_meerkat`, `Main.cc`, the nlohmann
      serde + hand validation in `types.{h,cc}` that the Smithy model
      replaced (`tracer_service` keeps its own request types until then),
      and `portrait_parity_test` — its differential purpose ends with the
      meerkat stack
- [ ] Have `TracerService` hand back raw PNG bytes and cache those — kills
      the base64 encode→decode→re-encode round-trip in `smithy_handler.cc`
      and the manual `imageToBase64` step
- [ ] Consolidate the valid-scene fixtures and test harnesses now spread
      across the four portrait test files, and extract the production
      middleware chain from `portrait_smithy_main.cc` into a shared builder
      so tests exercise the real wiring instead of simplified copies
- [ ] Revisit `meerkat::HttpMetricsManager` as a portrait dep — rehome the
      instruments (e.g. under futility) once meerkat has no other consumers
- [ ] Log the derived client address (ADR-0012) in the access log once the
      meerkat line-shape constraint lifts — since the limiter keys on it,
      a 429's actual bucket is currently not reconstructible from the line,
      which logs only the raw X-Forwarded-For
- [ ] Put the `DeriveClient` source distribution on a dashboard (the
      trust-boundary drift signal: ~100% kUntrustedHeaderIgnored behind
      Caddy means the trust set no longer matches the topology — recipe in
      smithy-cpp docs/production-guide.md). Needs an instrument outside the
      meerkat-parity set, so decide naming with the metrics rehoming above

## Feature backlog (pre-migration items still open)

- [ ] Optional `format` parameter (png/base64/raw) and size/render-time
      fields in the response — an API change; model it in portrait.smithy
- [ ] X-RateLimit-Limit / X-RateLimit-Remaining response headers
- [ ] Rate-limit bypass for authenticated clients (`@httpApiKeyAuth` +
      `RequireApiKeyHeader` exist upstream)
- [ ] Progressive rendering / streaming — blocked on smithy-cpp `@streaming`
      support (upstream phase 8)
- [ ] Scene-complexity limits beyond the current constraint traits (memory
      caps for large renders)
