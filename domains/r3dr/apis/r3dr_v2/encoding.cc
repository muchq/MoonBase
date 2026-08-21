#include "domains/r3dr/apis/r3dr_v2/encoding.h"

#include <cstdint>
#include <limits>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/escaping.h"

namespace r3dr_v2 {
namespace {

std::string LittleEndianBytes(uint64_t value, int width) {
  std::string bytes(width, '\0');
  for (int i = 0; i < width; ++i) {
    bytes[i] = static_cast<char>((value >> (8 * i)) & 0xff);
  }
  return bytes;
}

}  // namespace

absl::StatusOr<std::string> EncodeId(int64_t id) {
  if (id < 0) {
    return absl::InvalidArgumentError("id is negative");
  }
  int width = 8;
  if (id <= std::numeric_limits<int16_t>::max()) {
    width = 2;
  } else if (id <= std::numeric_limits<int32_t>::max()) {
    width = 4;
  }
  std::string slug;
  // WebSafeBase64Escape is base64url with no padding — Go's RawURLEncoding.
  absl::WebSafeBase64Escape(LittleEndianBytes(static_cast<uint64_t>(id), width), &slug);
  return slug;
}

}  // namespace r3dr_v2
