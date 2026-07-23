#ifndef CPP_CARDS_GOLF_GAME_STATE_H
#define CPP_CARDS_GOLF_GAME_STATE_H

#include <deque>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/golf/player.h"

namespace golf {
using namespace cards;
using std::string;

class GameState;

/// Deals a fresh game from an already-shuffled deck: four cards a seat
/// (drawn from the back), one card seeding the discard, first seat to
/// move. The engine owns the opening layout and the fresh-game
/// invariants; callers only choose the shuffle. 2-4 players.
[[nodiscard]] absl::StatusOr<GameState> dealGolfGame(const string& game_id,
                                                     const std::vector<string>& player_ids,
                                                     std::deque<Card> shuffled_deck);

class GameState {
 public:
  GameState(std::deque<Card> _drawPile, std::deque<Card> _discardPile, std::vector<Player> _players,
            bool _peekedAtDrawPile, int _whoseTurn, int _whoKnocked)
      : drawPile(std::move(_drawPile)),
        discardPile(std::move(_discardPile)),
        players(std::move(_players)),
        peekedAtDrawPile(_peekedAtDrawPile),
        whoseTurn(_whoseTurn),
        whoKnocked(_whoKnocked) {}

  GameState(std::deque<Card> _drawPile, std::deque<Card> _discardPile, std::vector<Player> _players,
            bool _peekedAtDrawPile, int _whoseTurn, int _whoKnocked, string _gameId,
            string _version_id)
      : drawPile(std::move(_drawPile)),
        discardPile(std::move(_discardPile)),
        players(std::move(_players)),
        peekedAtDrawPile(_peekedAtDrawPile),
        whoseTurn(_whoseTurn),
        whoKnocked(_whoKnocked),
        gameId(std::move(_gameId)),
        version_id(std::move(_version_id)) {}

  GameState(std::deque<Card> _drawPile, std::deque<Card> _discardPile, std::vector<Player> _players,
            bool _peekedAtDrawPile, int _whoseTurn, int _whoKnocked, bool _peeksHidden,
            string _gameId, string _version_id)
      : drawPile(std::move(_drawPile)),
        discardPile(std::move(_discardPile)),
        players(std::move(_players)),
        peekedAtDrawPile(_peekedAtDrawPile),
        whoseTurn(_whoseTurn),
        whoKnocked(_whoKnocked),
        peeksHidden(_peeksHidden),
        gameId(std::move(_gameId)),
        version_id(std::move(_version_id)) {}
  [[nodiscard]] bool isOver() const;
  [[nodiscard]] bool allPlayersPresent() const;
  [[nodiscard]] std::unordered_set<int> winners() const;  // winning player indices
  [[nodiscard]] absl::StatusOr<GameState> peekAtDrawPile(int player) const;
  [[nodiscard]] absl::StatusOr<GameState> swapForDrawPile(int player, Position Position) const;
  [[nodiscard]] absl::StatusOr<GameState> swapDrawForDiscardPile(int player) const;
  [[nodiscard]] absl::StatusOr<GameState> swapForDiscardPile(int player, Position Position) const;
  [[nodiscard]] absl::StatusOr<GameState> knock(int player) const;

  /// The opening reveal: before turn play, each player peeks at two of
  /// their own cards. When the last player finishes, the reveal countdown
  /// is active — turn moves wait — until any player's hideCards ends it
  /// for the whole table. Peeks are per-game: once hidden they cannot
  /// restart. (Distinct from peekedAtDrawPile, which is the draw
  /// mechanic: looking at the pile top commits that turn to it.)
  [[nodiscard]] absl::StatusOr<GameState> peekOwnCard(int player, Position position) const;
  [[nodiscard]] absl::StatusOr<GameState> hideCards(int player) const;
  [[nodiscard]] bool allPlayersPeeked() const;
  [[nodiscard]] bool revealCountdownActive() const;
  [[nodiscard]] bool getPeeksHidden() const { return peeksHidden; }

  /// A seat abandoned mid-game: the seat disappears and indices compact.
  /// A departing knocker voids the knock; below two players the game is
  /// over and scores stand.
  [[nodiscard]] absl::StatusOr<GameState> removePlayer(int player) const;

  [[nodiscard]] GameState withPlayers(std::vector<Player> newPlayers) const;
  [[nodiscard]] GameState withIdAndVersion(const string& game_id, const string& version_id) const;
  [[nodiscard]] const std::deque<Card>& getDrawPile() const { return drawPile; }
  [[nodiscard]] const std::deque<Card>& getDiscardPile() const { return discardPile; }
  [[nodiscard]] const std::vector<Player>& getPlayers() const { return players; }
  [[nodiscard]] const Player& getPlayer(const int index) const { return players.at(index); }
  [[nodiscard]] int playerIndex(const string& username) const {
    int i = 0;
    for (auto& p : players) {
      if (p.nameMatches(username)) {
        return i;
      }
      i++;
    }
    return -1;
  }
  [[nodiscard]] bool getPeekedAtDrawPile() const { return peekedAtDrawPile; }
  [[nodiscard]] int getWhoseTurn() const { return whoseTurn; }
  [[nodiscard]] int getWhoKnocked() const { return whoKnocked; }
  [[nodiscard]] const string& getGameId() const { return gameId; }
  [[nodiscard]] const string& getVersionId() const { return version_id; }

 private:
  [[nodiscard]] absl::Status ensurePlayableTurn(int player) const;

  const std::deque<Card> drawPile;
  const std::deque<Card> discardPile;
  const std::vector<Player> players;
  const bool peekedAtDrawPile;
  const int whoseTurn;
  const int whoKnocked;
  const bool peeksHidden = false;
  const std::string gameId;
  const std::string version_id;
};

typedef std::shared_ptr<const GameState> GameStatePtr;

}  // namespace golf

#endif
