#ifndef DOMAINS_AI_LIBS_DEJA_CPP_PRODUCTION_CLIENT_H_
#define DOMAINS_AI_LIBS_DEJA_CPP_PRODUCTION_CLIENT_H_

#include <string>

#include "domains/ai/libs/deja_cpp/client.h"

namespace deja {

/// Creates a client on `endpoint` using the Beast transport.
opal::Outcome<Client> CreateProductionClient(std::string endpoint);

}  // namespace deja

#endif  // DOMAINS_AI_LIBS_DEJA_CPP_PRODUCTION_CLIENT_H_
