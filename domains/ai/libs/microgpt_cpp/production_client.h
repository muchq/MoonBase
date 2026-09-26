#ifndef DOMAINS_AI_LIBS_MICROGPT_CPP_PRODUCTION_CLIENT_H_
#define DOMAINS_AI_LIBS_MICROGPT_CPP_PRODUCTION_CLIENT_H_

#include <string>

#include "domains/ai/libs/microgpt_cpp/client.h"

namespace microgpt {

/// Creates a client on `endpoint` using the Beast transport.
opal::Outcome<Client> CreateProductionClient(std::string endpoint);

}  // namespace microgpt

#endif  // DOMAINS_AI_LIBS_MICROGPT_CPP_PRODUCTION_CLIENT_H_
