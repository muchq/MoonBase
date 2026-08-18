#ifndef DOMAINS_GAMES_LIBS_CHESS_COM_CPP_PRODUCTION_CLIENT_H_
#define DOMAINS_GAMES_LIBS_CHESS_COM_CPP_PRODUCTION_CLIENT_H_

#include "domains/games/libs/chess_com_cpp/client.h"

namespace chess_com {

/// Creates a client using the Beast HTTPS transport.
smithy::Outcome<Client> CreateProductionClient();

}  // namespace chess_com

#endif  // DOMAINS_GAMES_LIBS_CHESS_COM_CPP_PRODUCTION_CLIENT_H_
