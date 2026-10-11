#ifndef DOMAINS_GAMES_LIBS_ONE_D4_CPP_CLIENT_H_
#define DOMAINS_GAMES_LIBS_ONE_D4_CPP_CLIENT_H_

#include <string>
#include <utility>

#include "moonbase/oned4/client.h"
#include "opal/client/config.h"
#include "opal/core/outcome.h"

namespace one_d4 {

/// How games_hub reaches one_d4: `endpoint` is the service on the app
/// network (http://one_d4:8080). One attempt and a short deadline: an
/// index ask only writes a row, and one that misses waits for the
/// player's next ask or an index asked for on 1d4.
opal::ClientConfig DefaultClientConfig(std::string endpoint);

/// Asks one_d4 to index muchq.com players from games_hub's public feed.
class Client {
 public:
  static opal::Outcome<Client> Create(opal::ClientConfig config);

  Client(Client&&) = default;
  Client& operator=(Client&&) = default;

  /// Queues an index of `player_id`'s games on muchq.com for `month`
  /// ("2026-10"); the request's id.
  opal::Outcome<std::string> IndexMuchqMonth(const std::string& player_id,
                                             const std::string& month) const;

 private:
  explicit Client(moonbase::oned4::OneD4Client client) : client_(std::move(client)) {}

  moonbase::oned4::OneD4Client client_;
};

}  // namespace one_d4

#endif  // DOMAINS_GAMES_LIBS_ONE_D4_CPP_CLIENT_H_
