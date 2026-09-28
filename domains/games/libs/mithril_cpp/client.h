#ifndef DOMAINS_GAMES_LIBS_MITHRIL_CPP_CLIENT_H_
#define DOMAINS_GAMES_LIBS_MITHRIL_CPP_CLIENT_H_

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "moonbase/mithril/client.h"
#include "opal/client/config.h"
#include "opal/core/outcome.h"

namespace mithril {

/// How a consumer of mithril is configured. `endpoint` is the service on
/// the app network (http://mithril:8083), not the public route. One
/// attempt and a 2 s deadline: an answer in chat that arrives late is no
/// better than none.
opal::ClientConfig DefaultClientConfig(std::string endpoint);

/// Asks mithril for word ladders.
class Client {
 public:
  static opal::Outcome<Client> Create(opal::ClientConfig config);

  Client(Client&&) = default;
  Client& operator=(Client&&) = default;

  /// The shortest ladder from `start` to `end`, both ends included, or
  /// nothing when none exists.
  opal::Outcome<std::optional<std::vector<std::string>>> Wordchain(std::string start,
                                                                   std::string end) const;

 private:
  explicit Client(moonbase::mithril::MithrilClient client) : client_(std::move(client)) {}

  moonbase::mithril::MithrilClient client_;
};

}  // namespace mithril

#endif  // DOMAINS_GAMES_LIBS_MITHRIL_CPP_CLIENT_H_
