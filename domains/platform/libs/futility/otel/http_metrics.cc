#include "domains/platform/libs/futility/otel/http_metrics.h"

#include <string>
#include <utility>

namespace futility::otel {

HttpMetricsManager::HttpMetricsManager(const std::string& service_name)
    : HttpMetricsManager(service_name, std::make_unique<MetricsRecorder>(service_name)) {}

HttpMetricsManager::HttpMetricsManager(const std::string& service_name,
                                       std::unique_ptr<MetricsRecorder> recorder)
    : service_name_(service_name), recorder_(std::move(recorder)) {}

void HttpMetricsManager::RecordRequestStart(const std::string& method) {
  if (!recorder_) return;

  // The gauge is the one instrument that moves here, and the one instrument
  // without a route: pre-dispatch nothing bounded is known about the path,
  // and the raw target is exactly what #1305 removed from the labels. The
  // request counter moves at completion instead, where it can carry the
  // route — so it counts completed-or-abandoned requests rather than started
  // ones, the same totals observed a request-duration later (yodel's
  // contract; the two always pair on this rail because smithy's Observe
  // reports a completion even when dispatch throws).
  recorder_->RecordGauge("http_server_requests_active", 1, CreateGaugeAttributes(method));
}

void HttpMetricsManager::RecordRequestComplete(const std::string& route, const std::string& method,
                                               int status_code,
                                               std::chrono::microseconds duration) {
  if (!recorder_) return;

  // Decrement with the same label set the increment used, or the per-label
  // gauge series would drift apart instead of draining.
  recorder_->RecordGauge("http_server_requests_active", -1, CreateGaugeAttributes(method));

  auto base_attrs = CreateBaseAttributes(route, method);
  recorder_->RecordCounter("http_server_requests", 1, base_attrs);

  if (IsSuccess(status_code)) {
    recorder_->RecordCounter("http_server_requests_success", 1, base_attrs);
  } else {
    auto failure_attrs = CreateRequestAttributes(route, method, status_code);
    failure_attrs["error_type"] = DetermineErrorType(status_code);
    recorder_->RecordCounter("http_server_requests_failure", 1, failure_attrs);
  }

  recorder_->RecordLatency("http_server_request_duration", duration,
                           CreateRequestAttributes(route, method, status_code));
}

std::map<std::string, std::string> HttpMetricsManager::CreateGaugeAttributes(
    const std::string& method) const {
  return {{"service_name", service_name_}, {"http_method", method}};
}

std::map<std::string, std::string> HttpMetricsManager::CreateBaseAttributes(
    const std::string& route, const std::string& method) const {
  // http_method, not futility's historical `method`: the label spelling is
  // shared with yodel and server_pal, and otel_contract pins the common set.
  return {{"service_name", service_name_}, {"route", route}, {"http_method", method}};
}

std::map<std::string, std::string> HttpMetricsManager::CreateRequestAttributes(
    const std::string& route, const std::string& method, int status_code) const {
  auto attrs = CreateBaseAttributes(route, method);
  attrs["status_code"] = std::to_string(status_code);
  attrs["result"] = DetermineResult(status_code);
  return attrs;
}

std::string HttpMetricsManager::DetermineErrorType(int status_code) const {
  if (status_code == 429) return "rate_limited";
  if (status_code >= 400 && status_code < 500) return "client_error";
  if (status_code >= 500) return "server_error";
  return "unknown";
}

std::string HttpMetricsManager::DetermineResult(int status_code) const {
  return IsSuccess(status_code) ? "success" : "failure";
}

bool HttpMetricsManager::IsSuccess(int status_code) const {
  return status_code >= 200 && status_code < 400;
}

}  // namespace futility::otel
