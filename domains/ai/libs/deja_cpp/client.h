#ifndef DOMAINS_AI_LIBS_DEJA_CPP_CLIENT_H_
#define DOMAINS_AI_LIBS_DEJA_CPP_CLIENT_H_

#include <cstdint>
#include <string>
#include <utility>

#include "moonbase/deja/client.h"
#include "opal/client/config.h"
#include "opal/core/outcome.h"

namespace deja {

/// How a consumer of deja's tape is configured. `endpoint` is deja itself
/// on the app network (http://deja:8093), not the public route: the tape
/// is a service-to-service read and has no business crossing Caddy.
///
/// One attempt and a short deadline, unlike the indexer clients: a poller
/// that retries is a poller holding a thread while its own next tick comes
/// round, and a tape event nobody caught is one the next poll fetches
/// anyway. Best effort is the contract, so failing fast IS the retry.
opal::ClientConfig DefaultClientConfig(std::string endpoint);

/// Reads deja's tape (#1150, #1554).
class Client {
 public:
  static opal::Outcome<Client> Create(opal::ClientConfig config);

  Client(Client&&) = default;
  Client& operator=(Client&&) = default;

  /// The events deja holds after `after`, oldest first. `after` of 0 asks
  /// for everything its ring still has.
  opal::Outcome<moonbase::deja::FetchRecentOutput> Recent(std::int64_t after) const;

 private:
  explicit Client(moonbase::deja::DejaClient client) : client_(std::move(client)) {}

  moonbase::deja::DejaClient client_;
};

}  // namespace deja

#endif  // DOMAINS_AI_LIBS_DEJA_CPP_CLIENT_H_
