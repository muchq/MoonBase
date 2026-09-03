#ifndef CPP_CARDS_CASTLE_PLAYER_H
#define CPP_CARDS_CASTLE_PLAYER_H

#include <string>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "domains/games/libs/cards/card.h"

namespace castle {
using namespace cards;

/// Where a seat's next card comes from: the hand while it holds cards,
/// then the face-up row, then the face-down row played blind.
enum class Source { Hand, FaceUp, FaceDown };

/// One seat: a hand, a face-up row on the table, and the face-down row
/// beneath it (the castle). Immutable; every change is a new Player.
class Player {
 public:
  Player(std::string _id, std::vector<Card> _hand, std::vector<Card> _faceUp,
         std::vector<Card> _faceDown)
      : Player(std::move(_id), std::move(_hand), std::move(_faceUp), std::move(_faceDown), false) {}
  Player(std::string _id, std::vector<Card> _hand, std::vector<Card> _faceUp,
         std::vector<Card> _faceDown, bool _ready)
      : id(std::move(_id)),
        hand(std::move(_hand)),
        faceUp(std::move(_faceUp)),
        faceDown(std::move(_faceDown)),
        ready(_ready) {}

  [[nodiscard]] const std::string& getId() const { return id; }
  [[nodiscard]] const std::vector<Card>& getHand() const { return hand; }
  [[nodiscard]] const std::vector<Card>& getFaceUp() const { return faceUp; }
  [[nodiscard]] const std::vector<Card>& getFaceDown() const { return faceDown; }
  [[nodiscard]] bool isReady() const { return ready; }
  [[nodiscard]] const std::vector<Card>& row(Source source) const;

  [[nodiscard]] Source source() const;
  [[nodiscard]] bool isOut() const;
  [[nodiscard]] int cardsLeft() const;

  /// Setup: exchange a hand card with a face-up card.
  [[nodiscard]] absl::StatusOr<Player> swapForSetup(int handIndex, int faceUpIndex) const;
  [[nodiscard]] Player withReady() const;
  /// The cards at these indexes leave the row. Indexes must be distinct
  /// and in range; the row's remaining order is kept.
  [[nodiscard]] absl::StatusOr<Player> without(Source source,
                                               const std::vector<int>& indexes) const;
  [[nodiscard]] Player withHandAdded(const std::vector<Card>& cards) const;

  bool operator==(const Player& o) const {
    return id == o.id && hand == o.hand && faceUp == o.faceUp && faceDown == o.faceDown &&
           ready == o.ready;
  }

 private:
  const std::string id;
  const std::vector<Card> hand;
  const std::vector<Card> faceUp;
  const std::vector<Card> faceDown;
  const bool ready;
};

}  // namespace castle

#endif
