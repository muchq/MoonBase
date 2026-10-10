#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_ROUND_ROBIN_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_ROUND_ROBIN_H

#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"

namespace games_hub {

/// A chess round robin's pairings and standings (#1647): everyone plays
/// everyone once, on the same terms. Pure: the hub stores and serves it.

inline constexpr std::size_t kMinEventEntrants = 3;
/// Eight entrants is 28 games: an evening, not a season.
inline constexpr std::size_t kMaxEventEntrants = 8;

enum class PairingResult { kWhite, kBlack, kDraw };

struct Pairing {
  /// 1-based. Rounds only order a player's list: pairings are played in
  /// whatever order their players are free.
  int round = 0;
  std::string white;
  std::string black;
  std::optional<PairingResult> result = std::nullopt;
  /// Recorded by the creator rather than played. Scores as the game would.
  bool forfeit = false;
  bool operator==(const Pairing&) const = default;
};

/// The circle method over `entrants` in order: n-1 rounds, or n with one
/// bye each when n is odd. Every pair meets once, nobody plays twice in a
/// round, and each player's colours alternate as far as the round count
/// allows. Refuses fewer than kMinEventEntrants, more than
/// kMaxEventEntrants, a blank name or a name twice.
absl::StatusOr<std::vector<Pairing>> RoundRobinPairings(const std::vector<std::string>& entrants);

/// An unplayed pairing with a withdrawn player. A played one stands.
bool Voided(const Pairing& pairing, const std::set<std::string>& withdrawn);

/// The index of the pairing between `a` and `b`, either colour, if it's
/// still to play. Pairings are fixed at creation, so the index names it.
std::optional<std::size_t> OpenPairing(const std::vector<Pairing>& pairings,
                                       const std::set<std::string>& withdrawn, std::string_view a,
                                       std::string_view b);

struct Standing {
  std::string player;
  double points = 0;
  /// The points of every opponent beaten plus half those of every opponent
  /// drawn. Forfeits count as the result they record.
  double sonneborn_berger = 0;
  /// 1-based. Players level on points, Sonneborn-Berger and their games
  /// against each other share a place, and the place after them skips.
  int place = 0;
  bool withdrawn = false;
  bool operator==(const Standing&) const = default;
};

/// The table, best first: points, then Sonneborn-Berger, then points
/// scored among the players still tied, then a shared place. Ties keep
/// entry order. Withdrawn players stay in, with the games they played.
std::vector<Standing> Standings(const std::vector<std::string>& entrants,
                                const std::vector<Pairing>& pairings,
                                const std::set<std::string>& withdrawn);

}  // namespace games_hub

#endif
