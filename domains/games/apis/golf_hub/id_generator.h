#ifndef DOMAINS_GAMES_APIS_GOLF_HUB_ID_GENERATOR_H
#define DOMAINS_GAMES_APIS_GOLF_HUB_ID_GENERATOR_H

#include <string>

namespace golf_hub {

/// The hub's identifier seam, mirroring the Go hub's PlayerIDGenerator
/// and the cards library's Dealer: production randomness behind a small
/// interface so tests can script every id (including forcing the
/// game-code collision path).
class IdGenerator {
 public:
  virtual ~IdGenerator() = default;

  /// A player id; doubles as the display name.
  virtual std::string PlayerId() = 0;

  /// An opaque room id.
  virtual std::string RoomId() = 0;

  /// A 6-char uppercase alphanumeric game code — the Go hub's format,
  /// kept for permalink compatibility.
  virtual std::string GameCode() = 0;
};

/// Production ids: whimsical player names ("bouncy-coral-quokka-x9k2",
/// the Go hub's word lists, so beta players never see opaque ids),
/// "r-<hex>" rooms, random game codes.
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

}  // namespace golf_hub

#endif
