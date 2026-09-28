#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_WORDCHAIN_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_WORDCHAIN_H

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "domains/games/apis/games_hub/room_bot.h"
#include "domains/games/libs/mithril_cpp/client.h"
#include "moonbase/games/types.h"

namespace games_hub {

/// A word ladder from `start` to `end`: `path` is every rung, both ends
/// included, or nothing when no ladder joins them.
using Wordchain = moonbase::games::Wordchain;

/// The ask in "/wordchain start end": the command in any case, then
/// exactly two words of 3 to 8 ASCII letters, lowercased. Nothing for any
/// other text, which stays ordinary chat.
std::optional<Wordchain> WordchainCommand(std::string_view text);

/// A ladder as mithril's reply says it: "cold → cord → … → warm", or
/// "no ladder from cold to warm". A ladder longer than chat holds says
/// only how many rungs it has, and reads back as nothing.
std::string WordchainText(const Wordchain& chain);

/// The ladder in a reply's text, so a stored reply replays as structure;
/// nothing for text WordchainText never writes.
std::optional<Wordchain> WordchainOfText(std::string_view text);

/// mithril answering "/wordchain" as kWordchainPlayerId.
std::shared_ptr<Responder> WordchainResponder(std::shared_ptr<mithril::Client> client);

}  // namespace games_hub

#endif
