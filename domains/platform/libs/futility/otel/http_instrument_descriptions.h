#pragma once

/// @file http_instrument_descriptions.h
/// @brief The descriptions the shared http_server_* instruments are exported with.
///
/// These are a cross-language contract, not documentation. Every service's
/// metrics land in one collector, which merges series by instrument name across
/// services written in all three languages. Its Prometheus exporter keeps the
/// first description it sees for a name and logs
///
///     Instrument description conflict, using existing
///
/// for every later one that disagrees — once per export interval, for as long
/// as both services run. An empty description conflicts with a non-empty one
/// just as loudly as two different sentences do, so a rail that declines to
/// describe an instrument is not staying out of the argument.
///
/// //domains/platform/libs/otel_contract pins these equal to yodel's and
/// server_pal's declarations.

#include <string_view>

namespace futility::otel {

struct HttpInstrumentDescription {
  std::string_view instrument_name;
  const char* description;
};

/// Keyed by the instrument name as created, which is the name that reaches the
/// collector — so the gauge and the histogram appear here with the _gauge and
/// _microseconds suffixes MetricsRecorder appends, not the stems its callers
/// pass in.
inline constexpr HttpInstrumentDescription kHttpInstrumentDescriptions[] = {
    {"http_server_requests", "HTTP requests received"},
    {"http_server_requests_success", "HTTP requests completed successfully (2xx-3xx)"},
    {"http_server_requests_failure", "HTTP requests that returned 4xx or 5xx"},
    {"http_server_requests_active_gauge", "HTTP requests currently in flight"},
    {"http_server_request_duration_microseconds", "HTTP request duration in microseconds"},
};

/// The canonical description for `instrument_name`, or "" when the name is not
/// one of the shared rails.
///
/// Empty is the right answer for everything else: a service-defined instrument
/// like scene_sphere_count is reported by one service, so there is no second
/// declaration for it to disagree with, and the SDK omits an empty description
/// rather than exporting a blank one.
constexpr const char* DescriptionFor(std::string_view instrument_name) {
  for (const auto& entry : kHttpInstrumentDescriptions) {
    if (entry.instrument_name == instrument_name) {
      return entry.description;
    }
  }
  return "";
}

}  // namespace futility::otel
