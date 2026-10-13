#include "domains/games/libs/cards/spades/table.h"

#include <algorithm>
#include <set>
#include <utility>

#include "absl/status/status.h"

namespace spades {
namespace {

std::vector<int> BidsOf(const Hand& hand) {
  std::vector<int> bids;
  for (const std::optional<int>& bid : hand.bids()) bids.push_back(bid.value_or(kNil));
  return bids;
}

absl::Status ValidSeats(Mode mode, const std::vector<std::string>& seats) {
  if (mode == Mode::kPartners && seats.size() != 4) {
    return absl::InvalidArgumentError("partners seats four");
  }
  if (seats.size() < static_cast<std::size_t>(kMinSeats) ||
      seats.size() > static_cast<std::size_t>(kMaxSeats)) {
    return absl::InvalidArgumentError("spades seats two to four");
  }
  if (std::set<std::string>(seats.begin(), seats.end()).size() != seats.size()) {
    return absl::InvalidArgumentError("a seat twice");
  }
  return absl::OkStatus();
}

int TeamsOf(Mode mode, std::size_t seats) {
  return mode == Mode::kPartners ? 2 : static_cast<int>(seats);
}

}  // namespace

std::string_view phaseName(Phase phase) {
  switch (phase) {
    case Phase::kBidding:
      return "bidding";
    case Phase::kPlaying:
      return "playing";
    case Phase::kScored:
      return "scored";
    case Phase::kWon:
      return "won";
    case Phase::kClosed:
      break;
  }
  return "closed";
}

std::string_view modeName(Mode mode) { return mode == Mode::kPartners ? "partners" : "cutthroat"; }

std::optional<Mode> parseMode(std::string_view name) {
  if (name == "partners") return Mode::kPartners;
  if (name == "cutthroat") return Mode::kCutthroat;
  return std::nullopt;
}

absl::StatusOr<Table> Table::open(Mode mode, std::vector<std::string> seats,
                                  std::deque<Card> shuffled) {
  if (absl::Status valid = ValidSeats(mode, seats); !valid.ok()) return valid;
  absl::StatusOr<Hand> hand = Hand::deal(static_cast<int>(seats.size()), 0, std::move(shuffled));
  if (!hand.ok()) return hand.status();
  const int teams = TeamsOf(mode, seats.size());
  return Table(mode, std::move(seats), *std::move(hand), 1, std::vector<int>(teams),
               std::vector<int>(teams), {}, {}, std::nullopt);
}

absl::StatusOr<Table> Table::restore(Mode mode, std::vector<std::string> seats, Hand hand,
                                     int hand_number, std::vector<int> scores,
                                     std::vector<int> bags, std::vector<HandLine> hands,
                                     std::vector<GameScore> games, std::optional<int> left) {
  if (absl::Status valid = ValidSeats(mode, seats); !valid.ok()) return valid;
  const std::size_t seat_count = seats.size();
  const int teams = TeamsOf(mode, seat_count);
  if (static_cast<std::size_t>(hand.seats()) != seat_count) {
    return absl::InvalidArgumentError("a hand a seat");
  }
  if (scores.size() != static_cast<std::size_t>(teams) ||
      bags.size() != static_cast<std::size_t>(teams)) {
    return absl::InvalidArgumentError("a score and bags a team");
  }
  for (int team_bags : bags) {
    if (team_bags < 0 || team_bags >= kBagLimit) {
      return absl::InvalidArgumentError("bags past the limit are paid for");
    }
  }
  if (left.has_value() && (*left < 0 || static_cast<std::size_t>(*left) >= seat_count)) {
    return absl::InvalidArgumentError("no such seat left");
  }
  for (const GameScore& game : games) {
    if (game.scores.size() != static_cast<std::size_t>(teams) ||
        (game.winner.has_value() && (*game.winner < 0 || *game.winner >= teams))) {
      return absl::InvalidArgumentError("no such team");
    }
  }
  for (const HandLine& line : hands) {
    if (line.bids.size() != seat_count || line.tricks.size() != seat_count ||
        line.points.size() != static_cast<std::size_t>(teams) ||
        line.scores.size() != static_cast<std::size_t>(teams)) {
      return absl::InvalidArgumentError("a line counts every seat and team");
    }
  }
  // The hands of this game: one a hand dealt, the one in play included
  // once it is played out; the scores are the last one's.
  const bool done = hand.stage() == Stage::kDone;
  if (hand_number < 1 || static_cast<std::size_t>(hand_number) != hands.size() + (done ? 0 : 1)) {
    return absl::InvalidArgumentError("a line for each hand played out");
  }
  if (scores != (hands.empty() ? std::vector<int>(teams) : hands.back().scores)) {
    return absl::InvalidArgumentError("the scores are the last line's");
  }
  if (done && (hands.back().bids != BidsOf(hand) || hands.back().tricks != hand.tricks())) {
    return absl::InvalidArgumentError("a hand played out is the last line");
  }
  const std::optional<int> won = done ? gameWinner(scores) : std::nullopt;
  if (!done && gameWinner(scores).has_value()) {
    return absl::InvalidArgumentError("a game won plays no further hand");
  }
  // A game won is the games' last; so is one a leave abandoned.
  const bool abandoned = left.has_value() && !won.has_value();
  if ((won.has_value() || abandoned) && (games.empty() || games.back() != GameScore{won, scores})) {
    return absl::InvalidArgumentError("a game ended is the games' last");
  }
  return Table(mode, std::move(seats), std::move(hand), hand_number, std::move(scores),
               std::move(bags), std::move(hands), std::move(games), left);
}

int Table::teams() const { return TeamsOf(mode_, seats_.size()); }

Phase Table::phase() const {
  if (left_.has_value()) return Phase::kClosed;
  switch (hand_.stage()) {
    case Stage::kBidding:
      return Phase::kBidding;
    case Stage::kPlaying:
      return Phase::kPlaying;
    case Stage::kDone:
      break;
  }
  return winner().has_value() ? Phase::kWon : Phase::kScored;
}

std::optional<int> Table::winner() const {
  if (left_.has_value() || hand_.stage() != Stage::kDone) return std::nullopt;
  return gameWinner(scores_);
}

bool Table::abandonedGame() const {
  return left_.has_value() && (hand_.stage() != Stage::kDone || !gameWinner(scores_).has_value());
}

std::optional<GameScore> Table::result() const {
  if (phase() == Phase::kWon || abandonedGame()) return games_.back();
  return std::nullopt;
}

std::vector<int> Table::gamesWon() const {
  std::vector<int> won(teams());
  for (const GameScore& game : games_) {
    if (game.winner.has_value()) ++won[*game.winner];
  }
  return won;
}

int Table::playerIndex(const std::string& id) const {
  const auto at = std::find(seats_.begin(), seats_.end(), id);
  return at == seats_.end() ? -1 : static_cast<int>(at - seats_.begin());
}

Table Table::withHand(Hand next) const {
  if (next.stage() != Stage::kDone) {
    return Table(mode_, seats_, std::move(next), hand_number_, scores_, bags_, hands_, games_,
                 left_);
  }
  const std::vector<TeamScore> scored = scoreHand(BidsOf(next), next.tricks(), bags_);
  std::vector<int> scores = scores_;
  std::vector<int> bags(teams());
  std::vector<int> points(teams());
  for (int team = 0; team < teams(); ++team) {
    points[team] = scored[team].points;
    scores[team] += scored[team].points;
    bags[team] = scored[team].bags;
  }
  std::vector<HandLine> hands = hands_;
  hands.push_back({BidsOf(next), next.tricks(), points, scores});
  std::vector<GameScore> games = games_;
  if (const std::optional<int> won = gameWinner(scores); won.has_value()) {
    games.push_back({won, scores});
  }
  return Table(mode_, seats_, std::move(next), hand_number_, std::move(scores), std::move(bags),
               std::move(hands), std::move(games), left_);
}

absl::StatusOr<Table> Table::bid(int seat, int tricks) const {
  if (left_.has_value()) return absl::FailedPreconditionError("the table has closed");
  absl::StatusOr<Hand> next = hand_.bid(seat, tricks);
  if (!next.ok()) return next.status();
  return withHand(*std::move(next));
}

absl::StatusOr<Table> Table::play(int seat, const Card& card) const {
  if (left_.has_value()) return absl::FailedPreconditionError("the table has closed");
  absl::StatusOr<Hand> next = hand_.play(seat, card);
  if (!next.ok()) return next.status();
  return withHand(*std::move(next));
}

absl::StatusOr<Table> Table::deal(std::deque<Card> shuffled) const {
  const Phase now = phase();
  if (now != Phase::kScored && now != Phase::kWon) {
    return absl::FailedPreconditionError("the hand is not over");
  }
  const int seat_count = hand_.seats();
  absl::StatusOr<Hand> next =
      Hand::deal(seat_count, (hand_.dealer() + 1) % seat_count, std::move(shuffled));
  if (!next.ok()) return next.status();
  if (now == Phase::kWon) {
    return Table(mode_, seats_, *std::move(next), 1, std::vector<int>(teams()),
                 std::vector<int>(teams()), {}, games_, std::nullopt);
  }
  return Table(mode_, seats_, *std::move(next), hand_number_ + 1, scores_, bags_, hands_, games_,
               std::nullopt);
}

absl::StatusOr<Table> Table::removePlayer(int seat) const {
  if (left_.has_value()) return absl::FailedPreconditionError("the table has closed");
  if (seat < 0 || seat >= hand_.seats()) return absl::InvalidArgumentError("no such seat");
  std::vector<GameScore> games = games_;
  // A leave in a game abandons it: a game's end, with no winner.
  if (!winner().has_value()) games.push_back({std::nullopt, scores_});
  return Table(mode_, seats_, hand_, hand_number_, scores_, bags_, hands_, std::move(games), seat);
}

}  // namespace spades
