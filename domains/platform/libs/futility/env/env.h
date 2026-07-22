#ifndef DOMAINS_PLATFORM_LIBS_FUTILITY_ENV_ENV_H
#define DOMAINS_PLATFORM_LIBS_FUTILITY_ENV_ENV_H

#include <cstdlib>
#include <string>
#include <vector>

#include "absl/strings/ascii.h"
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

}  // namespace futility::env

#endif
