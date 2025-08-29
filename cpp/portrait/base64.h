#ifndef CPP_PORTRAIT_BASE64_H_
#define CPP_PORTRAIT_BASE64_H_

#include <string>
#include <vector>

namespace portrait {

class Base64 {
 public:
  static std::string encode(const std::vector<uint8_t>& data);

  static std::string encode(const uint8_t* data, size_t length);

  static std::vector<uint8_t> decode(const std::string& encoded);
};

std::string pngToBase64(const std::vector<uint8_t>& png_data);

std::vector<uint8_t> base64ToPng(const std::string& base64_data);

}  // namespace portrait

#endif  // CPP_PORTRAIT_BASE64_H_