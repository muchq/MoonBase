# golf_hub — the Golf game hub on smithy-cpp event streams

The rebuild of `games_ws_backend`'s golf hub (the deployed Go WebSocket
service) on smithy-cpp's Phase 8 streaming stack: a modeled protocol with
generated async handlers (ADR-0021), `SessionRegistry` fan-out with
reconnect grace (ADR-0017/0020/0022), the JSON-text browser wire
(ADR-0018), and ticket auth ahead of the 101. Tracking issue:
MoonBase#1187.

## The model (two namespaces, per #79)

- `model/games.smithy` — `moonbase.games`: the game-agnostic room layer.
  Session identity (`POST /games/v2/session`), rooms, chat, player info
  with room-scoped stats. Nothing here knows which game a room hosts; a
  future game reuses these shapes verbatim.
- `model/golf_hub.smithy` — `moonbase.golf`: the service, the `Play`
  stream (`/games/v2/golf/play`), and golf's vocabulary nested under one
  `golf` member in each streaming union. Adding a second game later is
  one new member per union; the room layer never changes shape.

## The rules (resolved in #1187)

Go-hub semantics with the three corrections where `libs/cards/golf` had
the better rule — an exhausted draw pile ends the game, three of a kind
scores exactly one card, non-knocker ties are shared wins (knocker still
takes ties alone). The engine is `libs/cards/golf`'s immutable
`GameState`, reshaped in place: it gained the Go opening (two own-card
peeks per player, a table-wide reveal countdown, `hideCards`),
`removePlayer` for abandoned seats, and kept its draw mechanic — which is
gameplay-equivalent to the Go hub's draw-then-decide (a draw is a peek at
the pile top; take-from-discard commits to a slot in one step because the
discard top is public).

## Redaction

Every game broadcast is per-recipient (`ViewLocked`): own card faces only
at the viewer's peeked indexes, the drawn card only to its holder, other
hands always null slots, scores only at game end — tighter than v1, which
shipped a player their whole hand during peek windows. Room state carries
lobby-safe summaries only.

## Scaffold notes / deferred

- Tokens are an in-memory `TicketVault` (restart forgets everything —
  matching the Go hub, whose JWT secret rotated on restart). Signed
  tokens are a later hardening if restart-survival ever matters.
- Player ids are whimsical (`bouncy-coral-quokka-x9k2`, the Go
  generator's word lists) and double as display names. Room and game ids
  are 6-char uppercase codes for permalink compatibility.
- Observability: unary requests ride the shared aura chain (#1185); the
  stream side counts admissions, live sessions, disconnects, grace
  expiries, and the command/event flow (`stream_*`, phase 4).
- `ALLOWED_ORIGINS` unset admits all origins (dev parity with the Go
  hub's DEV_MODE); production sets the allowlist.
- Deployed behind Caddy at `/games/v2/*` (phase 4,
  `deploy/consolidated`); the UI's v2 beta switch is `?golf=v2` (phase
  3). Next (#1187): default flip + retirements (phase 5).
