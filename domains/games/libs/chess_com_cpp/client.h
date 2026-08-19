#ifndef DOMAINS_GAMES_LIBS_CHESS_COM_CPP_CLIENT_H_
#define DOMAINS_GAMES_LIBS_CHESS_COM_CPP_CLIENT_H_

#include <string_view>
#include <utility>

#include "moonbase/chess_com/client.h"
#include "smithy/client/config.h"
#include "smithy/core/outcome.h"

namespace chess_com {

/// Production configuration for chess.com's public API.
smithy::ClientConfig DefaultClientConfig();

/// Indexer-facing client over the generated chess.com API.
class Client {
 public:
  static smithy::Outcome<Client> Create(smithy::ClientConfig config);

  Client(Client&&) = default;
  Client& operator=(Client&&) = default;

  smithy::Outcome<moonbase::chess_com::FetchPlayerOutput> FetchPlayer(
      std::string_view username) const;
  smithy::Outcome<moonbase::chess_com::FetchArchiveOutput> FetchArchive(std::string_view username,
                                                                        int year,
                                                                        unsigned month) const;

  /// Every player holding `title` ("GM", "WIM", ...). Uppercased, since
  /// that is how the path spells them.
  smithy::Outcome<moonbase::chess_com::FetchTitledOutput> FetchTitled(std::string_view title) const;

 private:
  explicit Client(moonbase::chess_com::ChessComClient client) : client_(std::move(client)) {}

  moonbase::chess_com::ChessComClient client_;
};

}  // namespace chess_com

#endif  // DOMAINS_GAMES_LIBS_CHESS_COM_CPP_CLIENT_H_
