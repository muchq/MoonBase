#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_VOICE_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_VOICE_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "domains/games/apis/games_hub/hub_metrics.h"
#include "moonbase/games/types.h"

namespace games_hub {

/// A room's voice (#1590): who in each room is in its voice, and the
/// WebRTC signals between them. Audio never reaches the hub; this keeps
/// membership and relays each signal to the one peer it names, nothing
/// more.
///
/// Like World, this is the rules and the map and nothing about wires:
/// it stages what each session is owed, in delivery order, and GolfHub,
/// which hosts it as the room stream's `voice` member, hands them to its
/// registry under its lock. Not thread-safe; the owner's lock covers
/// every call. Which room a player is in is the hub's to say.
class Voice {
 public:
  /// A full mesh: each member sends a stream to every other, so the cap
  /// is what one phone's uplink carries.
  static constexpr std::size_t kCapacity = 6;
  static constexpr std::size_t kMaxSdpBytes = 16 * 1024;
  static constexpr std::size_t kMaxCandidateBytes = 1024;

  using Refusal = games_hub::Refusal;

  /// One update owed to one session.
  struct Delivery {
    std::string to;
    moonbase::games::VoiceUpdate update;
  };
  using Deliveries = std::vector<Delivery>;

  /// The ICE servers every roster hands a joiner. Set before serving.
  void SetIceServers(std::vector<moonbase::games::IceServer> servers);

  /// Enters `room_id`'s voice. Stages the joiner's roster (everyone
  /// already in it, and the ICE servers) first, then joined to each of
  /// them. Refused while already in voice or when it is full; a refusal
  /// stages nothing.
  std::optional<Refusal> Join(const std::string& player_id, const std::string& room_id,
                              Deliveries& out);

  /// Removes the player from their room's voice and stages left to the
  /// rest of it; false when they were in none. A deliberate leave, a
  /// room left, and a closed socket alike.
  bool Leave(const std::string& player_id, Deliveries& out);

  /// Relays one signal to the peer it names, as that peer's signal
  /// naming the sender. Refused unless sender and peer are both in the
  /// same room's voice and the signal names the peer's current join
  /// (epoch), with one reason whoever the peer is, so a signal cannot
  /// probe who exists and never reaches a connection made after it was
  /// sent; and refused unless it carries exactly one of a description (an
  /// offer or an answer) or a candidate, within the size limits.
  std::optional<Refusal> Signal(const std::string& player_id,
                                const moonbase::games::SendSignal& signal, Deliveries& out);

 private:
  struct Member {
    std::string room_id;
    std::int64_t epoch;
  };
  /// Everyone in voice, by id, with the room whose voice it is and the
  /// join they are on. One map, so there is no per-room lifecycle; scans
  /// are over at most every player in voice on this instance.
  std::map<std::string, Member> in_voice_;
  /// Each join's epoch; never reused while the process lives.
  std::int64_t next_epoch_ = 1;
  std::vector<moonbase::games::IceServer> ice_servers_;
};

/// VOICE_STUN_URLS as ICE servers: comma-separated STUN urls, one server
/// each, blanks skipped. STUN needs no credentials.
std::vector<moonbase::games::IceServer> StunServersFromList(std::string_view list);

}  // namespace games_hub

#endif  // DOMAINS_GAMES_APIS_GAMES_HUB_VOICE_H
