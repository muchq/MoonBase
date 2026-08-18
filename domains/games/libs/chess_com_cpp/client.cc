#include "domains/games/libs/chess_com_cpp/client.h"

#include <string>
#include <utility>

#include "smithy/core/error.h"

namespace chess_com {
namespace {

std::string LowercaseUsername(std::string_view username) {
  std::string lower(username);
  for (char& value : lower) {
    if (value >= 'A' && value <= 'Z') {
      value += 'a' - 'A';
    }
  }
  return lower;
}

}  // namespace

smithy::ClientConfig DefaultClientConfig() {
  smithy::ClientConfig config;
  config.endpoint = "https://api.chess.com";
  config.user_agent = "MoonBase indexer/1.0";
  config.request_timeout_ms = 60'000;
  config.retry.max_attempts = 3;
  return config;
}

smithy::Outcome<Client> Client::Create(smithy::ClientConfig config) {
  auto client = moonbase::chess_com::ChessComClient::Create(std::move(config));
  if (!client.ok()) {
    return std::move(client).error();
  }
  return Client(std::move(*client));
}

smithy::Outcome<moonbase::chess_com::FetchPlayerOutput> Client::FetchPlayer(
    std::string_view username) const {
  return client_.FetchPlayer(
      moonbase::chess_com::FetchPlayerInput{.username = LowercaseUsername(username)});
}

smithy::Outcome<moonbase::chess_com::FetchArchiveOutput> Client::FetchArchive(
    std::string_view username, int year, unsigned month) const {
  if (month < 1 || month > 12) {
    return smithy::Error::Validation("month must be between 1 and 12");
  }

  moonbase::chess_com::FetchArchiveInput input;
  input.username = LowercaseUsername(username);
  input.year = std::to_string(year);
  input.month = month < 10 ? "0" + std::to_string(month) : std::to_string(month);
  return client_.FetchArchive(input);
}

}  // namespace chess_com
