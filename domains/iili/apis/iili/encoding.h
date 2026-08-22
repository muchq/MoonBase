#ifndef DOMAINS_IILI_APIS_IILI_ENCODING_H
#define DOMAINS_IILI_APIS_IILI_ENCODING_H

#include <cstdint>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"

namespace iili {

/// Id -> slug: little-endian bytes (2/4/8, stepping at MaxInt16/MaxInt32),
/// base64url unpadded — so slugs are exactly 3, 6, or 11 chars and widths
/// can't collide. Bit-exact with the Go encoder; there is no decoder, slugs
/// are matched as strings. Refuses negative ids.
absl::StatusOr<std::string> EncodeId(int64_t id);

/// Whether a string is even the shape of a slug: exactly 3, 6, or 11 chars,
/// the only lengths the widths above produce. The cheap filter in front of
/// the store.
bool IsPossibleSlug(std::string_view slug);

}  // namespace iili

#endif  // DOMAINS_IILI_APIS_IILI_ENCODING_H
