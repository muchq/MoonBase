#include "domains/games/apis/one_d4_v2/smithy_handler.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status.h"

namespace one_d4_v2 {
namespace {

using ::testing::HasSubstr;

TEST(ToSmithyErrorTest, InvalidArgumentIsTheModeled400WithItsMessage) {
  const smithy::Error error =
      ToSmithyError(absl::InvalidArgumentError("pgn has 4200 plies (max 4096)"));
  EXPECT_EQ(error.code(), "InvalidPgnError");
  const auto* detail = error.detail<moonbase::one_d4::InvalidPgnError>();
  ASSERT_NE(detail, nullptr);
  EXPECT_THAT(detail->message, HasSubstr("4200 plies"));
}

// Unreachable today — Analyze only refuses input — and mapped anyway, so
// the day a dependency appears its failure is a 500 with a fixed body
// rather than a 400 blaming the caller for the server's problem.
TEST(ToSmithyErrorTest, AnythingElseIsUnmodeledAndItsMessageStaysInside) {
  const smithy::Error error = ToSmithyError(absl::InternalError("libpq said something private"));
  EXPECT_NE(error.code(), "InvalidPgnError");
  EXPECT_EQ(error.detail<moonbase::one_d4::InvalidPgnError>(), nullptr);
}

}  // namespace
}  // namespace one_d4_v2
