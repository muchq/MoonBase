#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_OPENINGS_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_OPENINGS_H

#include <string>
#include <string_view>

namespace one_d4_worker {

/// The opening name carried by a chess.com ECOUrl slug, or "" when the URL
/// carries none — a bare ECO code ("B10") is a code, not a name.
std::string OpeningNameFromEcoUrl(std::string_view eco_url);

/// The family: opening words up to and including the first structural word
/// ("Defense", "Gambit", ...), else the first two. "" when there is none.
///
/// The move continuation is glued to the preceding word
/// ("Owens-Defense...3.Nc3-e6"), which is what hides that word from a scan
/// that splits on whitespace — drop it first.
std::string OpeningFamilyFromName(std::string_view opening_name);

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_OPENINGS_H
