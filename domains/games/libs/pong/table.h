#ifndef DOMAINS_GAMES_LIBS_PONG_TABLE_H
#define DOMAINS_GAMES_LIBS_PONG_TABLE_H

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"

namespace pong {

inline constexpr int kSeats = 2;
/// Each seat's lives at a game's start, as smallcat.dog gave them.
inline constexpr int kLives = 3;

/// smallcat's flat court, seen side on; or the 3D tunnel, seen from
/// behind your own paddle.
enum class Variant { kSmallcat, kThreeD };
[[nodiscard]] std::string_view VariantName(Variant variant);
[[nodiscard]] std::optional<Variant> ParseVariant(std::string_view name);

/// A game is won on points, or by the other seat leaving it.
enum class Ending { kPoints, kAbandoned };
[[nodiscard]] std::string_view EndingName(Ending ending);
[[nodiscard]] std::optional<Ending> ParseEnding(std::string_view name);

/// A line of the table's score sheet: one game's end.
struct GameScore {
  std::optional<std::string> winner;
  Ending ending = Ending::kPoints;
  bool operator==(const GameScore&) const = default;
};

/// A pong table: two seats playing one game after another until one of
/// them leaves, the way a chess table does. What it holds is the score —
/// lives and the sheet — and never the ball: the rally (rally.h) is
/// motion, played between points and lost with nothing.
///
///   - Seat i defends end i of the court, every game.
///   - A miss costs the seat a life; the seat that runs out loses the
///     game, which is a line on the sheet, and the table waits for the
///     next.
///   - A leave closes the table. In a game it abandons that game to the
///     other seat; between games it scores nothing.
class Table {
 public:
  [[nodiscard]] static absl::StatusOr<Table> open(std::vector<std::string> players,
                                                  Variant variant);
  /// The full truth, for a stored row: refused unless play could reach it.
  [[nodiscard]] static absl::StatusOr<Table> restore(std::vector<std::string> players,
                                                     Variant variant, std::array<int, kSeats> lives,
                                                     int points, std::vector<GameScore> score_sheet,
                                                     bool in_play, bool closed,
                                                     bool ended_by_close);

  /// The seat that missed loses a life. Only in a game.
  [[nodiscard]] absl::StatusOr<Table> point(int missed_by) const;
  /// The next game, on `variant`, every life back. Only between games.
  [[nodiscard]] absl::StatusOr<Table> next(Variant variant) const;
  /// A seat leaving: the table closes.
  [[nodiscard]] absl::StatusOr<Table> removePlayer(int seat) const;

  /// Closed: a seat left.
  [[nodiscard]] bool isOver() const { return closed_; }
  /// The close ended the game in play, which the sheet's last line scores.
  [[nodiscard]] bool endedByClose() const { return ended_by_close_; }
  /// A game is being played.
  [[nodiscard]] bool inPlay() const { return in_play_; }
  [[nodiscard]] Variant variant() const { return variant_; }
  [[nodiscard]] const std::vector<std::string>& players() const { return players_; }
  [[nodiscard]] int playerIndex(const std::string& id) const;
  [[nodiscard]] const std::array<int, kSeats>& lives() const { return lives_; }
  /// Points played in this game, or the last one between games.
  [[nodiscard]] int points() const { return points_; }
  [[nodiscard]] const std::vector<GameScore>& scoreSheet() const { return score_sheet_; }

 private:
  Table(std::vector<std::string> players, Variant variant, std::array<int, kSeats> lives,
        int points, std::vector<GameScore> score_sheet, bool in_play, bool closed,
        bool ended_by_close)
      : players_(std::move(players)),
        variant_(variant),
        lives_(lives),
        points_(points),
        score_sheet_(std::move(score_sheet)),
        in_play_(in_play),
        closed_(closed),
        ended_by_close_(ended_by_close) {}

  std::vector<std::string> players_;
  Variant variant_ = Variant::kSmallcat;
  std::array<int, kSeats> lives_{};
  int points_ = 0;
  std::vector<GameScore> score_sheet_;
  bool in_play_ = false;
  bool closed_ = false;
  bool ended_by_close_ = false;
};

}  // namespace pong

#endif  // DOMAINS_GAMES_LIBS_PONG_TABLE_H
