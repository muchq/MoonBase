#include "domains/games/apis/games_hub/wordchain.h"

#include <utility>

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"
#include "domains/games/apis/games_hub/chat_store.h"

namespace games_hub {
namespace {

constexpr std::string_view kCommand = "/wordchain";
constexpr std::string_view kRung = " → ";
constexpr std::string_view kNoLadder = "no ladder from ";
constexpr std::string_view kTo = " to ";

// mithril's own bounds on a word, in ASCII.
bool IsWord(std::string_view word) {
  if (word.size() < 3 || word.size() > 9) return false;
  for (const char c : word) {
    if (!absl::ascii_isalpha(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

bool IsLowerWord(std::string_view word) {
  return IsWord(word) && absl::AsciiStrToLower(word) == word;
}

class Mithril final : public Responder {
 public:
  explicit Mithril(std::shared_ptr<mithril::Client> client) : client_(std::move(client)) {}

  const char* Author() const override { return kWordchainPlayerId; }

  bool Asks(std::string_view text) const override { return WordchainCommand(text).has_value(); }

  opal::Outcome<std::string> Reply(const ChatRow& trigger) const override {
    std::optional<Wordchain> chain = WordchainCommand(trigger.text);
    if (!chain.has_value()) return opal::Error::Validation("not a /wordchain command");
    auto path = client_->Wordchain(chain->start, chain->end);
    if (!path.ok()) return std::move(path).error();
    chain->path = *std::move(path);
    return WordchainText(*chain);
  }

 private:
  const std::shared_ptr<mithril::Client> client_;
};

}  // namespace

std::optional<Wordchain> WordchainCommand(std::string_view text) {
  if (text.size() < kCommand.size() ||
      !absl::EqualsIgnoreCase(text.substr(0, kCommand.size()), kCommand)) {
    return std::nullopt;
  }
  const std::string_view rest = text.substr(kCommand.size());
  if (rest.empty() || !absl::ascii_isspace(static_cast<unsigned char>(rest.front()))) {
    return std::nullopt;
  }
  const std::vector<std::string_view> words =
      absl::StrSplit(rest, absl::ByAnyChar(" \t\r\n"), absl::SkipEmpty());
  if (words.size() != 2 || !IsWord(words[0]) || !IsWord(words[1])) return std::nullopt;
  return Wordchain{absl::AsciiStrToLower(words[0]), absl::AsciiStrToLower(words[1]), std::nullopt};
}

std::string WordchainText(const Wordchain& chain) {
  if (!chain.path.has_value()) return absl::StrCat(kNoLadder, chain.start, kTo, chain.end);
  return absl::StrJoin(*chain.path, kRung);
}

std::optional<Wordchain> WordchainOfText(std::string_view text) {
  if (absl::ConsumePrefix(&text, kNoLadder)) {
    const std::vector<std::string_view> ends = absl::StrSplit(text, kTo);
    if (ends.size() != 2 || !IsLowerWord(ends[0]) || !IsLowerWord(ends[1])) return std::nullopt;
    return Wordchain{std::string(ends[0]), std::string(ends[1]), std::nullopt};
  }
  std::vector<std::string> path = absl::StrSplit(text, kRung);
  for (const std::string& word : path) {
    if (!IsLowerWord(word)) return std::nullopt;
  }
  return Wordchain{path.front(), path.back(), std::move(path)};
}

std::shared_ptr<Responder> WordchainResponder(std::shared_ptr<mithril::Client> client) {
  return std::make_shared<Mithril>(std::move(client));
}

}  // namespace games_hub
