#include "domains/games/libs/one_d4_motifs/occurrence.h"

#include <string_view>

namespace one_d4 {

std::string_view ToString(PinType pin_type) {
  switch (pin_type) {
    case PinType::kAbsolute:
      return "ABSOLUTE";
    case PinType::kRelative:
      return "RELATIVE";
  }
  return "UNKNOWN";
}

}  // namespace one_d4
