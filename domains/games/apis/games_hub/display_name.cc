#include "domains/games/apis/games_hub/display_name.h"

#include <cstdint>
#include <optional>

#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "domains/games/apis/games_hub/chat_store.h"

namespace games_hub {
namespace {

// The code point starting at `at`, advancing past it; nullopt for bytes
// that are not well-formed UTF-8 (overlong, surrogate or out of range).
std::optional<uint32_t> NextCodePoint(std::string_view text, std::size_t& at) {
  const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(text[i]); };
  const unsigned char lead = byte(at);
  int length = 0;
  uint32_t cp = 0;
  if (lead < 0x80) {
    length = 1;
    cp = lead;
  } else if ((lead & 0xE0) == 0xC0) {
    length = 2;
    cp = lead & 0x1F;
  } else if ((lead & 0xF0) == 0xE0) {
    length = 3;
    cp = lead & 0x0F;
  } else if ((lead & 0xF8) == 0xF0) {
    length = 4;
    cp = lead & 0x07;
  } else {
    return std::nullopt;
  }
  if (at + length > text.size()) return std::nullopt;
  for (int i = 1; i < length; ++i) {
    if ((byte(at + i) & 0xC0) != 0x80) return std::nullopt;
    cp = (cp << 6) | (byte(at + i) & 0x3F);
  }
  static constexpr uint32_t kSmallest[] = {0, 0, 0x80, 0x800, 0x10000};
  if (cp < kSmallest[length] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
    return std::nullopt;
  }
  at += length;
  return cp;
}

// C0 and C1 controls and DEL.
bool IsControl(uint32_t cp) { return cp < 0x20 || (cp >= 0x7F && cp <= 0x9F); }

}  // namespace

absl::StatusOr<std::string> NormalizeDisplayName(std::string_view name) {
  const std::string_view trimmed = absl::StripAsciiWhitespace(name);
  int chars = 0;
  for (std::size_t at = 0; at < trimmed.size();) {
    const std::optional<uint32_t> cp = NextCodePoint(trimmed, at);
    if (!cp.has_value() || IsControl(*cp)) {
      return absl::InvalidArgumentError("a name is printable text");
    }
    ++chars;
  }
  if (chars > kMaxDisplayNameChars) {
    return absl::InvalidArgumentError(
        absl::StrCat("a name is at most ", kMaxDisplayNameChars, " characters"));
  }
  for (const std::string_view bot : {kBotPlayerId, kWordchainPlayerId}) {
    if (absl::EqualsIgnoreCase(trimmed, bot)) {
      return absl::InvalidArgumentError("that name is a bot's");
    }
  }
  return std::string(trimmed);
}

}  // namespace games_hub
