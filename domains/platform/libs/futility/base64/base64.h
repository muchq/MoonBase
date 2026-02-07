#ifndef DOMAINS_API_PLATFORM_LIBS_FUTILITY_BASE64_BASE64_H
#define DOMAINS_API_PLATFORM_LIBS_FUTILITY_BASE64_BASE64_H

/// @file base64.h
/// @brief Base64 encoding/decoding utilities for binary data.
///
/// Provides a simple interface for Base64 encoding and decoding of binary data.
/// Wraps Abseil's Base64 implementation for consistent behavior.
///
/// Example usage:
/// @code
///   // Encoding binary data
///   std::vector<uint8_t> binary_data = {0x48, 0x65, 0x6c, 0x6c, 0x6f};
///   std::string encoded = Base64::encode(binary_data);
///   // encoded == "SGVsbG8="
///
///   // Decoding back to binary
///   std::vector<uint8_t> decoded = Base64::decode(encoded);
///   // decoded == {0x48, 0x65, 0x6c, 0x6c, 0x6f}
/// @endcode

#include <string>
#include <vector>

namespace futility::base64 {

/// @brief Base64 encoding and decoding utilities.
///
/// All methods are static; no instance is needed.
class Base64 {
 public:
  /// @brief Encodes binary data to a Base64 string.
  /// @param data The binary data to encode.
  /// @return The Base64-encoded string.
  static std::string encode(const std::vector<uint8_t>& data);

  /// @brief Encodes binary data to a Base64 string.
  /// @param data Pointer to the binary data.
  /// @param length Number of bytes to encode.
  /// @return The Base64-encoded string.
  static std::string encode(const uint8_t* data, size_t length);

  /// @brief Decodes a Base64 string to binary data.
  /// @param encoded The Base64-encoded string.
  /// @return The decoded binary data.
  /// @note Behavior is undefined for invalid Base64 input.
  static std::vector<uint8_t> decode(const std::string& encoded);
};

}  // namespace futility::base64

#endif  // DOMAINS_API_PLATFORM_LIBS_FUTILITY_BASE64_BASE64_H