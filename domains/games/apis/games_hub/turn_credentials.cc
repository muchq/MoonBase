#include "domains/games/apis/games_hub/turn_credentials.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <cstdint>
#include <string>

#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

namespace games_hub {

std::optional<TurnConfig> TurnConfigFrom(std::string_view urls, std::string_view secret) {
  TurnConfig config;
  for (const std::string_view url : absl::StrSplit(urls, ',')) {
    const std::string_view trimmed = absl::StripAsciiWhitespace(url);
    if (!trimmed.empty()) config.urls.emplace_back(trimmed);
  }
  const std::string_view trimmed_secret = absl::StripAsciiWhitespace(secret);
  if (config.urls.empty() || trimmed_secret.empty()) return std::nullopt;
  config.secret = std::string(trimmed_secret);
  return config;
}

moonbase::games::IceServer TurnServerFor(const TurnConfig& config, const std::string& player_id,
                                         absl::Time now) {
  constexpr int64_t kHour = 3600;
  const int64_t expiry = (absl::ToUnixSeconds(now + config.ttl) / kHour + 1) * kHour;
  const std::string username = absl::StrCat(expiry, ":", player_id);
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int length = 0;
  HMAC(EVP_sha1(), config.secret.data(), config.secret.size(),
       reinterpret_cast<const unsigned char*>(username.data()), username.size(), digest, &length);

  moonbase::games::IceServer server;
  server.urls = config.urls;
  server.username = username;
  server.credential =
      absl::Base64Escape(std::string_view(reinterpret_cast<const char*>(digest), length));
  return server;
}

}  // namespace games_hub
