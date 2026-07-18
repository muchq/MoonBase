#ifndef DOMAINS_PLATFORM_LIBS_FUTILITY_ENV_ENV_H
#define DOMAINS_PLATFORM_LIBS_FUTILITY_ENV_ENV_H

#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace futility::env {

/// The port from the PORT environment variable, or default_port when unset.
/// A non-numeric PORT yields 0, matching meerkat::read_port's behavior.
inline int ReadPort(int default_port) {
  const char* port = std::getenv("PORT");
  return port != nullptr ? std::atoi(port) : default_port;
}

/// The named environment variable as a comma-separated list: entries split
/// on commas, surrounding spaces trimmed, empty entries dropped. Unset and
/// empty both yield {}.
inline std::vector<std::string> ReadList(const char* name) {
  std::vector<std::string> values;
  const char* raw = std::getenv(name);
  if (raw == nullptr) return values;
  std::string_view remaining(raw);
  while (!remaining.empty()) {
    const auto comma = remaining.find(',');
    std::string_view entry = remaining.substr(0, comma);
    remaining = comma == std::string_view::npos ? std::string_view{} : remaining.substr(comma + 1);
    while (!entry.empty() && entry.front() == ' ') entry.remove_prefix(1);
    while (!entry.empty() && entry.back() == ' ') entry.remove_suffix(1);
    if (!entry.empty()) values.emplace_back(entry);
  }
  return values;
}

}  // namespace futility::env

#endif
