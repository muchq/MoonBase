#ifndef DOMAINS_GAMES_APIS_GOLF_HUB_ID_GENERATOR_H
#define DOMAINS_GAMES_APIS_GOLF_HUB_ID_GENERATOR_H

#include <string>

namespace golf_hub {

/// The hub's identifier seam, in the shape of the cards library's Dealer: production randomness
/// behind a small interface so tests can script every id (including forcing the game-code collision
/// path).
class IdGenerator {
 public:
  virtual ~IdGenerator() = default;

  /// A player id; doubles as the display name.
  virtual std::string PlayerId() = 0;

  /// A 6-char uppercase alphanumeric room code: rooms are the shareable
  /// unit (joining a friend's game means joining their room first), so
  /// the id must survive a permalink and a "type this code" exchange.
  virtual std::string RoomId() = 0;

  /// A 6-char uppercase alphanumeric game code, on the same terms as the
  /// room code: it rides in permalinks.
  virtual std::string GameCode() = 0;
};

/// Production ids: whimsical player names ("bouncy-coral-quokka-x9k2",
/// so players never see opaque ids), short codes for rooms and games.
class WhimsicalIdGenerator final : public IdGenerator {
 public:
  std::string PlayerId() override;
  std::string RoomId() override;
  std::string GameCode() override;
};

/// Deterministic ids for tests: player-1, room-1, GAME01, counting up.
class SequentialIdGenerator final : public IdGenerator {
 public:
  std::string PlayerId() override;
  std::string RoomId() override;
  std::string GameCode() override;

 private:
  int players_ = 0;
  int rooms_ = 0;
  int games_ = 0;
};

/// SequentialIdGenerator's twin for a test's second hub instance: a
/// distinct id space (remote-player-1, remote-room-1, RGAME1), because
/// two SequentialIdGenerators would both mint "player-1" and a fresh
/// seat on the second instance would then hold the first instance's
/// player id — a seat conflict for that player's own resume.
class RemoteIdGenerator final : public IdGenerator {
 public:
  std::string PlayerId() override { return "remote-player-" + std::to_string(++players_); }
  std::string RoomId() override { return "remote-room-" + std::to_string(++rooms_); }
  std::string GameCode() override { return "RGAME" + std::to_string(++games_); }

 private:
  int players_ = 0;
  int rooms_ = 0;
  int games_ = 0;
};

}  // namespace golf_hub

#endif
