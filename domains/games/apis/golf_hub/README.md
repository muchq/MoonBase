# golf_hub — the Golf game hub on smithy-cpp event streams

The golf backend behind muchq.com/golf, on smithy-cpp's streaming stack:
a modeled protocol with generated async handlers (ADR-0021),
`SessionRegistry` fan-out with reconnect grace (ADR-0017/0020/0022), the
JSON-text browser wire (ADR-0018), and ticket auth ahead of the 101.

## The model (two namespaces, per #79)

- `model/games.smithy` — `moonbase.games`: the game-agnostic room layer.
  Session identity (`POST /games/v2/session`), rooms, chat, player info
  with room-scoped stats. Nothing here knows which game a room hosts; a
  future game reuses these shapes verbatim.
- `model/golf_hub.smithy` — `moonbase.golf`: the service, the `Play`
  stream (`/games/v2/golf/play`), and golf's vocabulary nested under one
  `golf` member in each streaming union. Adding a second game later is
  one new member per union; the room layer never changes shape.

## The rules

Four-card golf for 2–4 players: each player peeks at two own cards, a
table-wide reveal countdown opens play, a draw is a peek at the pile top,
take-from-discard commits to a slot in one step (the discard top is
public), a knock gives every other player one final turn, an exhausted
draw pile ends the game, three of a kind scores exactly one card, and
non-knocker ties are shared wins (the knocker takes ties alone). The
engine is `libs/cards/golf`'s immutable `GameState`, which also carries
`hideCards` and `removePlayer` for abandoned seats.

## Redaction

Every game broadcast is per-recipient (`ViewLocked`): own card faces only
at the viewer's peeked indexes, the drawn card only to its holder, other
hands always null slots, scores only at game end. Room state carries
lobby-safe summaries only.

## Scaffold notes / deferred

- Persistence rides `GOLF_HUB_DB_URL` (#1194). Step 1: credentials in
  postgres (`PgTicketVault`, hashed at rest, spend = single-row
  `DELETE ... RETURNING`) — tickets and resume tokens survive deploys.
  Step 2: rooms, membership stats, and live games write through
  (`PgHubStore` — ops staged under the hub lock, applied FIFO by one
  writer; games save serialized `GameState` with a version counter) and
  restore at boot, so a deploy no longer kills games: players resume by
  token into their seat. Memory stays authoritative single-instance;
  fan-out is still process-local (step 3). Unset falls back to
  all-in-memory — dev mode and the test harness.
- Player ids are whimsical (`bouncy-coral-quokka-x9k2`) and double as
  display names. Room and game ids
  are 6-char uppercase codes that ride in permalinks.
- Observability: unary requests ride the shared aura chain (#1185); the
  stream side counts admissions, live sessions, disconnects, grace
  expiries, and the command/event flow (`stream_*`).
- `ALLOWED_ORIGINS` unset admits all origins (local dev); production
  sets the allowlist.
- Deployed behind Caddy at `/games/v2/*` (`deploy/consolidated`); the
  muchq.com golf UI's only backend.
