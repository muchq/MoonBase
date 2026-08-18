#ifndef DOMAINS_GAMES_LIBS_ONE_D4_MOTIFS_MOTIF_H
#define DOMAINS_GAMES_LIBS_ONE_D4_MOTIFS_MOTIF_H

#include <optional>
#include <string_view>

namespace one_d4 {

/// A tactical pattern, spelled as `motif_occurrences.motif` spells it.
///
/// Extraction writes all of these but FORK, which is a grouping of ATTACK
/// rows by attacker and is derived at read time (GameFeatureDao) and
/// nowhere else. CHECKMATE, DISCOVERED_ATTACK, DISCOVERED_CHECK and
/// DOUBLE_CHECK are derived there too, from the same rows — the read path
/// keeps doing that until it can tell a game these detectors scanned from
/// one they did not.
enum class Motif {
  kPin,
  kCrossPin,
  kFork,
  kSkewer,
  kAttack,
  kDiscoveredAttack,
  kDiscoveredCheck,
  kCheck,
  kCheckmate,
  kPromotion,
  kPromotionWithCheck,
  kPromotionWithCheckmate,
  kBackRankMate,
  kSmotheredMate,
  kZugzwang,
  kDoubleCheck,
  kOverloadedPiece,
};

std::string_view ToString(Motif motif);

/// nullopt for an unrecognised name.
std::optional<Motif> MotifFromString(std::string_view name);

}  // namespace one_d4

#endif  // DOMAINS_GAMES_LIBS_ONE_D4_MOTIFS_MOTIF_H
