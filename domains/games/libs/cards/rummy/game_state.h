#ifndef CPP_CARDS_RUMMY_GAME_STATE_H
#define CPP_CARDS_RUMMY_GAME_STATE_H

#include <deque>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "domains/games/libs/cards/card.h"

namespace rummy {
using namespace cards;
using std::string;

/// Rummy (#245), the basic game: be the first to get rid of every card
/// by laying them down in melds.
///
/// The rules this engine plays:
///   - 2-4 players, one deck, seven cards a seat. One card is turned up
///     to start the discard pile; the rest is the stock. The opener the
///     table names moves first (TableState: the seat after the dealer).
///   - A turn opens with a draw: the top of the stock, or the top of the
///     discard pile. A stock that has run out is refilled by turning the
///     discard pile over, all but its top card; with nothing under the top
///     there is no stock to draw from.
///   - Then any number of melds and lay-offs, in any order. A meld puts
///     down a set or a run from the hand (arrangedMeld); a lay-off adds
///     one card to any meld on the table, anyone's, when the meld stays a
///     meld.
///   - The turn ends with one card discarded. The card taken from the
///     discard pile this turn may not go straight back, unless it is the
///     last card in the hand.
///   - A seat that empties its hand — by a discard, a meld or a lay-off —
///     wins, and that ends the game. The winner scores what everyone else
///     still holds (cardPoints).
///
/// Refusals are absl statuses: FailedPrecondition for the wrong turn or
/// the wrong part of it, InvalidArgument for cards that make no meld, and
/// NotFound for a card the hand does not hold — which is how the hub
/// tells a player acting on a view the table has moved past (#1505).
class GameState;

enum class Phase { Playing, Over, Abandoned };

/// Where the seat on turn is within it: about to draw, or holding the
/// drawn card with melds, lay-offs and the discard to come.
enum class Stage { Draw, Play };

/// Cards on the table: laid in order (arrangedMeld) by `owner`, who may
/// since have left. A meld is never taken back; lay-offs only grow it.
struct Meld {
  string owner;
  std::vector<Card> cards;
  bool operator==(const Meld& o) const { return owner == o.owner && cards == o.cards; }
};

enum class MoveKind { DrawStock, DrawDiscard, Meld, LayOff, Discard };

/// The table's most recent move, as everyone saw it. `cards` is what went
/// on or came off the table: nothing for a stock draw (nobody sees it),
/// the card taken for a discard draw, the meld's cards, the card laid
/// off, the card discarded. `meld` is the table meld a meld or lay-off
/// made or grew, -1 otherwise. The seat named may since have left.
struct LastMove {
  string playerId;
  MoveKind kind = MoveKind::DrawStock;
  std::vector<Card> cards;
  int meld = -1;
  bool operator==(const LastMove& o) const {
    return playerId == o.playerId && kind == o.kind && cards == o.cards && meld == o.meld;
  }
};

/// One seat: who, and the hand in the order its cards arrived (a drawn
/// card goes on the end). Nothing outside the engine addresses a card by
/// its position; moves name cards.
struct Player {
  string id;
  std::vector<Card> hand;
  bool operator==(const Player& o) const { return id == o.id && hand == o.hand; }
};

/// Deals a fresh game from an already-shuffled deck (drawn from the
/// back): one card a seat around the table until each has its hand, then
/// one card face up to the discard pile. Play opens at `opener`'s draw.
[[nodiscard]] absl::StatusOr<GameState> dealRummyGame(const string& game_id,
                                                      const std::vector<string>& player_ids,
                                                      std::deque<Card> shuffled_deck,
                                                      int opener = 0);

class GameState {
 public:
  static constexpr int kMinPlayers = 2;
  static constexpr int kMaxPlayers = 4;
  /// whoseTurn once the game is over.
  static constexpr int kNoTurn = -1;

  /// The hand each seat is dealt at a table of this size.
  static constexpr int kHandSize = 7;

