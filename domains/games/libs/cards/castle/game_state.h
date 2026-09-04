#ifndef CPP_CARDS_CASTLE_GAME_STATE_H
#define CPP_CARDS_CASTLE_GAME_STATE_H

#include <deque>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/castle/player.h"

namespace castle {
using namespace cards;
using std::string;

/// Castle, the shedding game (also played as Palace): be the first to
/// get rid of every card; whoever is left holding cards loses.
///
/// The rules this engine plays:
///   - 2-4 players, one deck. Each seat is dealt three face-down cards,
///     three face-up cards on top of them, and three in hand.
///   - Setup: each player may swap hand cards with their own face-up
///     cards, then declares ready. Play opens when every seat is ready;
///     the seat holding the lowest ordinary card in hand (three low, ace
///     high; twos and tens are specials and do not count; face-up rows
///     do not count) goes first, the earliest seat on a tie, seat 0 when
///     no hand holds an ordinary card.
///   - A turn plays one or more cards of a single rank from the seat's
///     active row (hand while it has cards, then the face-up row, then
///     the face-down row one card at a time, blind) onto the pile. The
///     run on top sets the price: after n cards of rank k, a play is n or
///     more cards of rank k or higher (two is the lowest rank, so a two
///     on top asks only for its count), or exactly the 4-n cards of rank
///     k that complete its four of a kind. Twos and tens play on anything
///     in any count; anything plays on an empty pile. A hand play draws
///     back up to three while the draw pile lasts; face-up and face-down
///     plays never draw. The turn passes to the next seat, wrapping.
///   - A ten, or four of a kind on top of the pile, burns the pile: it
///     leaves the game and the same seat plays again from whichever row
///     is then in play — unless the burn shed the seat's last card, which
///     ends the game. A burn is a play like any other: the cards must be
///     playable on the pile as it stands, and a run of four is broken by
///     a card of another rank.
///   - A seat with no legal play in hand or in the face-up row must pick
///     up the pile. A face-down card that turns out unplayable goes into
///     the hand with the pile.
///   - The first seat to shed its last card wins, and that ends the
///     game. The loser is the one seat still holding cards, which a
///     two-seat game always has and a bigger table usually does not.
class GameState;

enum class Phase { Setup, Playing, Over, Abandoned };

/// Deals a fresh game from an already-shuffled deck (drawn from the
/// back), in the setup phase. Nine cards a seat — face-down row, then
/// face-up row, then hand; the rest is the draw pile.
[[nodiscard]] absl::StatusOr<GameState> dealCastleGame(const string& game_id,
                                                       const std::vector<string>& player_ids,
                                                       std::deque<Card> shuffled_deck);

class GameState {
 public:
  static constexpr int kHandSize = 3;
  static constexpr int kMinPlayers = 2;
  static constexpr int kMaxPlayers = 4;
  /// whoseTurn once no seat has a turn: setup and every ending.
  static constexpr int kNoTurn = -1;

  GameState(std::deque<Card> _drawPile, std::vector<Card> _pile, std::vector<Player> _players,
            int _whoseTurn, Phase _phase, std::vector<string> _finished, string _gameId,
            string _versionId)
      : drawPile(std::move(_drawPile)),
        pile(std::move(_pile)),
        players(std::move(_players)),
        whoseTurn(_whoseTurn),
        phase(_phase),
        finished(std::move(_finished)),
        gameId(std::move(_gameId)),
        versionId(std::move(_versionId)) {}

  // Setup.
  [[nodiscard]] absl::StatusOr<GameState> swapForSetup(int player, int handIndex,
                                                       int faceUpIndex) const;
  [[nodiscard]] absl::StatusOr<GameState> ready(int player) const;

  // Turns.
  [[nodiscard]] absl::StatusOr<GameState> playFromHand(int player,
                                                       const std::vector<int>& indexes) const;
  [[nodiscard]] absl::StatusOr<GameState> playFaceUp(int player,
                                                     const std::vector<int>& indexes) const;
  [[nodiscard]] absl::StatusOr<GameState> playFaceDown(int player, int index) const;
  [[nodiscard]] absl::StatusOr<GameState> pickUp(int player) const;

  /// A seat abandoned mid-game: it disappears with its cards, indices
  /// compact, and a turn it held passes on. Below two seats, or below two
  /// seats holding cards, the game is over by abandonment: the finish
  /// order stands and nobody loses, since no play ended it.
  [[nodiscard]] absl::StatusOr<GameState> removePlayer(int player) const;

  // Queries.
  [[nodiscard]] bool isOver() const { return phase == Phase::Over || phase == Phase::Abandoned; }
  [[nodiscard]] Phase getPhase() const { return phase; }
  [[nodiscard]] std::optional<Card> pileTop() const;
  /// How many cards of one rank sit on top of the pile: the count the
  /// next play must match. Zero on an empty pile.
  [[nodiscard]] int runOnTop() const;
  /// Whether `count` cards of this rank may go on the pile as it stands:
  /// a special always; on an empty pile anything; otherwise the count on
  /// top or more of that rank or higher, or exactly what completes the
  /// four of a kind of the top's own rank. No cards is never a play.
  [[nodiscard]] bool isPlayable(Rank rank, int count = 1) const;
  /// Whether the seat's active row holds a legal play. Always false for
  /// a face-down row: those are played blind.
  [[nodiscard]] bool hasLegalPlay(int player) const;
  /// Seats that went out, first out first.
  [[nodiscard]] const std::vector<string>& getFinished() const { return finished; }
  /// The one seat still holding cards once the game is over by play:
  /// always the other seat of a two-seat game, usually nobody at a
  /// bigger table.
  [[nodiscard]] std::optional<string> loser() const;

  [[nodiscard]] GameState withIdAndVersion(const string& game_id, const string& version_id) const;
  [[nodiscard]] const std::deque<Card>& getDrawPile() const { return drawPile; }
  [[nodiscard]] const std::vector<Card>& getPile() const { return pile; }
  [[nodiscard]] const std::vector<Player>& getPlayers() const { return players; }
  [[nodiscard]] const Player& getPlayer(int index) const { return players.at(index); }
  [[nodiscard]] int playerIndex(const string& id) const;
  [[nodiscard]] int getWhoseTurn() const { return whoseTurn; }
  [[nodiscard]] const string& getGameId() const { return gameId; }
  [[nodiscard]] const string& getVersionId() const { return versionId; }

 private:
  [[nodiscard]] absl::Status ensureSeat(int player) const;
  [[nodiscard]] absl::Status ensurePlayableTurn(int player) const;
  [[nodiscard]] absl::StatusOr<GameState> play(int player, Source source,
                                               const std::vector<int>& indexes) const;
  /// The state after a seat's cards landed on the pile: burns, finishes,
  /// the next turn, and the end of the game.
  [[nodiscard]] GameState settle(int player, std::deque<Card> newDrawPile,
                                 std::vector<Card> newPile, std::vector<Player> newPlayers,
                                 bool burned) const;
  [[nodiscard]] int nextSeat(int from, const std::vector<Player>& roster) const;

  const std::deque<Card> drawPile;
  const std::vector<Card> pile;  // back is the top
  const std::vector<Player> players;
  const int whoseTurn;
  const Phase phase;
  const std::vector<string> finished;
  const string gameId;
  const string versionId;
};

}  // namespace castle

#endif
