#include "domains/games/apis/golf_hub/id_generator.h"

#include <cstddef>
#include <string_view>

#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/strings/str_format.h"
#include "domains/games/apis/golf_hub/ticket_vault.h"

namespace golf_hub {

namespace {

// The Go hub's word lists (players.WhimsicalIDGenerator), verbatim.
constexpr std::string_view kAdjectives[] = {"bouncy",  "giggly", "sparkly", "fuzzy",   "wiggly",
                                            "snuggly", "dreamy", "bubbly",  "twinkly", "jolly",
                                            "quirky",  "peppy",  "zesty",   "frisky",  "silly",
                                            "perky",   "cheeky", "zippy",   "groovy",  "jazzy"};
constexpr std::string_view kColors[] = {
    "lavender", "periwinkle", "coral",  "mint",       "peach",   "turquoise", "magenta",
    "cerulean", "lilac",      "salmon", "chartreuse", "crimson", "cobalt",    "amber",
    "jade",     "fuchsia",    "indigo", "teal",       "mauve",   "vermillion"};
constexpr std::string_view kAnimals[] = {
    "koala",  "kangaroo", "wombat",    "quokka",     "platypus", "echidna",  "wallaby",
    "bilby",  "numbat",   "possum",    "kookaburra", "cockatoo", "lorikeet", "galah",
    "budgie", "dingo",    "bandicoot", "pademelon",  "potoroo",  "glider"};

template <std::size_t N>
std::string_view Pick(absl::BitGen& gen, const std::string_view (&words)[N]) {
  return words[absl::Uniform<std::size_t>(gen, 0u, N)];
}

}  // namespace

std::string WhimsicalIdGenerator::PlayerId() {
  thread_local absl::BitGen gen;
  constexpr char kSlug[] = "abcdefghijklmnopqrstuvwxyz0123456789";
  std::string id;
  id.append(Pick(gen, kAdjectives));
  id.push_back('-');
  id.append(Pick(gen, kColors));
  id.push_back('-');
  id.append(Pick(gen, kAnimals));
  id.push_back('-');
  for (int i = 0; i < 4; ++i) {
    id.push_back(kSlug[absl::Uniform<std::size_t>(gen, 0u, sizeof(kSlug) - 1)]);
  }
  return id;
}

std::string WhimsicalIdGenerator::RoomId() { return RandomId("r"); }

std::string WhimsicalIdGenerator::GameCode() {
  thread_local absl::BitGen gen;
  constexpr char kCode[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  std::string code;
  for (int i = 0; i < 6; ++i) {
    code.push_back(kCode[absl::Uniform<std::size_t>(gen, 0u, sizeof(kCode) - 1)]);
  }
  return code;
}

std::string SequentialIdGenerator::PlayerId() { return absl::StrFormat("player-%d", ++players_); }

std::string SequentialIdGenerator::RoomId() { return absl::StrFormat("room-%d", ++rooms_); }

std::string SequentialIdGenerator::GameCode() { return absl::StrFormat("GAME%02d", ++games_); }

}  // namespace golf_hub
