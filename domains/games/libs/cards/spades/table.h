#ifndef DOMAINS_GAMES_LIBS_CARDS_SPADES_TABLE_H
#define DOMAINS_GAMES_LIBS_CARDS_SPADES_TABLE_H

#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "domains/games/libs/cards/spades/hand.h"
#include "domains/games/libs/cards/spades/scoring.h"

namespace spades {

/// Where the table stands: a hand bid or played, a hand scored and the
/// next to deal, a game won and the next to deal, or a seat gone.
enum class Phase { kBidding, kPlaying, kScored, kWon, kClosed };
[[nodiscard]] std::string_view phaseName(Phase phase);

/// Partners across four seats, two teams; or cutthroat, every seat for
/// itself, a team of one.
enum class Mode { kPartners, kCutthroat };
[[nodiscard]] std::string_view modeName(Mode mode);
[[nodiscard]] std::optional<Mode> parseMode(std::string_view name);

/// A line of the game's score sheet: one hand's bids and tricks, seat by
/// seat, what each team scored on it and its total after.
struct HandLine {
  std::vector<int> bids;
  std::vector<int> tricks;
  std::vector<int> points;
  std::vector<int> scores;
  bool operator==(const HandLine&) const = default;
};

/// A game's end: won by a team on points, or abandoned by a leave (no
/// winner); each team's score as it ended, team 0 first.
struct GameScore {
  std::optional<int> winner;
  std::vector<int> scores;
  bool operator==(const GameScore&) const = default;
};

/// A spades table (#1658): partners at four seats, or cutthroat at two to
/// four, playing hands until a team wins the game (scoring.h), then game
/// after game until a seat leaves.
///
///   - The first hand is dealt by seat 0; the deal passes left each hand.
///   - A hand played out is scored and kept, for its tricks to be seen,
///     until any seat deals the next. A hand that wins the game ends it;
///     the next deal starts a new game, scores and bags at nothing.
///   - A seat leaving closes the table; a game in play is abandoned.
class Table {
 public:
  [[nodiscard]] static absl::StatusOr<Table> open(Mode mode, std::vector<std::string> seats,
                                                  std::deque<Card> shuffled);
  /// The full truth, for a stored row: refused unless play could reach it.
  [[nodiscard]] static absl::StatusOr<Table> restore(Mode mode, std::vector<std::string> seats,
                                                     Hand hand, int hand_number,
                                                     std::vector<int> scores, std::vector<int> bags,
                                                     std::vector<HandLine> hands,
                                                     std::vector<GameScore> games,
                                                     std::optional<int> left);

  [[nodiscard]] absl::StatusOr<Table> bid(int seat, int tricks) const;
  [[nodiscard]] absl::StatusOr<Table> play(int seat, const Card& card) const;
  /// The next hand, once this one is scored; after a won game, the first
  /// of a new one.
  [[nodiscard]] absl::StatusOr<Table> deal(std::deque<Card> shuffled) const;
  /// A seat leaving: the table closes.
  [[nodiscard]] absl::StatusOr<Table> removePlayer(int seat) const;

  /// Closed: a seat left.
  [[nodiscard]] bool isOver() const { return left_.has_value(); }
  [[nodiscard]] Phase phase() const;
  /// The team that won the game the last hand ended.
  [[nodiscard]] std::optional<int> winner() const;
  /// The leave that closed the table abandoned a game in play.
  [[nodiscard]] bool abandonedGame() const;
  /// The game's end while the table shows it: won, or abandoned by the
  /// leave that closed it.
  [[nodiscard]] std::optional<GameScore> result() const;
  [[nodiscard]] Mode mode() const { return mode_; }
  [[nodiscard]] const std::vector<std::string>& seats() const { return seats_; }
  /// Two teams of partners, or one a seat.
  [[nodiscard]] int teams() const;
  [[nodiscard]] int teamOf(int seat) const { return seat % teams(); }
  [[nodiscard]] int playerIndex(const std::string& id) const;
  /// The hand in play, or the one last played out.
  [[nodiscard]] const Hand& hand() const { return hand_; }
  /// Hands dealt in this game, from 1.
  [[nodiscard]] int handNumber() const { return hand_number_; }
  [[nodiscard]] const std::vector<int>& scores() const { return scores_; }
  [[nodiscard]] const std::vector<int>& bags() const { return bags_; }
  /// This game's hands, in order.
  [[nodiscard]] const std::vector<HandLine>& hands() const { return hands_; }
  /// Every game this table has ended, in order.
  [[nodiscard]] const std::vector<GameScore>& games() const { return games_; }
  /// Games won, team by team.
  [[nodiscard]] std::vector<int> gamesWon() const;
  /// The seat that left, closing the table.
  [[nodiscard]] const std::optional<int>& left() const { return left_; }

  bool operator==(const Table&) const = default;

 private:
  Table(Mode mode, std::vector<std::string> seats, Hand hand, int hand_number,
        std::vector<int> scores, std::vector<int> bags, std::vector<HandLine> hands,
        std::vector<GameScore> games, std::optional<int> left)
      : mode_(mode),
        seats_(std::move(seats)),
        hand_(std::move(hand)),
        hand_number_(hand_number),
        scores_(std::move(scores)),
        bags_(std::move(bags)),
        hands_(std::move(hands)),
        games_(std::move(games)),
        left_(left) {}

  /// This table with `next` as its hand, scored if it is played out.
  [[nodiscard]] Table withHand(Hand next) const;

  Mode mode_ = Mode::kPartners;
  std::vector<std::string> seats_;
  Hand hand_;
  int hand_number_ = 1;
  std::vector<int> scores_;
  std::vector<int> bags_;
  std::vector<HandLine> hands_;
  std::vector<GameScore> games_;
  std::optional<int> left_;
};

}  // namespace spades

#endif  // DOMAINS_GAMES_LIBS_CARDS_SPADES_TABLE_H
