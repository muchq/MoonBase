#ifndef DOMAINS_GAMES_APIS_GOLF_HUB_PROTOCOL_INPUT_H
#define DOMAINS_GAMES_APIS_GOLF_HUB_PROTOCOL_INPUT_H

#include <string_view>

namespace golf_hub {

/// PostgreSQL text and identifiers cannot contain NUL. libpq's current
/// text-parameter path is null-terminated, so rejecting at admission also
/// prevents a protocol value from aliasing the prefix the database sees.
inline bool HasEmbeddedNul(std::string_view value) {
  return value.find('\0') != std::string_view::npos;
}

}  // namespace golf_hub

#endif
