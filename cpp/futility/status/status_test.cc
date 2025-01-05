#include "status.h"

#include <gtest/gtest.h>

using namespace futility::status;

TEST(StatusHelper, ConvertGrpcToAbseil) {
  // Arrange
  grpc::Status grpc_status{grpc::StatusCode::CANCELLED, "oh no"};

  // Act
  auto absl_status = GrpcToAbseil(grpc_status);

  // Assert
  EXPECT_FALSE(absl_status.ok());
  EXPECT_EQ(absl_status.code(), absl::StatusCode::kCancelled);
  EXPECT_EQ(absl_status.message(), "oh no");
}

TEST(StatusHelper, ConvertAbseilToGrpc) {
  // Arrange
  absl::Status abseil_status{absl::StatusCode::kCancelled, "uh oh"};

  // Act
  auto grpc_status = AbseilToGrpc(abseil_status);

  // Assert
  EXPECT_FALSE(grpc_status.ok());
  EXPECT_EQ(grpc_status.error_code(), grpc::StatusCode::CANCELLED);
  EXPECT_EQ(grpc_status.error_message(), "uh oh");
}
