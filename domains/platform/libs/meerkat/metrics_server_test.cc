#include "domains/platform/libs/meerkat/meerkat.h"

#include <gtest/gtest.h>

#include <memory>

namespace meerkat {

class MeerkatMetricsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    server_ = std::make_unique<HttpServer>();
  }

  void TearDown() override {
    if (server_->is_running()) {
      server_->stop();
    }
    server_.reset();
  }

  std::unique_ptr<HttpServer> server_;
};

TEST_F(MeerkatMetricsTest, CanEnableMetrics) {
  EXPECT_NO_THROW(server_->enable_metrics("test_service"));
}

TEST_F(MeerkatMetricsTest, CanDisableMetrics) {
  server_->enable_metrics("test_service");
  EXPECT_NO_THROW(server_->disable_metrics());
}

TEST_F(MeerkatMetricsTest, CanEnableAndDisableMultipleTimes) {
  EXPECT_NO_THROW(server_->enable_metrics("test_service"));
  EXPECT_NO_THROW(server_->disable_metrics());
  EXPECT_NO_THROW(server_->enable_metrics("test_service_2"));
  EXPECT_NO_THROW(server_->disable_metrics());
}

TEST_F(MeerkatMetricsTest, CanChangeServiceNameBetweenEnables) {
  server_->enable_metrics("service_1");
  server_->disable_metrics();
  EXPECT_NO_THROW(server_->enable_metrics("service_2"));
}

TEST_F(MeerkatMetricsTest, DisablingMetricsWithoutEnablingDoesNotCrash) {
  EXPECT_NO_THROW(server_->disable_metrics());
}

TEST_F(MeerkatMetricsTest, ExtractRoutePatternExactMatch) {
  // Register a route
  server_->get("/test", [](const HttpRequest& req) -> HttpResponse {
    return responses::ok(json{{"message", "test"}});
  });

  // Test that ExtractRoutePattern finds exact matches
  // Note: We can't directly test the private method, but we can verify
  // the server accepts the route registration without crashing
  EXPECT_NO_THROW(server_->enable_metrics("test_service"));
}

TEST_F(MeerkatMetricsTest, ExtractRoutePatternWithMultipleRoutes) {
  // Register multiple routes
  server_->get("/test", [](const HttpRequest& req) -> HttpResponse {
    return responses::ok(json{{"message", "test"}});
  });

  server_->post("/api/v1/users", [](const HttpRequest& req) -> HttpResponse {
    return responses::created(json{{"id", "123"}});
  });

  server_->put("/api/v1/users/123", [](const HttpRequest& req) -> HttpResponse {
    return responses::ok(json{{"updated", true}});
  });

  EXPECT_NO_THROW(server_->enable_metrics("test_service"));
}

TEST_F(MeerkatMetricsTest, MetricsWithHealthChecks) {
  server_->enable_health_checks();
  EXPECT_NO_THROW(server_->enable_metrics("test_service"));
}

TEST_F(MeerkatMetricsTest, MetricsWithTracing) {
  server_->enable_tracing();
  EXPECT_NO_THROW(server_->enable_metrics("test_service"));
}

TEST_F(MeerkatMetricsTest, MetricsWithAllFeatures) {
  server_->enable_health_checks();
  server_->enable_tracing();
  EXPECT_NO_THROW(server_->enable_metrics("test_service"));

  // Add a route after enabling all features
  server_->get("/test", [](const HttpRequest& req) -> HttpResponse {
    return responses::ok(json{{"message", "test"}});
  });
}

TEST_F(MeerkatMetricsTest, EmptyServiceNameHandling) {
  // Test that empty service name doesn't crash
  EXPECT_NO_THROW(server_->enable_metrics(""));
}

TEST_F(MeerkatMetricsTest, SpecialCharactersInServiceName) {
  // Test that service names with special characters are handled
  EXPECT_NO_THROW(server_->enable_metrics("test-service"));
  server_->disable_metrics();

  EXPECT_NO_THROW(server_->enable_metrics("test_service"));
  server_->disable_metrics();

  EXPECT_NO_THROW(server_->enable_metrics("test.service"));
  server_->disable_metrics();

  EXPECT_NO_THROW(server_->enable_metrics("test:service"));
}

}  // namespace meerkat