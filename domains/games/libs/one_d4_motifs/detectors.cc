#include "domains/games/libs/one_d4_motifs/detectors.h"

#include <memory>
#include <utility>
#include <vector>

namespace one_d4 {

std::vector<std::unique_ptr<Detector>> DefaultDetectors() {
  std::vector<std::unique_ptr<Detector>> detectors;
  detectors.push_back(MakePinDetector());
  detectors.push_back(MakeCrossPinDetector());
  detectors.push_back(MakeSkewerDetector());
  detectors.push_back(MakeAttackDetector());
  detectors.push_back(MakeCheckDetector());
  detectors.push_back(MakePromotionDetector());
  detectors.push_back(MakePromotionWithCheckDetector());
  detectors.push_back(MakePromotionWithCheckmateDetector());
  detectors.push_back(MakeBackRankMateDetector());
  detectors.push_back(MakeSmotheredMateDetector());
  return detectors;
}

}  // namespace one_d4
