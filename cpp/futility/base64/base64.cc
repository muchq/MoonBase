#include "base64.h"

#include "absl/strings/escaping.h"

namespace futility::base64 {

std::string Base64::encode(const std::vector<uint8_t>& data) {
  return absl::Base64Escape(std::string(data.begin(), data.end()));
}

std::string Base64::encode(const uint8_t* data, size_t length) {
  return absl::Base64Escape(std::string(reinterpret_cast<const char*>(data), length));
}

std::vector<uint8_t> Base64::decode(const std::string& encoded) {
  std::string decoded;
  if (!absl::Base64Unescape(encoded, &decoded)) {
    return std::vector<uint8_t>();
  }
  return std::vector<uint8_t>(decoded.begin(), decoded.end());
}
}  // namespace futility::base64