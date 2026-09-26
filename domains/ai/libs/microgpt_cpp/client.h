#ifndef DOMAINS_AI_LIBS_MICROGPT_CPP_CLIENT_H_
#define DOMAINS_AI_LIBS_MICROGPT_CPP_CLIENT_H_

#include <string>
#include <utility>
#include <vector>

#include "moonbase/microgpt/client.h"
#include "opal/client/config.h"
#include "opal/core/outcome.h"

namespace microgpt {

/// How a consumer of microgpt-serve's chat is configured. `endpoint` is
/// the service on the app network (http://microgpt-serve:8087), not the
/// public route.
///
/// One attempt and a 5 s deadline: generation is synchronous and
/// CPU-bound, a retry doubles the load on a service that is already slow,
/// and the room bot's contract is best effort — a missed reply is silence,
/// not an error anyone sees.
opal::ClientConfig DefaultClientConfig(std::string endpoint);

/// Asks microgpt-serve for the assistant's next turn.
class Client {
 public:
  static opal::Outcome<Client> Create(opal::ClientConfig config);

  Client(Client&&) = default;
  Client& operator=(Client&&) = default;

  /// The reply to `messages`, generating at most `max_tokens`.
  opal::Outcome<moonbase::microgpt::ChatOutput> Chat(
      std::vector<moonbase::microgpt::Message> messages, int max_tokens) const;

 private:
  explicit Client(moonbase::microgpt::MicrogptClient client) : client_(std::move(client)) {}

  moonbase::microgpt::MicrogptClient client_;
};

}  // namespace microgpt

#endif  // DOMAINS_AI_LIBS_MICROGPT_CPP_CLIENT_H_
