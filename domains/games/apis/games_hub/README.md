# games_hub — the games hub on opal-cpp event streams

The backend behind muchq.com/games — the lobby (#1490), golf, and castle
(#77) — and its /golf and /thoughts pages (#79), on opal-cpp's
streaming stack: a modeled protocol with generated async handlers
(ADR-0021), `SessionRegistry` fan-out with reconnect grace
(ADR-0017/0020/0022), the JSON-text browser wire (ADR-0018), and ticket
auth ahead of the 101. One session identity opens the one stream.

## The model (five namespaces)

- `model/games.smithy` — `moonbase.games`: the service, session identity
  (`POST /games/v2/session`), the two terminal stream errors, the one
  stream — `Play` at `/games/v2/play`, its `GameCommands`/`GameEvents`
  unions carrying the room layer's own cases plus one envelope member
  per tenant (`lobby`, `voice`, `golf`, `castle`) — and the game-agnostic room
  layer — rooms, chat, player info with room-scoped stats and the
  member's table (`PlayerInfo.table`: which game, which
  table, pending or in play, absent while idle — how the lobby tells who
  is free, #1490). Apart from `GameSummary.game` and `Table.game`, the
  word that names a table's game for the lobby, nothing here knows which
  game a table plays.
- `model/golf.smithy` — `moonbase.golf`: golf's vocabulary, nested under
  the `golf` member of each streaming union.
- `model/castle.smithy` — `moonbase.castle`: castle's vocabulary (#77),
  the `castle` member of the same unions. A room hosts tables of either
  game (`GameSummary.game` says which); the shared lifecycle shapes
  (create/join/start/leave and their announcements) are reused, and each
  game's join is refused on the other game's table, so nobody is seated
  at a table whose vocabulary they do not speak. The room-wide
  `gameCreated` is the one event that crosses: a room hears every table
  in that table's own envelope.
- `model/lobby.smithy` — `moonbase.lobby`: the world's shapes — the
  `lobby` member of the room stream (`LobbyAction`, `LobbyUpdate`) and
  what they carry.
- `model/voice.smithy` — `moonbase.voice`: a room's voice (#1590) — the
  `voice` member (`VoiceAction`, `VoiceUpdate`): joining and leaving it,
  and the WebRTC signals between its members.

A new game is one new model file and one more envelope member on the
room stream's unions, the way castle and the lobby joined. Codegen
flattens every namespace into `moonbase::games`, so shape names must be
unique across the five files (a collision gets the foreign namespace's
name appended, which nothing here wants).

## The lobby's world

A world per room (#1490): a joined player is a position on the room's
surface, an RGB color in 0..1, and a shape (0 sphere, 1 cube, 2
pyramid), standing in one room's world. The surface is the room's
(#1554): the ground plane (`[x, 0, z]`, x and z within ±50); the inside
of a sphere, where a position is a point on the wall and the hub snaps
one within a unit of it into place and refuses one farther off; or the
glasshouse, that same plane inside four glass walls at its edges — the
floor's rules to the letter, since the glass is the boundary the plane
already had rather than somewhere to stand, and it is where the tape
lands (below). `createRoom` names the first one (absent: the plane), and
any member changes it with the lobby's `setGeometry`: everyone standing
is placed at the nearest point of the new surface and hears
`geometryChanged` with every placement, on every instance (the row
carries it, `rooms.geometry`). The plaza starts flat and changes the
same way, for this instance's life. `roomState` and `worldState` name
the surface. `join` answers the joiner with a `worldState` of everyone
else in that world and tells the rest of it `playerJoined`; `move` and
`shape` fan out as `playerMoved` and `shapeChanged`, never echoed and
never past the world's edge; `leave` — or a closed socket, alike — fans
out `playerLeft`. A session that has not joined hears nothing.
Out-of-bounds values and commands before a join are refused in-band as
`commandRejected`. Presence is never stored; only the room's surface
is. A slow reader's queue holds at most one move per walker (opal's coalescing
delivery); any other update to that reader starts a new key, so a move
never replaces one queued ahead of it (`MoveCoalescing`, pinned by
`move_coalescing_test`; the hub's use of it by `lobby_e2e_test`). The
rules and the map are `World` (`world.cc`), pinned by `world_test`.

Hosted by `GolfHub` (`golf_hub.cc`) as the room stream's `lobby` member,
the world is the session's: its room's, or the plaza's — the well-known
room `plaza` — while unroomed; a `roomId` on `join` can only agree with
that. Joining, creating, or leaving a room leaves the world (the client
joins the new one), and so does a closed socket, at once, while the
seat parks for grace. Lobby traffic counts on `lobby_commands` and
`lobby_events`, castle's precedent; the world is per instance, like
the registry. `GamesHubHandler` implements the generated service: it
mints sessions itself and forwards the stream to the hub.

## A room's voice

Voice for the people in a room (#1590). Audio never reaches the hub:
browsers connect to each other over WebRTC, a full mesh capped at six
(`Voice::kCapacity`). The hub keeps who is in each room's voice and relays
each negotiation signal to the one peer it names. `join` answers the
joiner with a `roster` (everyone already in voice, and the ICE servers)
and tells each of them `joined`; the joiner offers to each member on its
roster, and later renegotiation is perfect negotiation with the smaller
playerId polite. Every join is a new epoch, carried on `roster` and
`joined`, and a `signal` names the epoch of the join it is for, so an
answer meant for someone who has since left and come back is refused
rather than applied to their new connection. A signal carries exactly one
of an offer/answer description (sdp ≤ 16384 characters) or a candidate
(each field ≤ 1024, `RTCIceCandidate.toJSON()` as it is, nulls read as
absent). Those bounds are the model's (`voice.smithy`): the generated
decoder refuses an event over them, in band, and it costs the command
bucket (opal ADR-0025). A signal reaches its
peer only if both are in the same room's voice — refused with one
reason whoever the peer is, so a signal cannot ask who is online.
Leaving voice, leaving the room (a sibling instance's drop included), and
a closed socket each fan out `left`; a resume does not restore voice,
since the peer connections died with the socket. Voice frames draw from
their own rate bucket, which holds a whole join's candidates. Like the
world, voice is per instance and nothing is stored. `VOICE_STUN_URLS`
(comma-separated) names the STUN servers every roster hands out; unset,
browsers reach each other only on one network. TURN, for browsers STUN
cannot connect, is still to come (#1590). The rules are `Voice`
(`voice.cc`), pinned by `voice_test`; the bytes by `voice_wire_test`.

## deja on the walls

A glasshouse's glass shows deja's tape (#1554, #1150). The hub is deja's
second consumer, and the tape is world state the hub owns: it polls
`GET /deja/v1/recent?after=<seq>` over `//domains/ai/libs/deja_cpp`
every one to two seconds (jittered), dedupes by `seq`, and fans each
new event to the world as a `tape` `LobbyUpdate` the way `playerMoved` goes out. A browser
never talks to deja and never asks where a splat goes: the splat point —
which wall, and where on it as two fractions — is a pure function of
`seq` (`splat.h`), so every client in the room draws the same event on
the same square inch, and no wall height rides the wire. `worldState`
hands a joiner the last 32 and `geometryChanged` hands the same 32 to
whoever was already standing in a room that has just become glass, so
nobody watches a blank wall the rest of the room can see.

The poll is gated on occupancy and best-effort. With nobody standing in a
glasshouse the hub sends deja no HTTP at all — the first joiner opens it,
the last leaver closes it, and a resume after an idle stretch starts from
the newest `seq` rather than replaying the backlog nobody watched. A poll
that comes back from an outage while somebody *is* standing there shows
the newest 32 and advances past the rest, so a recovery is never a burst.
The round trip never happens under `mu_`, one attempt with a two-second
deadline, and deja being down, slow or wrong costs a counted poll and
nothing else. `DEJA_URL` unset leaves the walls blank.

The ring lives in the process, like the rest of the world. Live fan-out
agrees across instances because the placement follows from `seq`, but two
instances that started polling at different times hold different last-32
windows, so what a joiner finds already on the glass depends on which one
answered them. A wall is a mood rather than a log, so that is left alone.

## The room bot

A room chat message that starts with `@bot` (any case, then whitespace or
nothing) is answered by microgpt-serve (#1591), posted into the room's
chat as the reserved player id `microgpt` with `bot: true`. The hub makes
the call, not the browser, so every member sees one reply, it replays in
history like any message, and microgpt is asked once per mention.

`RoomBot` (`room_bot.h`) runs one worker thread, so no stream and never
`mu_` waits on microgpt. After the asker's message commits and is pumped,
the mention is refused if the room already has a request in flight
(`busy`), or if the room's bucket (burst 2, one per 10 s) or the hub's
(burst 4, 4/s — microgpt-serve limits each IP to 5 a second, and this
instance is one IP) is empty (`rate_limited`). The prompt is the room's
last 8 messages up to the mention, within 1500 bytes and never dropping
the mention itself: a player's turn is `user` as `<playerId>: <text>` with
any `@bot` stripped, and the bot's own replies are `assistant`. The call
is `POST /microgpt/v1/chat` over `//domains/ai/libs/microgpt_cpp`, one
attempt, 5 s, `max_tokens` 60.

The reply is cut to 500 bytes on a character boundary and appended on the
asker's membership, so an asker who left meanwhile gets nothing posted.
microgpt down, slow, refusing or saying nothing posts nothing either:
every outcome is a `bot_requests{result}` count (`ok`, `empty`, `busy`,
`rate_limited`, `unreachable`, `error`) and `bot_latency_us`, never an
error in chat. `MICROGPT_URL` unset leaves the bot off, and a mention is
only chat.

## Game events

Seven events, one JSON line each, written to `GAME_EVENT_LOG_DIR` for the
stats pipeline to read back out of S3 (#1571): `room_created` (the
surface it chose), `room_joined` (the room's size once the joiner was in
it), `geometry_changed` (the surface it became), `chat_message` (the
room's size when it was said), `game_started` (variant, seats dealt),
`game_finished` (variant, outcome, seats still held) and `room_closed`.
Together they are the shape of an evening — somebody made a room,
somebody else walked in, they reshaped the world they were standing in,
they talked, a table started, the table ended, the last of them left.

Every line carries the room it happened in, which is what makes this a
session rather than seven counters: one room's evening reads back in
order, and what no single line can say — how long a room lasted, how long
a game took, how many tables it got through — is a join away. It is the
one high-cardinality field, for stitching lines together and not for
grouping, and the one that has to be made safe rather than assumed to be:
`RoomTag` reduces anything outside `[A-Za-z0-9_-]`, and every id the hub
mints passes through it unchanged. A room id is also a share link — it
reaches S3 an hour or more later, by which time an emptied room is gone.

No message text, no sphere radius, no player id, no game code. A refused
chat, join or geometry is no event: the counters carry the rejections,
the archive carries what happened. `surface` is `SurfaceKindName`'s
word, the same one the wire and the stored row use. Counters answer "how is the hub
doing right now"; these answer "what was played last March", which
Prometheus drops. `game_events.h` is the vocabulary and
`//domains/platform/libs/event_log` the writer; unset, the hub records
nothing.

Nothing here is visible at the edge. A session opens one socket and every
room, world, table, game and message rides that one connection, so the
access log counts a connection and never a game.

`game_started` is the only place a table's size is recorded while it is
still whole. `game_finished`'s `players` is the seats *still held*, which
for an abandonment is the moment the second-to-last one left — so it
reads 1 for nearly every abandoned game, and is not the table's size.

`room_closed` carries nothing: a room closes empty by definition, and
what it held is the lines before it. Against `room_created`, the
difference over a day is the rooms still open.

The finished line comes from `CommitEntryLocked`, the one place that
knows both that the finish landed and that this instance is what ended
the game. Not from `StageGameOverLocked`, which every instance holding
the room runs off the terminal row; and not after a commit that came back
unavailable, which leaves a live row somebody finishes again. Either
would count one game twice. `room_closed` is the same rule one level up:
the instance that empties a room writes it, and the ones that later read
the row gone drop the room in silence.

It is not the same guarantee, though. A finish is recorded only once its
commit has landed; a close is recorded before the `DeleteRoom` it stages
has flushed. A crash in that window restores the room, and its next
emptying writes a second `room_closed` under the same id. Making it
durable would mean a synchronous delete on the room teardown path, which
is a lot to ask of the hub for an archive line — so a reader counting
rooms should treat a repeated close as the one thing here that can
repeat.

## Ops: rooms in Postgres

What the instances have written, not what they hold in memory: world
presence and voice are never stored. A room's `last_active_at` is stamped
by every write naming it and by each holding instance's heartbeat
(`kRoomHeartbeat`, 1 min); the sweep deletes rooms unstamped for
`kRoomStaleAfter` (1 h), cascading to members, tables and chat. A room's
channel is `room_<room_id>`.

On the host, in the deploy user's home directory (where `deploy.sh` puts the
compose files):

```bash
sudo docker compose -f compose.yaml -f docker-compose.observability.yml \
  exec shared_postgres psql -U games_hub -d games_hub
```

Every room, newest stamp first:

```sql
SELECT r.room_id,
       (SELECT k FROM jsonb_object_keys(r.geometry) k LIMIT 1) AS surface,
       count(DISTINCT m.player_id) FILTER (WHERE m.connected) AS connected,
       count(DISTINCT m.player_id) AS members,
       count(DISTINCT g.game_id) AS tables,
       date_trunc('second', now() - r.last_active_at) AS since_stamp
FROM rooms r
LEFT JOIN room_members m USING (room_id)
LEFT JOIN games g USING (room_id)
GROUP BY r.room_id
ORDER BY r.last_active_at DESC;
```

One room's members, tables and recent chat (`dealt` is false for a table
still waiting for players):

```sql
SELECT player_id, connected, games_played, games_won, total_score
FROM room_members WHERE room_id = 'ABC123' ORDER BY player_id;

SELECT game_id, game, version, state IS NOT NULL AS dealt,
       jsonb_array_length(roster) AS seats, roster
FROM games WHERE room_id = 'ABC123' ORDER BY game_id;

SELECT message_id, sent_at, player_id, body
FROM room_chat_messages WHERE room_id = 'ABC123'
ORDER BY message_id DESC LIMIT 20;
```

Where a player is seated:

```sql
SELECT m.room_id, m.connected, g.game_id, g.game
FROM room_members m
LEFT JOIN games g ON g.room_id = m.room_id AND g.roster ? m.player_id
WHERE m.player_id = 'bouncy-coral-quokka-x9k2';
```

What the next sweep takes:

```sql
SELECT room_id, last_active_at
FROM rooms WHERE last_active_at < now() - interval '1 hour'
ORDER BY last_active_at;
```

Live credentials:

```sql
SELECT 'tickets' AS kind, count(*) FILTER (WHERE expires_at > now()) AS live, count(*) AS total
FROM tickets
UNION ALL
SELECT 'resume_tokens', count(*) FILTER (WHERE expires_at > now()), count(*)
FROM resume_tokens;
```

Watch a room's wakes (commits, sweeps) from `psql`: `LISTEN room_ABC123;`,
then any statement prints what arrived.

Deleting a room by hand is the sweep's own statement for one room. The
delete cascades; the notify makes every instance holding the room drop
it and tell its players:

```sql
WITH gone AS (DELETE FROM rooms WHERE room_id = 'ABC123' RETURNING room_id)
SELECT pg_notify('room_' || room_id, 'sweep') FROM gone;
```

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

## Notes

- Persistence rides `GAMES_HUB_DB_URL` (#1194): credentials
  (`PgTicketVault`, hashed at rest, spend = single-row
  `DELETE ... RETURNING`), rooms, membership stats and live games
  (`PgHubStore` — rooms and members staged under the hub lock and applied
  FIFO by one writer; game commits conditional on a version counter), and
  chat (`PgChatStore`). A deploy keeps games: players resume by token into
  their seat. The database is every game's authority; each commit's
  NOTIFY wakes the other instances holding the room. Unset is
  all-in-memory — dev mode and the test harness.
- Player ids are whimsical (`bouncy-coral-quokka-x9k2`) and double as
  display names. Room and game ids
  are 6-char uppercase codes that ride in permalinks.
- Observability: unary requests ride the shared aura chain (#1185); the
  stream side counts admissions, live sessions, disconnects, grace
  expiries, and the command/event flow (`hub_*` for the room layer —
  sessions, seats, refusals, its own commands and events — `golf_*`,
  `castle_*`, `lobby_*` and `voice_*` for each tenant's envelope, `chat_*` for
  chat). The tape rides the lobby's prefix: `lobby_tape_polls{result}`,
  `lobby_tape_splats`, and the `lobby_tape_poller_active` gauge, which is
  1 exactly while a glasshouse is occupied.
- `ALLOWED_ORIGINS` unset admits all origins (local dev); production
  sets the allowlist.
- Deployed behind Caddy at `/games/v2/*` (`deploy/consolidated`); the
  muchq.com games, golf, castle and thoughts UIs' only backend. No game
  has a route of its own: all ride the one play stream, and the origin
  gate is per connection, not per game.