  GameState(std::deque<Card> _stock, std::vector<Card> _discard, std::vector<Player> _players,
            std::vector<Meld> _melds, int _whoseTurn, Stage _stage, Phase _phase,
            std::optional<Card> _takenDiscard, string _gameId, string _versionId,
            std::optional<LastMove> _lastMove = std::nullopt)
      : stock(std::move(_stock)),
        discardPile(std::move(_discard)),
        players(std::move(_players)),
        melds(std::move(_melds)),
        whoseTurn(_whoseTurn),
        stage(_stage),
        phase(_phase),
        takenDiscard(std::move(_takenDiscard)),
        lastMove(std::move(_lastMove)),
        gameId(std::move(_gameId)),
        versionId(std::move(_versionId)) {}

  // The draw.
  [[nodiscard]] absl::StatusOr<GameState> drawStock(int player) const;
  [[nodiscard]] absl::StatusOr<GameState> drawDiscard(int player) const;
  // After it.
  [[nodiscard]] absl::StatusOr<GameState> meld(int player, const std::vector<Card>& cards) const;
  [[nodiscard]] absl::StatusOr<GameState> layOff(int player, const Card& card, int meldIndex) const;
  [[nodiscard]] absl::StatusOr<GameState> discard(int player, const Card& card) const;

  /// A seat abandoned mid-game: it leaves with its hand, indices compact,
  /// and a turn it held passes to the next seat's draw. Its melds stay on
  /// the table. Below two seats the game is over by abandonment and nobody
  /// wins.
  [[nodiscard]] absl::StatusOr<GameState> removePlayer(int player) const;

  // Queries.
  [[nodiscard]] bool isOver() const { return phase != Phase::Playing; }
  [[nodiscard]] Phase getPhase() const { return phase; }
  [[nodiscard]] Stage getStage() const { return stage; }
  /// The seat that went out, once the game is over by play.
  [[nodiscard]] std::optional<string> winner() const;
  /// What a seat's hand counts against it (cardPoints summed).
  [[nodiscard]] int deadwood(int player) const;
  /// The winner's score: everyone else's deadwood. Zero with no winner.
  [[nodiscard]] int winnerPoints() const;
  /// Whether the stock can be drawn from right now: it holds cards, or the
  /// discard pile has cards under its top to turn over.
  [[nodiscard]] bool canDrawStock() const;

  [[nodiscard]] GameState withIdAndVersion(const string& game_id, const string& version_id) const;
  [[nodiscard]] const std::deque<Card>& getStock() const { return stock; }
  [[nodiscard]] const std::vector<Card>& getDiscard() const { return discardPile; }
  [[nodiscard]] const std::vector<Player>& getPlayers() const { return players; }
  [[nodiscard]] const Player& getPlayer(int index) const { return players.at(index); }
  [[nodiscard]] const std::vector<Meld>& getMelds() const { return melds; }
  [[nodiscard]] int playerIndex(const string& id) const;
  [[nodiscard]] int getWhoseTurn() const { return whoseTurn; }
  /// The card the seat on turn took from the discard pile this turn.
  [[nodiscard]] const std::optional<Card>& getTakenDiscard() const { return takenDiscard; }
  [[nodiscard]] const std::optional<LastMove>& getLastMove() const { return lastMove; }
  [[nodiscard]] const string& getGameId() const { return gameId; }
  [[nodiscard]] const string& getVersionId() const { return versionId; }

 private:
  [[nodiscard]] absl::Status ensureTurn(int player, Stage wanted) const;
  /// The state after the seat on turn put cards down, its hand now `hand`:
  /// over if the hand is empty, else still its turn.
  [[nodiscard]] GameState afterLaying(int player, std::vector<Card> hand, std::vector<Meld> table,
                                      LastMove move) const;

  const std::deque<Card> stock;         // back is the top
  const std::vector<Card> discardPile;  // back is the top
  const std::vector<Player> players;
  const std::vector<Meld> melds;
  const int whoseTurn;
  const Stage stage;
  const Phase phase;
  const std::optional<Card> takenDiscard;
  const std::optional<LastMove> lastMove;
  const string gameId;
  const string versionId;
};

}  // namespace rummy

#endif
