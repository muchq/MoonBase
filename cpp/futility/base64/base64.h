#ifndef CPP_FUTILITY_BASE64_BASE64_H
#define CPP_FUTILITY_BASE64_BASE64_H

#include <string>
#include <vector>

namespace futility::base64 {

class Base64 {
 public:
  static std::string encode(const std::vector<uint8_t>& data);
  static std::string encode(const uint8_t* data, size_t length);
  static std::vector<uint8_t> decode(const std::string& encoded);
};
}  // namespace futility::base64

#endif  // CPP_FUTILITY_BASE64_BASE64_H