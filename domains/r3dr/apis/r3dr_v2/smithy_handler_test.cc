#include "domains/r3dr/apis/r3dr_v2/smithy_handler.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status.h"

namespace r3dr_v2 {
namespace {

using ::testing::HasSubstr;

TEST(ToSmithyErrorTest, InvalidArgumentBecomesTheModeled400) {
  const smithy::Error error = ToSmithyError(absl::InvalidArgumentError("expiresAt is in the past"));
  EXPECT_EQ(error.code(), "InvalidRequestError");
  ASSERT_NE(error.detail<moonbase::r3dr::InvalidRequestError>(), nullptr);
  EXPECT_EQ(error.detail<moonbase::r3dr::InvalidRequestError>()->message,
            "expiresAt is in the past");
}

// The arm a store outage takes: unmodeled, so the generated server answers a
// fixed 500 — never the modeled 404 v1 collapsed it into.
TEST(ToSmithyErrorTest, AnythingElseIsUnmodeled) {
  const smithy::Error error =
      ToSmithyError(absl::UnavailableError("postgres connect failed: host shared_postgres"));
  EXPECT_NE(error.code(), "InvalidRequestError");
  EXPECT_NE(error.code(), "NotFoundError");
  EXPECT_EQ(error.detail<moonbase::r3dr::InvalidRequestError>(), nullptr);
  EXPECT_THAT(error.message(), HasSubstr("postgres connect failed"));
}

}  // namespace
}  // namespace r3dr_v2
