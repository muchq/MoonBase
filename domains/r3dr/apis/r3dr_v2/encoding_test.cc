#include "domains/r3dr/apis/r3dr_v2/encoding.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace r3dr_v2 {
namespace {

// Byte order, widths, and alphabet pinned at the transitions. Five vectors
// are encoding_test.go's own; the rest are recomputed.
TEST(EncodingTest, MatchesTheGoVectorsAtEveryWidthTransition) {
  const std::vector<std::pair<int64_t, std::string>> cases = {
      {0, "AAA"},
      {1, "AQA"},
      {12, "DAA"},
      {127, "fwA"},
      {32767, "_38"},     // last 2-byte id
      {32768, "AIAAAA"},  // first 4-byte id
      {65535, "__8AAA"},
      {2147483647, "____fw"},       // last 4-byte id
      {2147483648, "AAAAgAAAAAA"},  // first 8-byte id
      {std::numeric_limits<int64_t>::max(), "_________38"},
  };
  for (const auto& [id, want] : cases) {
    const auto slug = EncodeId(id);
    ASSERT_TRUE(slug.ok()) << "EncodeId(" << id << "): " << slug.status();
    EXPECT_EQ(*slug, want) << "EncodeId(" << id << ")";
  }
}

// Slug length is a function of the id's width and nothing else — 3, 6, or
// 11 characters, never anything between. A fixed 8-byte encoder would
// round-trip every id and still be wrong: the product is short links.
TEST(EncodingTest, SlugsAreThreeSixOrElevenCharacters) {
  for (const int64_t id : {int64_t{0}, int64_t{1}, int64_t{32767}}) {
    EXPECT_EQ(EncodeId(id)->size(), 3u) << id;
  }
  for (const int64_t id : {int64_t{32768}, int64_t{2147483647}}) {
    EXPECT_EQ(EncodeId(id)->size(), 6u) << id;
  }
  for (const int64_t id : {int64_t{2147483648}, std::numeric_limits<int64_t>::max()}) {
    EXPECT_EQ(EncodeId(id)->size(), 11u) << id;
  }
}

// A corrupted sequence refuses loudly instead of minting an empty slug.
TEST(EncodingTest, ANegativeIdIsRefused) {
  EXPECT_EQ(EncodeId(-1).status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(EncodeId(std::numeric_limits<int64_t>::min()).status().code(),
            absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace r3dr_v2
