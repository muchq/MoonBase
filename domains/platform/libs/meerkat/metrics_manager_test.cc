#include "domains/platform/libs/meerkat/metrics_manager.h"

#include <chrono>
#include <memory>

#include "gtest/gtest.h"

namespace meerkat {

class HttpMetricsManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    manager_ = std::make_unique<HttpMetricsManager>("test_service");
  }

  std::unique_ptr<HttpMetricsManager> manager_;
};

TEST_F(HttpMetricsManagerTest, RecordRequestStart) {
  // This test verifies that RecordRequestStart doesn't crash
  // and handles basic input validation
  EXPECT_NO_THROW(manager_->RecordRequestStart("/test", "GET"));
  EXPECT_NO_THROW(manager_->RecordRequestStart("/api/v1/test", "POST"));
  EXPECT_NO_THROW(manager_->RecordRequestStart("", ""));
}

TEST_F(HttpMetricsManagerTest, RecordRequestComplete) {
  // Test various status codes and durations
  auto duration = std::chrono::microseconds(1000);

  EXPECT_NO_THROW(manager_->RecordRequestComplete("/test", "GET", 200, duration));
  EXPECT_NO_THROW(manager_->RecordRequestComplete("/test", "POST", 201, duration));
  EXPECT_NO_THROW(manager_->RecordRequestComplete("/test", "GET", 404, duration));
  EXPECT_NO_THROW(manager_->RecordRequestComplete("/test", "GET", 500, duration));
  EXPECT_NO_THROW(manager_->RecordRequestComplete("/test", "GET", 429, duration));
}

TEST_F(HttpMetricsManagerTest, HandlesZeroDuration) {
  auto zero_duration = std::chrono::microseconds(0);
  EXPECT_NO_THROW(manager_->RecordRequestComplete("/test", "GET", 200, zero_duration));
}

TEST_F(HttpMetricsManagerTest, HandlesLargeDuration) {
  auto large_duration = std::chrono::microseconds(std::chrono::seconds(60).count());
  EXPECT_NO_THROW(manager_->RecordRequestComplete("/test", "GET", 200, large_duration));
}

TEST_F(HttpMetricsManagerTest, HandlesEmptyRouteAndMethod) {
  auto duration = std::chrono::microseconds(1000);
  EXPECT_NO_THROW(manager_->RecordRequestComplete("", "", 200, duration));
}

TEST_F(HttpMetricsManagerTest, HandlesVariousStatusCodes) {
  auto duration = std::chrono::microseconds(1000);

  // Test success codes
  EXPECT_NO_THROW(manager_->RecordRequestComplete("/test", "GET", 200, duration));
  EXPECT_NO_THROW(manager_->RecordRequestComplete("/test", "GET", 201, duration));
  EXPECT_NO_THROW(manager_->RecordRequestComplete("/test", "GET", 204, duration));

  // Test redirection codes
  EXPECT_NO_THROW(manager_->RecordRequestComplete("/test", "GET", 301, duration));
  EXPECT_NO_THROW(manager_->RecordRequestComplete("/test", "GET", 302, duration));

  // Test client error codes
  EXPECT_NO_THROW(manager_->RecordRequestComplete("/test", "GET", 400, duration));
  EXPECT_NO_THROW(manager_->RecordRequestComplete("/test", "GET", 401, duration));
  EXPECT_NO_THROW(manager_->RecordRequestComplete("/test", "GET", 403, duration));
  EXPECT_NO_THROW(manager_->RecordRequestComplete("/test", "GET", 404, duration));
  EXPECT_NO_THROW(manager_->RecordRequestComplete("/test", "GET", 429, duration));

  // Test server error codes
  EXPECT_NO_THROW(manager_->RecordRequestComplete("/test", "GET", 500, duration));
  EXPECT_NO_THROW(manager_->RecordRequestComplete("/test", "GET", 502, duration));
  EXPECT_NO_THROW(manager_->RecordRequestComplete("/test", "GET", 503, duration));
}

TEST_F(HttpMetricsManagerTest, HandlesComplexRoutes) {
  auto duration = std::chrono::microseconds(1000);

  EXPECT_NO_THROW(manager_->RecordRequestStart("/api/v1/users/123", "GET"));
  EXPECT_NO_THROW(manager_->RecordRequestComplete("/api/v1/users/123", "GET", 200, duration));

  EXPECT_NO_THROW(manager_->RecordRequestStart("/health", "GET"));
  EXPECT_NO_THROW(manager_->RecordRequestComplete("/health", "GET", 200, duration));

  EXPECT_NO_THROW(manager_->RecordRequestStart("/v1/trace", "POST"));
  EXPECT_NO_THROW(manager_->RecordRequestComplete("/v1/trace", "POST", 201, duration));
}

TEST_F(HttpMetricsManagerTest, HandlesVariousHttpMethods) {
  auto duration = std::chrono::microseconds(1000);

  EXPECT_NO_THROW(manager_->RecordRequestStart("/test", "GET"));
  EXPECT_NO_THROW(manager_->RecordRequestComplete("/test", "GET", 200, duration));

  EXPECT_NO_THROW(manager_->RecordRequestStart("/test", "POST"));
  EXPECT_NO_THROW(manager_->RecordRequestComplete("/test", "POST", 201, duration));

  EXPECT_NO_THROW(manager_->RecordRequestStart("/test", "PUT"));
  EXPECT_NO_THROW(manager_->RecordRequestComplete("/test", "PUT", 200, duration));

  EXPECT_NO_THROW(manager_->RecordRequestStart("/test", "DELETE"));
  EXPECT_NO_THROW(manager_->RecordRequestComplete("/test", "DELETE", 204, duration));

  EXPECT_NO_THROW(manager_->RecordRequestStart("/test", "PATCH"));
  EXPECT_NO_THROW(manager_->RecordRequestComplete("/test", "PATCH", 200, duration));
}

}  // namespace meerkat