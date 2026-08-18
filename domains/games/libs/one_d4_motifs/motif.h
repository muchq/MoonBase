#ifndef DOMAINS_GAMES_LIBS_ONE_D4_MOTIFS_MOTIF_H
#define DOMAINS_GAMES_LIBS_ONE_D4_MOTIFS_MOTIF_H

#include <optional>
#include <string_view>

namespace one_d4 {

/// A tactical pattern, spelled as `motif_occurrences.motif` spells it.
///
/// The full set, not just what extraction writes: FORK, CHECKMATE,
/// DISCOVERED_ATTACK, DISCOVERED_CHECK and DOUBLE_CHECK are derived from
/// ATTACK rows at read time (GameFeatureDao); ZUGZWANG and OVERLOADED_PIECE
/// have no detector yet.
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
