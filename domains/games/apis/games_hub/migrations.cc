#include "domains/games/apis/games_hub/migrations.h"

namespace games_hub {

absl::Status RunMigrations(pg::Client& db) {
  // Credentials (#1194 step 1). Rows hold sha256 hashes of the tokens
  // (hashed in SQL by PgTicketVault), never the tokens themselves. The
  // expires_at indexes keep the purge-on-mint deletes cheap.
  static constexpr const char* kStatements[] = {
      R"sql(CREATE TABLE IF NOT EXISTS tickets (
          ticket_hash text PRIMARY KEY,
          player_id   text NOT NULL,
          expires_at  timestamptz NOT NULL
      ))sql",
      R"sql(CREATE INDEX IF NOT EXISTS idx_tickets_expires_at
          ON tickets (expires_at))sql",
      R"sql(CREATE TABLE IF NOT EXISTS resume_tokens (
          token_hash  text PRIMARY KEY,
          player_id   text NOT NULL,
          expires_at  timestamptz NOT NULL
      ))sql",
      R"sql(CREATE INDEX IF NOT EXISTS idx_resume_tokens_expires_at
          ON resume_tokens (expires_at))sql",
      // Rooms, membership, and games (#1194 step 2). game state is the
      // serialized engine value (game_state_serde schema); version is the
      // optimistic-concurrency counter the hub owns. Game codes are only
      // unique within a room, hence the composite keys. Deleting a room
      // cascades — the hub stages one DeleteRoom when the last member
      // leaves.
      R"sql(CREATE TABLE IF NOT EXISTS rooms (
          room_id text PRIMARY KEY
      ))sql",
      R"sql(CREATE TABLE IF NOT EXISTS room_members (
          room_id      text NOT NULL REFERENCES rooms (room_id) ON DELETE CASCADE,
          player_id    text NOT NULL,
          connected    boolean NOT NULL,
          games_played integer NOT NULL,
          games_won    integer NOT NULL,
          total_score  integer NOT NULL,
          PRIMARY KEY (room_id, player_id)
      ))sql",
      R"sql(CREATE TABLE IF NOT EXISTS games (
          room_id text NOT NULL REFERENCES rooms (room_id) ON DELETE CASCADE,
          game_id text NOT NULL,
          roster  jsonb NOT NULL,
          state   jsonb,
          version bigint NOT NULL,
          PRIMARY KEY (room_id, game_id)
      ))sql",
      // Which game a table plays (#77): golf or castle. Rows from before
      // the column are golf's, which the default says.
      R"sql(ALTER TABLE games
          ADD COLUMN IF NOT EXISTS game text NOT NULL DEFAULT 'golf')sql",
      // The surface a room's world stands on (#1554), in the wire's
      // spelling; rows from before the column are the plane they were.
      R"sql(ALTER TABLE rooms
          ADD COLUMN IF NOT EXISTS geometry jsonb NOT NULL DEFAULT '{"plane":{}}'::jsonb)sql",
      // Room chat (#1226). message_id is the ordering key and the
      // identity is global, not per-room, so one sequence orders every
      // room's history; sent_at is for display. Bodies are bounded here
      // as well as in ValidateChatText because retention counts rows,
      // not bytes. Chat dies with its room and survives its author
      // leaving, so the cascade hangs off rooms and there is no
      // reference to room_members.
      R"sql(CREATE TABLE IF NOT EXISTS room_chat_messages (
          message_id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
          room_id    text NOT NULL REFERENCES rooms (room_id) ON DELETE CASCADE,
          player_id  text NOT NULL,
          body       text NOT NULL CHECK (octet_length(body) BETWEEN 1 AND 500),
          sent_at    timestamptz NOT NULL DEFAULT clock_timestamp()
      ))sql",
      R"sql(ALTER TABLE room_chat_messages
          ALTER COLUMN sent_at SET DEFAULT clock_timestamp())sql",
      R"sql(CREATE INDEX IF NOT EXISTS idx_room_chat_messages_room
          ON room_chat_messages (room_id, message_id))sql",
      // When a live instance last vouched for a room: every write naming
      // it and every heartbeat from an instance holding a seat in it
      // stamps it. The sweep reaps rooms no one has stamped for an hour
      // (#1295's residue). Rows from before the column start at migration.
      R"sql(ALTER TABLE rooms
          ADD COLUMN IF NOT EXISTS last_active_at timestamptz NOT NULL DEFAULT now())sql",
      // The sweep's range scan.
      R"sql(CREATE INDEX IF NOT EXISTS idx_rooms_last_active_at
          ON rooms (last_active_at))sql",
      // A room's finished chess games (#1637), written by the commit that
      // ends each one. game is the finished game's chess_play serde.
      // published is the room's flag when it ended. A table code
      // can be minted again within a room, so (game_id, ordinal) alone
      // does not name a game: the unique index adds the game itself, and
      // every later commit carrying the same ended game conflicts on it.
      R"sql(ALTER TABLE rooms
          ADD COLUMN IF NOT EXISTS chess_published boolean NOT NULL DEFAULT false)sql",
      R"sql(CREATE TABLE IF NOT EXISTS chess_games (
          archive_id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
          room_id    text NOT NULL REFERENCES rooms (room_id) ON DELETE CASCADE,
          game_id    text NOT NULL,
          ordinal    integer NOT NULL,
          game       jsonb NOT NULL,
          published  boolean NOT NULL,
          ended_at   timestamptz NOT NULL DEFAULT clock_timestamp()
      ))sql",
      R"sql(CREATE UNIQUE INDEX IF NOT EXISTS idx_chess_games_once
          ON chess_games (room_id, game_id, ordinal, md5(game::text)))sql",
      R"sql(CREATE INDEX IF NOT EXISTS idx_chess_games_room
          ON chess_games (room_id, archive_id))sql",
      // The public feed: each game archived while its room was published,
      // copied by the same statement under the same id. No room and no
      // table code, so it neither names a room to join nor dies with one;
      // the heartbeat sweeps it by age.
      R"sql(CREATE TABLE IF NOT EXISTS published_chess_games (
          archive_id bigint PRIMARY KEY,
          game       jsonb NOT NULL,
          ended_at   timestamptz NOT NULL
      ))sql",
      R"sql(CREATE INDEX IF NOT EXISTS idx_published_chess_games_ended
          ON published_chess_games (ended_at))sql",
      // A room's round robins (#1647): what each event's creator fixed
      // and moderates, as one body under the hub's optimistic version. A
      // table playing a pairing carries it from its insert, and the commit
      // that archives the table's first game copies it onto the archive
      // with the winner (NULL for a draw), so an event's played results
      // are its tagged rows in chess_games, read without the game.
      R"sql(CREATE TABLE IF NOT EXISTS chess_events (
          room_id  text NOT NULL REFERENCES rooms (room_id) ON DELETE CASCADE,
          event_id text NOT NULL,
          version  bigint NOT NULL,
          body     jsonb NOT NULL,
          PRIMARY KEY (room_id, event_id)
      ))sql",
      R"sql(ALTER TABLE games
          ADD COLUMN IF NOT EXISTS event_id text,
          ADD COLUMN IF NOT EXISTS pairing integer)sql",
      R"sql(ALTER TABLE chess_games
          ADD COLUMN IF NOT EXISTS event_id text,
          ADD COLUMN IF NOT EXISTS pairing integer,
          ADD COLUMN IF NOT EXISTS winner text)sql",
      R"sql(CREATE INDEX IF NOT EXISTS idx_chess_games_event
          ON chess_games (room_id, event_id, archive_id) WHERE event_id IS NOT NULL)sql",
  };
  for (const char* statement : kStatements) {
    if (auto result = db.Exec(statement); !result.ok()) return result.status();
  }
  return absl::OkStatus();
}

}  // namespace games_hub
