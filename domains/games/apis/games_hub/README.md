# games_hub — the games hub on smithy-cpp event streams

The backend behind muchq.com/golf and muchq.com/thoughts (#79), and the
castle table (#77), on smithy-cpp's streaming stack: a modeled protocol
with generated async handlers (ADR-0021), `SessionRegistry` fan-out with reconnect grace
(ADR-0017/0020/0022), the JSON-text browser wire (ADR-0018), and ticket
auth ahead of the 101. One session identity opens either stream.

## The model (four namespaces, per #79)

- `model/games.smithy` — `moonbase.games`: the service, session identity
  (`POST /games/v2/session`), the two terminal stream errors, and the
  game-agnostic room layer — rooms, chat, player info with room-scoped
  stats and the member's table (`PlayerInfo.table`: which game, which
  table, pending or in play, absent while idle — how the lobby tells who
  is free, #1490). Apart from `GameSummary.game` and `Table.game`, the
  word that names a table's game for the lobby, nothing here knows which
  game a table plays.
- `model/golf.smithy` — `moonbase.golf`: the `Play` stream
  (`/games/v2/golf/play`) and golf's vocabulary nested under one `golf`
  member in each streaming union. The stream is the room's: castle rides
  it as a second member.
- `model/castle.smithy` — `moonbase.castle`: castle's vocabulary (#77),
  the `castle` member of the same unions. A room hosts tables of either
  game (`GameSummary.game` says which); the shared lifecycle shapes
  (create/join/start/leave and their announcements) are reused, and each
  game's join is refused on the other game's table, so nobody is seated
  at a table whose vocabulary they do not speak. The room-wide
  `gameCreated` is the one event that crosses: a room hears every table
  in that table's own envelope.
- `model/thoughts.smithy` — `moonbase.thoughts`: the `Think` stream
  (`/games/v2/thoughts/play`) with its own command and event unions —
  thoughts keys its worlds by room id but does not use the room layer
  (#1490 phase 3 joins them), so it shares only session identity and the
  `commandRejected` shape with golf.

A new game is one new model file. A game that wants rooms and chat is one
more envelope member on the room stream's unions, the way castle joined —
not thoughts' flat unions, which opted out of the room layer and would
have to be restructured, not extended, to join it. Codegen flattens every namespace into
`moonbase::games`, so shape names must be unique across the four files (a
collision gets the foreign namespace's name appended, which nothing here
wants).

## Thoughts

A world per room (#1490): a joined player is a position on the ground
plane (`[x, 0, z]`, x and z within ±50), an RGB color in 0..1, and a
shape (0 sphere, 1 cube, 2 pyramid), standing in the world of the room
`join` names — or in the plaza, the well-known room `plaza`, when it
names none, which is what muchq.com/thoughts does. Any id names a world;
the room layer is not consulted. `join` answers the joiner with a
`worldState` of everyone else in that world and tells the rest of it
`playerJoined`; `move` and `shape` fan out as `playerMoved` and
`shapeChanged`, never echoed and never past the world's edge; `leave` —
or a closed socket, alike — fans out `playerLeft`. A session that has
not joined hears nothing. Out-of-bounds values, a bad room id, and
commands before a join are refused in-band as `commandRejected`. No
persistence and no reconnect grace: presence is the whole game.
`ThoughtsHub` (`thoughts_hub.cc`) carries it, `GolfHub` (`golf_hub.cc`)
carries the room games, golf and castle, and `GamesHubHandler`
implements the generated service: it mints sessions itself and forwards
each stream to its hub. The two hubs share the ticket vault and nothing
else. Counters carry the `thoughts_` prefix.

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

A castle table redacts by `CastleViewLocked`: own hand faces (everyone's
once the game ends), every face-up row, face-down rows as counts. Golf's
rules, below, are `ViewLocked`'s.

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
  expiries, and the command/event flow (`golf_*` for golf and the room
  layer, `castle_*` for castle's envelope, `thoughts_*` for thoughts,
  `chat_*` for chat).
- `ALLOWED_ORIGINS` unset admits all origins (local dev); production
  sets the allowlist.
- Deployed behind Caddy at `/games/v2/*` (`deploy/consolidated`); the
  muchq.com golf, castle and thoughts UIs' only backend. Castle needs
  no route of its own: it rides the golf play stream, and the origin
  gate is per connection, not per game.
