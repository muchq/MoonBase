#ifndef DOMAINS_PLATFORM_LIBS_FUTILITY_ENV_ENV_H
#define DOMAINS_PLATFORM_LIBS_FUTILITY_ENV_ENV_H

#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"

namespace futility::env {

/// The port from the PORT environment variable, or default_port when unset.
/// A non-numeric PORT yields 0.
inline int ReadPort(int default_port) {
  const char* port = std::getenv("PORT");
  return port != nullptr ? std::atoi(port) : default_port;
}

/// The named environment variable as a comma-separated list: entries split
/// on commas, surrounding whitespace trimmed, empty entries dropped. Unset
/// and empty both yield {}.
inline std::vector<std::string> ReadList(const char* name) {
  std::vector<std::string> values;
  const char* raw = std::getenv(name);
  if (raw == nullptr) return values;
  for (absl::string_view entry : absl::StrSplit(raw, ',')) {
    entry = absl::StripAsciiWhitespace(entry);
    if (!entry.empty()) values.emplace_back(entry);
  }
  return values;
}

/// The named variable as a whole number of seconds, or default_seconds
/// when it is unset, empty, unreadable, or not positive.
///
/// Strict on purpose. std::atoi answers 0 for anything it cannot parse,
/// so a typo in an interval becomes a tight loop and a typo in a timeout
/// becomes no timeout — both silently, and both at the moment nobody is
/// looking. A value that cannot be read is not a value.
///
/// Non-positive is refused for the same reason rather than honoured: no
/// caller here wants a zero interval, and the ones that would notice are
/// the ones that hurt.
inline std::optional<int> ReadPositiveSeconds(const char* name) {
  const char* raw = std::getenv(name);
  if (raw == nullptr || *raw == '\0') return std::nullopt;

  int seconds = 0;
  if (!absl::SimpleAtoi(raw, &seconds) || seconds <= 0) return std::nullopt;
  return seconds;
}

}  // namespace futility::env

#endif
