#include "cpp/portrait/base64.h"

#include "absl/strings/escaping.h"

namespace portrait {

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

size_t Base64::getEncodedSize(size_t input_size) {
    return ((input_size + 2) / 3) * 4;
}

size_t Base64::getDecodedSize(const std::string& encoded) {
    if (encoded.empty()) {
        return 0;
    }
    
    size_t len = encoded.length();
    size_t padding = 0;
    
    if (encoded[len - 1] == '=') padding++;
    if (encoded[len - 2] == '=') padding++;
    
    return (len * 3) / 4 - padding;
}

std::string pngToBase64(const std::vector<uint8_t>& png_data) {
    return Base64::encode(png_data);
}

std::vector<uint8_t> base64ToPng(const std::string& base64_data) {
    return Base64::decode(base64_data);
}

}  // namespace portrait