# golf_hub — the Golf game hub on smithy-cpp event streams

The rebuild of `games_ws_backend`'s golf hub (the deployed Go WebSocket
service) on smithy-cpp's Phase 8 streaming stack: a modeled protocol
(`model/golf_hub.smithy`) with generated async handlers (ADR-0021),
`SessionRegistry` fan-out with reconnect grace (ADR-0017/0020/0022), the
JSON-text browser wire (ADR-0018), and ticket auth ahead of the 101.

## Phases

- **Phase 1 (this)** — session + room lifecycle: `GetSession` mints
  identity, a single-use ticket, and a resume token; `Play` is the one
  WebSocket stream per player (commands up, events down). Rooms are
  create/join/leave/state with per-recipient broadcast; abrupt losses park
  the seat for a 5-minute grace and reconnects resume it (`ResumeOrAdd`);
  invalid commands come back in-band as `commandRejected`, never ending
  the stream. In-memory e2e over the generated client.
- **Phase 2** — game rules + game wire vocabulary. **Gated on an open
  decision** (below).
- **Phase 3** — real-socket parity vs the Go hub (its test corpus as the
  behavioral reference), browser client switch
  (`new WebSocket(url, "smithy.eventstream.v1+json")`), deploy wiring
  (compose + Caddy), soak, then Go-hub golf retirement.

## The open phase-2 decision: which Golf variant?

The two existing implementations play **different games**:

- `games_ws_backend/golf` (deployed; the web UI speaks it): 4 cards
  indexed 0-3, pre-play peek of exactly 2 own cards, draw/take-discard/
  swap/discard-drawn, knock gives every *other* player one final turn,
  pair-cancellation scoring, ties favor the knocker.
- `libs/cards/golf` (the immutable C++ core golf_service/golf_grpc
  share): position-named cards (TL/TR/BL/BR), peek-at-draw-pile mechanic
  with post-peek restrictions, knock ends when the turn returns to the
  knocker, only bottom two cards ever shown.

Phase 2 either ports the Go rules onto a new engine (web-UI compatible,
Go test corpus transfers) or adopts `libs/cards/golf` (better-engineered
core, but changes the deployed game). This model deliberately stops at
rooms so no game wire shape freezes before that call.

## Scaffold notes / deferred

- Tokens are an in-memory `TicketVault` (restart forgets everything —
  matching the Go hub, whose JWT secret rotated on restart). Signed
  tokens are a later hardening if restart-survival ever matters.
- Player ids are `p-<hex>`; the Go hub's whimsical names
  (`bouncy-coral-quokka-x9k2`) are a phase-3 product decision.
- No metrics yet: observability parity waits on hoisting portrait's
  middleware adapters rather than growing a third copy
  (domains/graphics/apis/portrait/PORTRAIT_TODO.md tracks the rehoming).
- `ALLOWED_ORIGINS` unset admits all origins (dev parity with the Go
  hub's DEV_MODE); production sets the allowlist.
