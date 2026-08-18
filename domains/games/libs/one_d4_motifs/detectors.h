#ifndef DOMAINS_GAMES_LIBS_ONE_D4_MOTIFS_DETECTORS_H
#define DOMAINS_GAMES_LIBS_ONE_D4_MOTIFS_DETECTORS_H

#include <memory>
#include <vector>

#include "domains/games/libs/one_d4_motifs/detector.h"

namespace one_d4 {

// The detector set, behind factories. Nothing outside names a detector
// type: they are only ever used through Detector, and a per-detector header
// would carry a class declaration nobody writes down.

std::unique_ptr<Detector> MakePinDetector();
std::unique_ptr<Detector> MakeCrossPinDetector();
std::unique_ptr<Detector> MakeSkewerDetector();
std::unique_ptr<Detector> MakeAttackDetector();
std::unique_ptr<Detector> MakeCheckDetector();
std::unique_ptr<Detector> MakePromotionDetector();
std::unique_ptr<Detector> MakePromotionWithCheckDetector();
std::unique_ptr<Detector> MakePromotionWithCheckmateDetector();
std::unique_ptr<Detector> MakeBackRankMateDetector();
std::unique_ptr<Detector> MakeSmotheredMateDetector();

/// What indexing runs. The single definition — two processes assembling
/// different sets is two disagreeing indexes.
std::vector<std::unique_ptr<Detector>> DefaultDetectors();

}  // namespace one_d4

#endif  // DOMAINS_GAMES_LIBS_ONE_D4_MOTIFS_DETECTORS_H
