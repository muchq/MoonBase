#include "cpp/portrait/base64.h"

#include <gtest/gtest.h>

namespace portrait {
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

TEST(Base64Test, GetEncodedSize) {
  EXPECT_EQ(Base64::getEncodedSize(0), 0);
  EXPECT_EQ(Base64::getEncodedSize(1), 4);
  EXPECT_EQ(Base64::getEncodedSize(2), 4);
  EXPECT_EQ(Base64::getEncodedSize(3), 4);
  EXPECT_EQ(Base64::getEncodedSize(4), 8);
  EXPECT_EQ(Base64::getEncodedSize(5), 8);
  EXPECT_EQ(Base64::getEncodedSize(6), 8);
}

TEST(Base64Test, GetDecodedSize) {
  EXPECT_EQ(Base64::getDecodedSize(""), 0);
  EXPECT_EQ(Base64::getDecodedSize("QQ=="), 1);
  EXPECT_EQ(Base64::getDecodedSize("VGVzdA=="), 4);
  EXPECT_EQ(Base64::getDecodedSize("SGVsbG8="), 5);
}

TEST(Base64Test, PngToBase64HelperFunction) {
  std::vector<uint8_t> png_data = {0x89, 0x50, 0x4E, 0x47};  // PNG signature start
  std::string encoded = pngToBase64(png_data);
  EXPECT_EQ(encoded, "iVBORw==");
}

TEST(Base64Test, Base64ToPngHelperFunction) {
  std::string base64_data = "iVBORw==";
  std::vector<uint8_t> decoded = base64ToPng(base64_data);
  std::vector<uint8_t> expected = {0x89, 0x50, 0x4E, 0x47};
  EXPECT_EQ(decoded, expected);
}

TEST(Base64Test, PngRoundTripHelperFunctions) {
  std::vector<uint8_t> original_png = {
      0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A  // Full PNG signature
  };
  std::string encoded = pngToBase64(original_png);
  std::vector<uint8_t> decoded = base64ToPng(encoded);
  EXPECT_EQ(original_png, decoded);
}

TEST(Base64Test, LargeBinaryData) {
  std::vector<uint8_t> large_data(1000);
  for (size_t i = 0; i < large_data.size(); ++i) {
    large_data[i] = static_cast<uint8_t>(i % 256);
  }

  std::string encoded = Base64::encode(large_data);
  std::vector<uint8_t> decoded = Base64::decode(encoded);
  EXPECT_EQ(large_data, decoded);

  size_t expected_encoded_size = Base64::getEncodedSize(large_data.size());
  EXPECT_EQ(encoded.size(), expected_encoded_size);
}

}  // namespace
}  // namespace portrait