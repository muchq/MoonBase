#include "domains/games/apis/games_hub/client_tag.h"

#include <algorithm>

namespace games_hub {
namespace {

constexpr std::string_view kNone = "none";
constexpr std::string_view kOther = "other";
constexpr std::string_view kIosPrefix = "ios/";

}  // namespace

ClientTag ClientTagOf(const std::optional<std::string>& sent) {
  if (!sent.has_value()) return {std::string(kOther), std::string(kNone)};
  const std::string_view text = *sent;
  if (text == "web") return {"web", std::string(kNone)};
  if (text == "ios") return {"ios", std::string(kOther)};
  if (text.starts_with(kIosPrefix)) {
    const std::string_view version = text.substr(kIosPrefix.size());
    const bool declared =
        std::find(kIosVersions.begin(), kIosVersions.end(), version) != kIosVersions.end();
    // A version is never more than its declared spelling; a slash past it
    // is no version at all.
    if (version.find('/') != std::string_view::npos) return {std::string(kOther), std::string(kNone)};
    return {"ios", std::string(declared ? version : kOther)};
  }
  return {std::string(kOther), std::string(kNone)};
}

std::vector<CounterSeries> ClientTagSeries() {
  std::vector<CounterSeries> series;
  const auto add = [&series](std::string_view client, std::string_view version) {
    series.push_back(
        {"hub_session_mints", {{"client", std::string(client)}, {"version", std::string(version)}}});
  };
  add("web", kNone);
  for (std::string_view version : kIosVersions) add("ios", version);
  add("ios", kOther);
  add(kOther, kNone);
  return series;
}

}  // namespace games_hub
