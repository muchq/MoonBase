#include "base64.h"

#include <gtest/gtest.h>

namespace futility::base64 {
namespace {

TEST(Base64Test, EncodeEmptyVector) {
  std::vector<uint8_t> empty_data;
  std::string encoded = Base64::encode(empty_data);
  EXPECT_EQ(encoded, "");
}

TEST(Base64Test, EncodeSimpleData) {
  std::vector<uint8_t> data = {'H', 'e', 'l', 'l', 'o'};
  std::string encoded = Base64::encode(data);
  EXPECT_EQ(encoded, "SGVsbG8=");
}

TEST(Base64Test, EncodePointerData) {
  const uint8_t data[] = {'T', 'e', 's', 't'};
  std::string encoded = Base64::encode(data, sizeof(data));
  EXPECT_EQ(encoded, "VGVzdA==");
}

TEST(Base64Test, DecodeSingleCharacter) {
  std::vector<uint8_t> decoded = Base64::decode("QQ==");
  std::vector<uint8_t> expected = {'A'};
  EXPECT_EQ(decoded, expected);
}

TEST(Base64Test, DecodeMultipleCharacters) {
  std::vector<uint8_t> decoded = Base64::decode("SGVsbG8=");
  std::vector<uint8_t> expected = {'H', 'e', 'l', 'l', 'o'};
  EXPECT_EQ(decoded, expected);
}

TEST(Base64Test, DecodeInvalidData) {
  std::vector<uint8_t> decoded = Base64::decode("Invalid@#$");
  EXPECT_TRUE(decoded.empty());
}

TEST(Base64Test, RoundTripEncoding) {
  std::vector<uint8_t> original_data = {0x00, 0x01, 0x02, 0xFF, 0xFE, 0xFD};
  std::string encoded = Base64::encode(original_data);
  std::vector<uint8_t> decoded = Base64::decode(encoded);
  EXPECT_EQ(original_data, decoded);
}

TEST(Base64Test, LargeBinaryData) {
  std::vector<uint8_t> large_data(1000);
  for (size_t i = 0; i < large_data.size(); ++i) {
    large_data[i] = static_cast<uint8_t>(i % 256);
  }

  std::string encoded = Base64::encode(large_data);
  std::vector<uint8_t> decoded = Base64::decode(encoded);
  EXPECT_EQ(large_data, decoded);
}

}  // namespace
}  // namespace futility::base64