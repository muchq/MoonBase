# Portrait — remaining work

The smithy-cpp migration (https://github.com/muchq/MoonBase/issues/1168) is
complete, including the post-soak teardown of the old serving path
(https://github.com/muchq/MoonBase/issues/1174): `:portrait` ships the
smithy-cpp server, guarded by golden wire fixtures
(`portrait_smithy_wire_test`), real-render handler tests
(`smithy_handler_test`), and middleware tests over the production chain
builder (`smithy_middleware_test`).

Resolved along the way, from the old list here: per-IP rate limiting with
429 + Retry-After, request timeouts and body-size limits (Beast transport),
drained SIGTERM shutdown, machine-readable API spec (the Smithy model at
`model/portrait.smithy`), a generated typed client, real 0-255 color
validation, multi-threaded serving, and the tracy::Tracer RNG race.

## Feature backlog

- [ ] Optional `format` parameter (png/base64/raw) and size/render-time
      fields in the response — an API change; model it in portrait.smithy
- [ ] X-RateLimit-Limit / X-RateLimit-Remaining response headers
- [ ] Rate-limit bypass for authenticated clients (`@httpApiKeyAuth` +
      `RequireApiKeyHeader` exist upstream)
- [ ] Progressive rendering / streaming — smithy-cpp `@streaming` landed
      upstream (phase 8); model the streaming response when there's a
      consumer for it
- [ ] Scene-complexity limits beyond the current constraint traits (memory
      caps for large renders)
- [ ] Log the derived client address (ADR-0012) in the access log — the
      pre-migration line-shape constraint has lifted, and since the limiter
      keys on the derived client, a 429's actual bucket is currently not
      reconstructible from the line, which logs only the raw
      X-Forwarded-For
- [ ] Put the `DeriveClient` source distribution on a dashboard (the
      trust-boundary drift signal: ~100% kUntrustedHeaderIgnored behind
      Caddy means the trust set no longer matches the topology — recipe in
      smithy-cpp docs/production-guide.md). Same decision for ADR-0013
      connection-event kind counters, currently log-only WARNING lines
      (`ConnectionEventLog`). Both need instruments outside the inherited
      http_server_* set — decide naming together
