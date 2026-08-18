#include "domains/games/apis/one_d4_worker/openings.h"

#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

namespace one_d4_worker {
namespace {

constexpr std::size_t kMaxLength = 255;

/// The structural words a family name ends on.
constexpr std::string_view kTerminators[] = {"opening", "defense", "defence", "game",
                                             "attack",  "gambit",  "system",  "variation"};

bool IsEcoCode(std::string_view slug) {
  // [A-E]\d{2}[a-z]?
  if (slug.size() < 3 || slug.size() > 4) return false;
  if (slug[0] < 'A' || slug[0] > 'E') return false;
  if (!absl::ascii_isdigit(slug[1]) || !absl::ascii_isdigit(slug[2])) return false;
  return slug.size() == 3 || absl::ascii_islower(slug[3]);
}

std::string Truncate(std::string value) {
  if (value.size() > kMaxLength) value.resize(kMaxLength);
  return value;
}

}  // namespace

std::string OpeningNameFromEcoUrl(std::string_view eco_url) {
  std::string_view trimmed = absl::StripAsciiWhitespace(eco_url);
  while (!trimmed.empty() && trimmed.back() == '/') trimmed.remove_suffix(1);

  const auto slash = trimmed.rfind('/');
  std::string_view slug = slash == std::string_view::npos ? trimmed : trimmed.substr(slash + 1);
  // "openings" is the bare path directory — no slug present.
  if (slug.empty() || absl::EqualsIgnoreCase(slug, "openings") || IsEcoCode(slug)) return "";

  std::string name(slug);
  for (char& c : name) {
    if (c == '-') c = ' ';
  }
  return Truncate(std::string(absl::StripAsciiWhitespace(name)));
}

std::string OpeningFamilyFromName(std::string_view opening_name) {
  const auto continuation = opening_name.find("...");
  std::string_view base = absl::StripAsciiWhitespace(
      continuation == std::string_view::npos ? opening_name : opening_name.substr(0, continuation));
  if (base.empty()) return "";

  const std::vector<std::string_view> words =
      absl::StrSplit(base, absl::ByAnyChar(" \t\n\r\f\v"), absl::SkipEmpty());
  if (words.empty()) return "";

  for (std::size_t i = 0; i < words.size(); ++i) {
    const std::string lowered = absl::AsciiStrToLower(words[i]);
    for (std::string_view terminator : kTerminators) {
      if (lowered == terminator) {
        return Truncate(absl::StrJoin(words.begin(), words.begin() + i + 1, " "));
      }
    }
  }
  const std::size_t take = words.size() < 2 ? words.size() : 2;
  return Truncate(absl::StrJoin(words.begin(), words.begin() + take, " "));
}

}  // namespace one_d4_worker
