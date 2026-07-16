#ifndef DOMAINS_PLATFORM_LIBS_FUTILITY_ENV_ENV_H
#define DOMAINS_PLATFORM_LIBS_FUTILITY_ENV_ENV_H

#include <cstdlib>

namespace futility::env {

/// The port from the PORT environment variable, or default_port when unset.
/// A non-numeric PORT yields 0, matching meerkat::read_port's behavior.
inline int ReadPort(int default_port) {
  const char* port = std::getenv("PORT");
  return port != nullptr ? std::atoi(port) : default_port;
}

}  // namespace futility::env

#endif
