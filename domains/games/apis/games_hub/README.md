# games_hub — the games hub on smithy-cpp event streams

The backend behind muchq.com/golf and muchq.com/thoughts (#79), on
smithy-cpp's streaming stack: a modeled protocol with generated async
handlers (ADR-0021), `SessionRegistry` fan-out with reconnect grace
(ADR-0017/0020/0022), the JSON-text browser wire (ADR-0018), and ticket
auth ahead of the 101. One session identity opens either game's stream.

## The model (three namespaces, per #79)

- `model/games.smithy` — `moonbase.games`: the service, session identity
  (`POST /games/v2/session`), the two terminal stream errors, and the
  game-agnostic room layer — rooms, chat, player info with room-scoped
  stats. Nothing here knows which game a room hosts; a future game reuses
  these shapes verbatim.
- `model/golf.smithy` — `moonbase.golf`: the `Play` stream
  (`/games/v2/golf/play`) and golf's vocabulary nested under one `golf`
  member in each streaming union.
- `model/thoughts.smithy` — `moonbase.thoughts`: the `Think` stream
  (`/games/v2/thoughts/play`) with its own command and event unions —
  thoughts has no rooms, so it shares only session identity and the
  `commandRejected` shape with golf.

A new game is one new stream operation on the service and one new model
file. A game that wants rooms and chat copies golf's shape — the room-layer
cases plus one game envelope member per union — not thoughts' flat unions,
which opted out of the room layer and would have to be restructured, not
extended, to join it. Codegen flattens every namespace into
`moonbase::games`, so shape names must be unique across the three files (a
collision gets the foreign namespace's name appended, which nothing here
wants).

## Thoughts

One shared world, no rooms: a joined player is a position on the ground
plane (`[x, 0, z]`, x and z within ±50), an RGB color in 0..1, and a
shape (0 sphere, 1 cube, 2 pyramid). `join` answers the joiner with a
`worldState` of everyone else and tells every other session
`playerJoined`; `move` and `shape` fan out as `playerMoved` and
`shapeChanged`, never echoed; `leave` — or a closed socket, alike — fans
out `playerLeft`. Out-of-bounds values and commands before a join are
refused in-band as `commandRejected`. No persistence and no reconnect
grace: presence is the whole game. `ThoughtsHub` (`thoughts_hub.cc`)
carries it; `HubHandler::Think` forwards to it, sharing only the ticket
vault. Counters carry the `thoughts_` prefix.

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

- Persistence rides `GAMES_HUB_DB_URL` (#1194). Step 1: credentials in
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
  expiries, and the command/event flow (`stream_*` for golf, `thoughts_*`
  for thoughts).
- `ALLOWED_ORIGINS` unset admits all origins (local dev); production
  sets the allowlist.
- Deployed behind Caddy at `/games/v2/*` (`deploy/consolidated`); the
  muchq.com golf UI's only backend, and the thoughts UI's once it moves
  off the v1 Go server (`games_ws_backend`).
