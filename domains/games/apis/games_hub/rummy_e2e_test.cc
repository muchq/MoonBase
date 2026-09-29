// Rummy on the room stream (#245): the third game on the hub, end to end
// through the generated client.
//
// Every table is dealer's choice (#1609): the creator deals first, and the
// seat after the dealer opens. Two kinds of deal. The NoShuffleDealer
// deals the pristine deck one card a seat from the back, so every card is
// known: at two seats alice (the opener) holds A♥ A♣ K♥ K♣ Q♥ Q♣ J♥, bob
// (the dealer) A♠ A♦ K♠ K♦ Q♠ Q♦ J♠; J♦ is turned up and J♣ tops the
// stock. The SeededDealer shuffles with a fixed seed for whole games, which a local engine mirror
// plays alongside the hub: every turn the mirror picks a move by a simple policy, the same command
// goes to the hub, and every seat's view must agree with the mirror.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "domains/games/apis/games_hub/stream_test_fixture.h"
#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/dealer.h"
#include "domains/games/libs/cards/rummy/game_state.h"
#include "domains/games/libs/cards/rummy/meld.h"

namespace games_hub {
namespace {

// What the dealer may deal at each table size: gin only heads-up, ten
// cards a seat only while the deck covers them.
std::vector<std::string> OfferedAt(size_t seats) {
  if (seats == 2) return {"7-card", "10-card", "gin"};
  if (seats == 3) return {"7-card", "10-card"};
  return {"7-card"};
}

using moonbase::games::CastleMove;
using moonbase::games::GameCommands;
using moonbase::games::RummyMove;
using moonbase::games::RummyUpdate;
using moonbase::games::RummyView;

std::string Face(const moonbase::games::Card& card) { return card.rank + card.suit; }

std::vector<std::string> Faces(const std::vector<moonbase::games::Card>& cards) {
  std::vector<std::string> faces;
  for (const auto& card : cards) faces.push_back(Face(card));
  return faces;
}

// The engine's cards spelled the way the wire spells them — this table,
// not the hub's, so a view is compared to the mirror face by face.
moonbase::games::Card Wire(const cards::Card& card) {
  static constexpr const char* kRanks[] = {"2", "3",  "4", "5", "6", "7", "8",
                                           "9", "10", "J", "Q", "K", "A"};
  static constexpr const char* kSuits[] = {"♣", "♦", "♥", "♠"};
  return Named(kRanks[static_cast<int>(card.getRank())], kSuits[static_cast<int>(card.getSuit())]);
}

std::vector<moonbase::games::Card> Wire(const std::vector<cards::Card>& cards) {
  std::vector<moonbase::games::Card> wire;
  for (const cards::Card& card : cards) wire.push_back(Wire(card));
  return wire;
}

std::vector<std::string> Faces(const std::vector<cards::Card>& cards) { return Faces(Wire(cards)); }

std::string MoveName(rummy::MoveKind kind) {
  switch (kind) {
    case rummy::MoveKind::DrawStock:
      return "drawStock";
    case rummy::MoveKind::DrawDiscard:
      return "drawDiscard";
    case rummy::MoveKind::Meld:
      return "meld";
    case rummy::MoveKind::LayOff:
      return "layOff";
    case rummy::MoveKind::Discard:
      return "discard";
  }
  return "";
}

GameCommands DrawStock() {
  return Rummy(RummyMove::FromDrawstock(moonbase::games::RummyDrawStock{}));
}
GameCommands DrawDiscard() {
  return Rummy(RummyMove::FromDrawdiscard(moonbase::games::RummyDrawDiscard{}));
}
GameCommands MeldOf(std::vector<moonbase::games::Card> cards) {
  moonbase::games::RummyMeld meld;
  meld.cards = std::move(cards);
  return Rummy(RummyMove::FromMeld(std::move(meld)));
}
GameCommands LayOff(moonbase::games::Card card, int meld_index) {
  moonbase::games::RummyLayOff lay_off;
  lay_off.card = std::move(card);
  lay_off.meldIndex = meld_index;
  return Rummy(RummyMove::FromLayoff(std::move(lay_off)));
}
GameCommands Discard(moonbase::games::Card card) {
  moonbase::games::RummyDiscard discard;
  discard.card = std::move(card);
  return Rummy(RummyMove::FromDiscard(std::move(discard)));
}

// A fixed shuffle, so a whole game replays the same every run.
class SeededDealer : public cards::Dealer {
 public:
  explicit SeededDealer(uint32_t seed) : seed_(seed) {}
  void ShuffleDeck(std::deque<cards::Card>& deck) override {
    std::mt19937 generator(seed_);
    std::shuffle(deck.begin(), deck.end(), generator);
  }

 private:
  uint32_t seed_;
};

// --- The mirror's policy: plain, greedy, and legal by construction. ---

// Some three cards of the hand that make a meld, if any do.
std::optional<std::vector<cards::Card>> MeldIn(const std::vector<cards::Card>& hand) {
  for (std::size_t i = 0; i < hand.size(); ++i) {
    for (std::size_t j = i + 1; j < hand.size(); ++j) {
      for (std::size_t k = j + 1; k < hand.size(); ++k) {
        if (rummy::arrangedMeld({hand[i], hand[j], hand[k]}).has_value()) {
          return std::vector<cards::Card>{hand[i], hand[j], hand[k]};
        }
      }
    }
  }
  return std::nullopt;
}

// Whether this card and two of the hand would make a meld.
bool Completes(const std::vector<cards::Card>& hand, const cards::Card& card) {
  for (std::size_t i = 0; i < hand.size(); ++i) {
    for (std::size_t j = i + 1; j < hand.size(); ++j) {
      if (rummy::arrangedMeld({hand[i], hand[j], card}).has_value()) return true;
    }
  }
  return false;
}

// A card of the hand and a meld it fits, if any.
std::optional<std::pair<cards::Card, int>> LayOffIn(const std::vector<cards::Card>& hand,
                                                    const std::vector<rummy::Meld>& melds) {
  for (const cards::Card& card : hand) {
    for (std::size_t m = 0; m < melds.size(); ++m) {
      std::vector<cards::Card> grown = melds[m].cards;
      grown.push_back(card);
      if (rummy::arrangedMeld(grown).has_value()) return std::make_pair(card, static_cast<int>(m));
    }
  }
  return std::nullopt;
}

// The costliest card the hand may throw: never the one just taken from
// the discard, unless it is all that is left.
cards::Card DiscardFrom(const std::vector<cards::Card>& hand,
                        const std::optional<cards::Card>& taken) {
  std::optional<cards::Card> worst;
  for (const cards::Card& card : hand) {
    if (hand.size() > 1 && taken == card) continue;
    if (!worst.has_value() || rummy::cardPoints(card) > rummy::cardPoints(*worst)) worst = card;
  }
  return *worst;
}

class RummyGameFixture : public GamesHubStreamFixture {
 protected:
  // The same deal the hub made, as an engine value: `ids` in seat order,
  // opened by the seat after the dealer — seat 1 on the first deal.
  rummy::GameState MirrorDeal(const std::string& game_id, const std::vector<std::string>& ids,
                              int opener = 1) {
    auto dealer = MakeDealer();
    std::deque<cards::Card> deck = dealer->DealNewUnshuffledDeck();
    dealer->ShuffleDeck(deck);
    auto state = rummy::dealRummyGame(game_id, ids, std::move(deck), opener);
    EXPECT_TRUE(state.ok()) << state.status();
    return *state;
  }

  // The board, as one chair sees it, against the mirror: every public
  // fact, the last move, and hand faces only for the viewer's own seat.
  void ExpectBoard(const RummyView& seen, const std::string& viewer,
                   const rummy::GameState& mirror) {
    const bool ended = mirror.isOver();
    // A deal won by play leaves the table choosing the next; one broken up
    // by a leave closes it.
    EXPECT_EQ(seen.phase, !ended                                    ? "playing"
                          : mirror.getPhase() == rummy::Phase::Over ? "choosing"
                                                                    : "ended");
    EXPECT_EQ(seen.variant.value_or(""), "7-card");
    if (ended) {
      EXPECT_FALSE(seen.currentPlayerId.has_value());
      EXPECT_FALSE(seen.stage.has_value());
      EXPECT_FALSE(seen.canDrawStock);
      EXPECT_FALSE(seen.takenDiscard.has_value());
      // The deal's result rides the view, for every chair and every
      // instance, until the next deal replaces it.
      ASSERT_TRUE(seen.lastDeal.has_value());
      EXPECT_EQ(seen.lastDeal->variant, "7-card");
      EXPECT_EQ(seen.lastDeal->winner, mirror.winner());
      EXPECT_EQ(seen.lastDeal->points, mirror.winnerPoints());
      ASSERT_EQ(seen.lastDeal->scores.size(), mirror.getPlayers().size());
      for (std::size_t i = 0; i < seen.lastDeal->scores.size(); ++i) {
        EXPECT_EQ(seen.lastDeal->scores[i].playerId, mirror.getPlayer(static_cast<int>(i)).id);
        EXPECT_EQ(seen.lastDeal->scores[i].deadwood, mirror.deadwood(static_cast<int>(i)));
      }
    } else {
      EXPECT_FALSE(seen.lastDeal.has_value());
      EXPECT_FALSE(seen.choosing.has_value());
      EXPECT_EQ(seen.currentPlayerId.value_or(""), mirror.getPlayer(mirror.getWhoseTurn()).id);
      EXPECT_EQ(seen.stage.value_or(""), mirror.getStage() == rummy::Stage::Draw ? "draw" : "play");
      EXPECT_EQ(seen.canDrawStock, mirror.canDrawStock());
    }
    EXPECT_EQ(seen.stockCount, static_cast<int>(mirror.getStock().size()));
    EXPECT_EQ(seen.discardCount, static_cast<int>(mirror.getDiscard().size()));
    ASSERT_EQ(seen.discardTop.has_value(), !mirror.getDiscard().empty());
    if (seen.discardTop.has_value()) {
      EXPECT_EQ(Face(*seen.discardTop), Face(Wire(mirror.getDiscard().back())));
    }
    if (!ended) ASSERT_EQ(seen.takenDiscard.has_value(), mirror.getTakenDiscard().has_value());
    if (seen.takenDiscard.has_value()) {
      EXPECT_EQ(Face(*seen.takenDiscard), Face(Wire(*mirror.getTakenDiscard())));
    }
    ASSERT_EQ(seen.melds.size(), mirror.getMelds().size());
    for (std::size_t m = 0; m < seen.melds.size(); ++m) {
      EXPECT_EQ(seen.melds[m].owner, mirror.getMelds()[m].owner);
      EXPECT_EQ(Faces(seen.melds[m].cards), Faces(mirror.getMelds()[m].cards));
    }
    ASSERT_EQ(seen.lastMove.has_value(), mirror.getLastMove().has_value());
    if (seen.lastMove.has_value()) {
      const rummy::LastMove& want = *mirror.getLastMove();
      EXPECT_EQ(seen.lastMove->playerId, want.playerId);
      EXPECT_EQ(seen.lastMove->move, MoveName(want.kind));
      EXPECT_EQ(Faces(seen.lastMove->cards), Faces(want.cards));
      EXPECT_EQ(seen.lastMove->meldIndex.value_or(-1), want.meld);
    }
    ASSERT_EQ(seen.players.size(), mirror.getPlayers().size());
    for (std::size_t i = 0; i < seen.players.size(); ++i) {
      const rummy::Player& want = mirror.getPlayer(static_cast<int>(i));
      EXPECT_EQ(seen.players[i].playerId, want.id);
      EXPECT_EQ(seen.players[i].handCount, static_cast<int>(want.hand.size()));
      EXPECT_EQ(Faces(seen.players[i].hand),
                ended || want.id == viewer ? Faces(want.hand) : std::vector<std::string>{});
    }
  }

  // Every seat's next view, checked against the mirror.
  void ExpectViews(const std::vector<Seat*>& seats, const rummy::GameState& mirror,
                   const std::string& after) {
    for (Seat* seat : seats) {
      auto update = ReceiveRummy(seat->stream, "gameState");
      ASSERT_TRUE(update.has_value()) << seat->player_id << " after " << after;
      ExpectBoard(update->as_gameState_or_null()->view, seat->player_id, mirror);
    }
  }

  // What a game went through on its way to the end.
  struct PlayedOut {
    int moves = 0;
    int discard_draws = 0;
    int stock_draws = 0;
    int melds = 0;
    int lay_offs = 0;
    // Lay-offs onto a meld another seat laid.
    int foreign_lay_offs = 0;
  };

  // Plays the mirror's policy on every seat until someone goes out: the
  // same command to the hub and the mirror, and every seat's view checked
  // after each. Leaves each stream just before the final views.
  void PlayToEnd(const std::vector<Seat*>& seats, std::optional<rummy::GameState>& mirror,
                 PlayedOut& played) {
    std::map<std::string, Seat*> by_id;
    for (Seat* seat : seats) by_id[seat->player_id] = seat;
    auto step = [&](Seat& mover, GameCommands command, absl::StatusOr<rummy::GameState> next,
                    const std::string& what) {
      ASSERT_TRUE(next.ok()) << what << ": " << next.status();
      const int turn_before = mirror->getWhoseTurn();
      mirror.emplace(*std::move(next));
      ++played.moves;
      ASSERT_TRUE(mover.stream.Send(std::move(command)).ok());
      if (mirror->isOver()) return;
      ASSERT_NO_FATAL_FAILURE(ExpectViews(seats, *mirror, what));
      if (mirror->getWhoseTurn() != turn_before) {
        for (Seat* hearer : seats) {
          auto turn = ReceiveRummy(hearer->stream, "turnChanged");
          ASSERT_TRUE(turn.has_value()) << "after " << what;
          EXPECT_EQ(turn->as_turnChanged_or_null()->playerId,
                    mirror->getPlayer(mirror->getWhoseTurn()).id);
        }
      }
    };
    while (!mirror->isOver()) {
      ASSERT_LT(played.moves, 2000) << "the policy's game never ends";
      const int seat = mirror->getWhoseTurn();
      const std::string mover_id = mirror->getPlayer(seat).id;
      Seat& mover = *by_id.at(mover_id);

      const std::vector<cards::Card>& hand = mirror->getPlayer(seat).hand;
      const bool wants_discard =
          !mirror->getDiscard().empty() && Completes(hand, mirror->getDiscard().back());
      if (wants_discard || !mirror->canDrawStock()) {
        ++played.discard_draws;
        ASSERT_NO_FATAL_FAILURE(
            step(mover, DrawDiscard(), mirror->drawDiscard(seat), "drawDiscard"));
      } else {
        ++played.stock_draws;
        ASSERT_NO_FATAL_FAILURE(step(mover, DrawStock(), mirror->drawStock(seat), "drawStock"));
      }
      while (!mirror->isOver()) {
        const std::vector<cards::Card> held = mirror->getPlayer(seat).hand;
        if (auto meld = MeldIn(held); meld.has_value()) {
          ++played.melds;
          ASSERT_NO_FATAL_FAILURE(
              step(mover, MeldOf(Wire(*meld)), mirror->meld(seat, *meld), "meld"));
          continue;
        }
        if (auto lay_off = LayOffIn(held, mirror->getMelds()); lay_off.has_value()) {
          ++played.lay_offs;
          if (mirror->getMelds().at(lay_off->second).owner != mover_id) ++played.foreign_lay_offs;
          ASSERT_NO_FATAL_FAILURE(step(mover, LayOff(Wire(lay_off->first), lay_off->second),
                                       mirror->layOff(seat, lay_off->first, lay_off->second),
                                       "layOff"));
          continue;
        }
        const cards::Card thrown = DiscardFrom(held, mirror->getTakenDiscard());
        ASSERT_NO_FATAL_FAILURE(
            step(mover, Discard(Wire(thrown)), mirror->discard(seat, thrown), "discard"));
        break;
      }
    }
  }

  // A deal's end, from every chair: final views with every hand face up,
  // nobody to play and the deal's result, the next dealer choosing; then
  // the room's stats crediting the seat that went out with a game won and
  // everyone with a game played. The table stays: dealer's choice deals on.
  void ExpectDealEnding(const std::vector<Seat*>& seats, const rummy::GameState& mirror,
                        const std::string& next_dealer, int games_played = 1) {
    ASSERT_EQ(mirror.getPhase(), rummy::Phase::Over);
    const std::string winner = mirror.winner().value_or("");
    ASSERT_FALSE(winner.empty());
    for (Seat* seat : seats) {
      auto final_view = ReceiveRummy(seat->stream, "gameState");
      ASSERT_TRUE(final_view.has_value());
      const RummyView& view = final_view->as_gameState_or_null()->view;
      ExpectBoard(view, seat->player_id, mirror);
      ASSERT_TRUE(view.choosing.has_value());
      EXPECT_EQ(view.choosing->dealer, next_dealer);
      EXPECT_EQ(view.choosing->options, OfferedAt(seats.size()));
      for (const auto& standing : view.standings) {
        if (standing.playerId == winner) EXPECT_GE(standing.handsWon, 1);
      }
      auto room = ReceiveCase(seat->stream, "roomState");
      ASSERT_TRUE(room.has_value());
      ASSERT_EQ(room->as_roomState_or_null()->games.size(), 1u);
      EXPECT_EQ(room->as_roomState_or_null()->games[0].status, "choosing");
      for (const auto& player : room->as_roomState_or_null()->players) {
        EXPECT_EQ(player.gamesPlayed, games_played);
        if (player.playerId == winner) EXPECT_GE(player.gamesWon, 1);
        // Rummy's points are the winner's, on a scale golf's running total
        // does not share: they ride lastDeal and leave the total alone.
        EXPECT_EQ(player.totalScore, 0);
        EXPECT_TRUE(player.table.has_value());
      }
    }
  }
};

// The deal from each chair, then the quickest win the pristine deck
// allows: alice draws J♣ and lays her whole hand down as two runs, J to
// A in hearts and in clubs — no discard needed to go out.
TEST_F(RummyGameFixture, TheDealIsRedactedPerSeatAndLayingDownEverythingWins) {
  auto table = SeatedRummyTable();
  ASSERT_TRUE(table.has_value());
  auto& alice = table->alice;
  auto& bob = table->bob;
  std::optional<rummy::GameState> mirror(
      MirrorDeal(table->game_id, {bob.player_id, alice.player_id}));

  // The dealt view, in rummy's envelope, from each chair.
  ASSERT_NO_FATAL_FAILURE(ExpectViews({&alice, &bob}, *mirror, "the deal"));

  ASSERT_TRUE(alice.stream.Send(DrawStock()).ok());
  auto drew = mirror->drawStock(1);
  ASSERT_TRUE(drew.ok());
  mirror.emplace(*drew);
  ASSERT_NO_FATAL_FAILURE(ExpectViews({&alice, &bob}, *mirror, "drawStock"));

  ASSERT_TRUE(
      alice.stream
          .Send(MeldOf({Named("A", "♥"), Named("J", "♥"), Named("K", "♥"), Named("Q", "♥")}))
          .ok());
  auto hearts = mirror->meld(1, {cards::Card{cards::Suit::Hearts, cards::Rank::Ace},
                                 cards::Card{cards::Suit::Hearts, cards::Rank::Jack},
                                 cards::Card{cards::Suit::Hearts, cards::Rank::King},
                                 cards::Card{cards::Suit::Hearts, cards::Rank::Queen}});
  ASSERT_TRUE(hearts.ok()) << hearts.status();
  mirror.emplace(*hearts);
  auto after_hearts = ReceiveRummy(bob.stream, "gameState");
  ASSERT_TRUE(after_hearts.has_value());
  ExpectBoard(after_hearts->as_gameState_or_null()->view, bob.player_id, *mirror);
  // Laid low to high, whatever order it was named in.
  EXPECT_EQ(Faces(after_hearts->as_gameState_or_null()->view.melds.at(0).cards),
            (std::vector<std::string>{"J♥", "Q♥", "K♥", "A♥"}));
  ASSERT_TRUE(ReceiveRummy(alice.stream, "gameState").has_value());

  ASSERT_TRUE(
      alice.stream
          .Send(MeldOf({Named("J", "♣"), Named("Q", "♣"), Named("K", "♣"), Named("A", "♣")}))
          .ok());
  auto clubs = mirror->meld(1, {cards::Card{cards::Suit::Clubs, cards::Rank::Jack},
                                cards::Card{cards::Suit::Clubs, cards::Rank::Queen},
                                cards::Card{cards::Suit::Clubs, cards::Rank::King},
                                cards::Card{cards::Suit::Clubs, cards::Rank::Ace}});
  ASSERT_TRUE(clubs.ok()) << clubs.status();
  mirror.emplace(*clubs);
  // The deal passes to alice, the seat after bob.
  ASSERT_NO_FATAL_FAILURE(ExpectDealEnding({&alice, &bob}, *mirror, alice.player_id));
  // Bob held A♠ A♦ and five tens' worth of court cards: 1+1+50.
  EXPECT_EQ(mirror->winnerPoints(), 52);
  EXPECT_EQ(metrics_->CounterTotal("rummy_commands", {{"command", "meld"}}), 2);
  // A deal's end is the view's lastDeal; gameEnded is the table's.
  EXPECT_EQ(metrics_->CounterTotal("rummy_events", {{"event", "gameEnded"}}), 0);
}

// A stock draw is private: the drawer sees the card, everyone else a
// count and a move that names nothing.
TEST_F(RummyGameFixture, AStockDrawShowsTheCardOnlyToItsDrawer) {
  auto table = SeatedRummyTable();
  ASSERT_TRUE(table.has_value());
  auto& alice = table->alice;
  auto& bob = table->bob;
  ASSERT_TRUE(ReceiveRummy(alice.stream, "gameState").has_value());
  ASSERT_TRUE(ReceiveRummy(bob.stream, "gameState").has_value());

  ASSERT_TRUE(alice.stream.Send(DrawStock()).ok());
  auto hers = ReceiveRummy(alice.stream, "gameState");
  auto his = ReceiveRummy(bob.stream, "gameState");
  ASSERT_TRUE(hers.has_value() && his.has_value());
  const RummyView& her_view = hers->as_gameState_or_null()->view;
  const RummyView& his_view = his->as_gameState_or_null()->view;
  // Alice sits in seat 1, after the dealer.
  EXPECT_EQ(Face(her_view.players[1].hand.back()), "J♣");
  EXPECT_TRUE(his_view.players[1].hand.empty());
  EXPECT_EQ(his_view.players[1].handCount, 8);
  EXPECT_EQ(his_view.stockCount, 36);
  ASSERT_TRUE(his_view.lastMove.has_value());
  EXPECT_EQ(his_view.lastMove->move, "drawStock");
  EXPECT_TRUE(his_view.lastMove->cards.empty());
  EXPECT_FALSE(his_view.lastMove->meldIndex.has_value());
  EXPECT_EQ(his_view.stage.value_or(""), "play");
  // Nobody's turn changed: a draw opens the rest of the same turn.
  ExpectNoEvent(bob.stream);
}

// The turn's order is the engine's, and its refusals are rules refusals,
// in band: nothing on the table moves.
TEST_F(RummyGameFixture, OutOfTurnOrOutOfOrderIsRefusedInBand) {
  auto table = SeatedRummyTable();
  ASSERT_TRUE(table.has_value());
  auto& alice = table->alice;
  auto& bob = table->bob;
  ASSERT_TRUE(ReceiveRummy(alice.stream, "gameState").has_value());
  ASSERT_TRUE(ReceiveRummy(bob.stream, "gameState").has_value());

  ASSERT_TRUE(bob.stream.Send(DrawStock()).ok());
  auto refused = ReceiveCase(bob.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "not your turn");
  // Off turn, the turn is the answer, whatever card the move names: a
  // card that is not his is no reason to tell him he is out of sync.
  ASSERT_TRUE(bob.stream.Send(Discard(Named("A", "♥"))).ok());
  refused = ReceiveCase(bob.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "not your turn");

  ASSERT_TRUE(alice.stream.Send(Discard(Named("A", "♥"))).ok());
  refused = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "draw a card first");

  ASSERT_TRUE(alice.stream.Send(DrawStock()).ok());
  ASSERT_TRUE(ReceiveRummy(alice.stream, "gameState").has_value());
  ASSERT_TRUE(ReceiveRummy(bob.stream, "gameState").has_value());
  ASSERT_TRUE(alice.stream.Send(DrawDiscard()).ok());
  refused = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "you have already drawn");

  // Two cards are no meld; a king does not fit the meld nobody laid.
  ASSERT_TRUE(alice.stream.Send(MeldOf({Named("A", "♥"), Named("K", "♥")})).ok());
  refused = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "those cards are not a set or a run");
  ASSERT_TRUE(alice.stream.Send(LayOff(Named("K", "♥"), 0)).ok());
  refused = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "no such meld");

  EXPECT_EQ(metrics_->CounterTotal("hub_rejections", {{"kind", "rules"}}), 6);
  EXPECT_EQ(metrics_->CounterTotal("hub_rejections", {{"kind", "state"}}), 0);
  ExpectNoEvent(bob.stream);
}

// The point of naming cards (#1505): a card the hand does not hold is a
// client out of sync with its view, refused with the card it named; a
// spelling no card has is a client sending nonsense. Different spikes.
TEST_F(RummyGameFixture, ACardTheHandDoesNotHoldIsRefusedAsStaleAndNamed) {
  auto table = SeatedRummyTable();
  ASSERT_TRUE(table.has_value());
  auto& alice = table->alice;
  auto& bob = table->bob;
  ASSERT_TRUE(ReceiveRummy(alice.stream, "gameState").has_value());
  ASSERT_TRUE(ReceiveRummy(bob.stream, "gameState").has_value());
  ASSERT_TRUE(alice.stream.Send(DrawStock()).ok());
  ASSERT_TRUE(ReceiveRummy(alice.stream, "gameState").has_value());
  ASSERT_TRUE(ReceiveRummy(bob.stream, "gameState").has_value());

  // A♠ is bob's.
  ASSERT_TRUE(alice.stream.Send(Discard(Named("A", "♠"))).ok());
  auto refused = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "not in your hand: A♠");
  ASSERT_TRUE(alice.stream.Send(MeldOf({Named("A", "♥"), Named("K", "♥"), Named("Q", "♠")})).ok());
  refused = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "not in your hand: Q♠");
  // CardMapper's letters are not the wire's glyphs.
  ASSERT_TRUE(alice.stream.Send(Discard(Named("A", "S"))).ok());
  refused = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "no such card: AS");
  ASSERT_TRUE(alice.stream.Send(MeldOf({Named("A", "♥"), Named("A", "♥"), Named("A", "♣")})).ok());
  refused = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "named twice: A♥");
  EXPECT_EQ(metrics_->CounterTotal("hub_rejections", {{"kind", "state"}}), 2);
  EXPECT_EQ(metrics_->CounterTotal("hub_rejections", {{"kind", "invalid"}}), 2);
  EXPECT_EQ(metrics_->CounterTotal("hub_rejections", {{"kind", "rules"}}), 0);
  ExpectNoEvent(bob.stream);

  // The card she does hold goes, and it is the card she named.
  ASSERT_TRUE(alice.stream.Send(Discard(Named("J", "♣"))).ok());
  auto thrown = ReceiveRummy(bob.stream, "gameState");
  ASSERT_TRUE(thrown.has_value());
  const RummyView& view = thrown->as_gameState_or_null()->view;
  ASSERT_TRUE(view.discardTop.has_value());
  EXPECT_EQ(Face(*view.discardTop), "J♣");
  EXPECT_EQ(view.currentPlayerId.value_or(""), bob.player_id);
  EXPECT_EQ(view.stage.value_or(""), "draw");
}

// A draw, a meld and a lay-off are all the same seat's turn: the table
// hears views and no turnChanged until the discard hands it on.
TEST_F(RummyGameFixture, ADrawAndAMeldLeaveTheTurnUnannounced) {
  auto table = SeatedRummyTable();
  ASSERT_TRUE(table.has_value());
  auto& alice = table->alice;
  auto& bob = table->bob;
  ASSERT_TRUE(ReceiveRummy(alice.stream, "gameState").has_value());
  ASSERT_TRUE(ReceiveRummy(bob.stream, "gameState").has_value());
  ASSERT_TRUE(ReceiveCase(bob.stream, "roomState").has_value());

  ASSERT_TRUE(alice.stream.Send(DrawDiscard()).ok());
  ASSERT_TRUE(ReceiveRummy(bob.stream, "gameState").has_value());
  ExpectNoEvent(bob.stream);
  ASSERT_TRUE(alice.stream.Send(MeldOf({Named("A", "♥"), Named("K", "♥"), Named("Q", "♥")})).ok());
  ASSERT_TRUE(ReceiveRummy(bob.stream, "gameState").has_value());
  ExpectNoEvent(bob.stream);
  ASSERT_TRUE(alice.stream.Send(LayOff(Named("J", "♥"), 0)).ok());
  ASSERT_TRUE(ReceiveRummy(bob.stream, "gameState").has_value());
  ExpectNoEvent(bob.stream);
  // Its twin: the discard does announce.
  ASSERT_TRUE(alice.stream.Send(Discard(Named("A", "♣"))).ok());
  ASSERT_TRUE(ReceiveRummy(bob.stream, "gameState").has_value());
  auto next = bob.stream.Receive(kReceiveBudget);
  ASSERT_TRUE(next.ok() && next->has_value());
  ASSERT_NE((*next)->as_rummy_or_null(), nullptr);
  EXPECT_EQ((*next)->as_rummy_or_null()->update.case_name(), std::string("turnChanged"));
}

// Taken from the discard, a card is seen taken by the whole table, and
// it cannot go straight back.
TEST_F(RummyGameFixture, TheCardTakenFromTheDiscardIsPublicAndCannotGoStraightBack) {
  auto table = SeatedRummyTable();
  ASSERT_TRUE(table.has_value());
  auto& alice = table->alice;
  auto& bob = table->bob;
  ASSERT_TRUE(ReceiveRummy(alice.stream, "gameState").has_value());
  ASSERT_TRUE(ReceiveRummy(bob.stream, "gameState").has_value());

  ASSERT_TRUE(alice.stream.Send(DrawDiscard()).ok());
  ASSERT_TRUE(ReceiveRummy(alice.stream, "gameState").has_value());
  auto his = ReceiveRummy(bob.stream, "gameState");
  ASSERT_TRUE(his.has_value());
  const RummyView& view = his->as_gameState_or_null()->view;
  ASSERT_TRUE(view.takenDiscard.has_value());
  EXPECT_EQ(Face(*view.takenDiscard), "J♦");
  EXPECT_EQ(view.lastMove->move, "drawDiscard");
  EXPECT_EQ(Faces(view.lastMove->cards), std::vector<std::string>{"J♦"});
  EXPECT_FALSE(view.discardTop.has_value());
  EXPECT_EQ(view.discardCount, 0);

  ASSERT_TRUE(alice.stream.Send(Discard(Named("J", "♦"))).ok());
  auto refused = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason,
            "you took that card from the discard pile this turn");
  ExpectNoEvent(bob.stream);
}

// Whole games, played by the policy on both sides of the wire, at every
// table size: melds, lay-offs onto anyone's meld, discard draws, and the
// end — every view checked against the engine on the way.
class SeededRummyFixture : public RummyGameFixture, public ::testing::WithParamInterface<int> {
 protected:
  std::shared_ptr<cards::Dealer> MakeDealer() override { return std::make_shared<SeededDealer>(7); }
};

TEST_P(SeededRummyFixture, AWholeGameAgreesWithTheEngine) {
  const int seats_at_table = GetParam();
  auto table = MultiSeatRummyTable(seats_at_table);
  ASSERT_TRUE(table.has_value());
  std::vector<Seat*> seats;
  for (Seat& seat : table->seats) seats.push_back(&seat);
  std::optional<rummy::GameState> mirror(MirrorDeal(table->game_id, table->ids()));
  ASSERT_NO_FATAL_FAILURE(ExpectViews(seats, *mirror, "the deal"));

  PlayedOut played;
  ASSERT_NO_FATAL_FAILURE(PlayToEnd(seats, mirror, played));
  ASSERT_NO_FATAL_FAILURE(ExpectDealEnding(seats, *mirror, table->ids()[1]));
  // The policy's game touched every move, so each was checked above.
  EXPECT_GT(played.melds, 0);
  EXPECT_GT(played.lay_offs, 0);
  EXPECT_GT(played.foreign_lay_offs, 0);
  EXPECT_GT(played.discard_draws, 0);
  EXPECT_GT(played.stock_draws, 0);
  EXPECT_EQ(metrics_->CounterTotal("rummy_commands", {{"command", "layOff"}}), played.lay_offs);
  EXPECT_EQ(metrics_->CounterTotal("rummy_commands", {{"command", "meld"}}), played.melds);
}

INSTANTIATE_TEST_SUITE_P(TwoThreeAndFourSeats, SeededRummyFixture, ::testing::Values(2, 3, 4));

// A stock that ran out is the discard pile turned over. The pristine
// deck at two seats leaves 37 in the stock: alice and bob each draw and
// throw back what they drew until it is gone, and the next draw refills
// it from under the top card, the table seeing the counts move.
TEST_F(RummyGameFixture, AnEmptyStockIsRefilledFromTheDiscardPile) {
  auto table = SeatedRummyTable();
  ASSERT_TRUE(table.has_value());
  // In seat order: bob deals, alice opens.
  std::vector<Seat*> seats{&table->bob, &table->alice};
  std::optional<rummy::GameState> mirror(
      MirrorDeal(table->game_id, {table->bob.player_id, table->alice.player_id}));
  ASSERT_NO_FATAL_FAILURE(ExpectViews(seats, *mirror, "the deal"));
  while (!mirror->getStock().empty()) {
    const int seat = mirror->getWhoseTurn();
    Seat& mover = *seats.at(seat);
    ASSERT_TRUE(mover.stream.Send(DrawStock()).ok());
    auto drew = mirror->drawStock(seat);
    ASSERT_TRUE(drew.ok());
    mirror.emplace(*drew);
    ASSERT_NO_FATAL_FAILURE(ExpectViews(seats, *mirror, "drawStock"));
    const cards::Card drawn = mirror->getPlayer(seat).hand.back();
    ASSERT_TRUE(mover.stream.Send(Discard(Wire(drawn))).ok());
    auto threw = mirror->discard(seat, drawn);
    ASSERT_TRUE(threw.ok()) << threw.status();
    mirror.emplace(*threw);
    ASSERT_NO_FATAL_FAILURE(ExpectViews(seats, *mirror, "discard"));
  }
  ASSERT_EQ(mirror->getDiscard().size(), 38u);
  const cards::Card top = mirror->getDiscard().back();
  const cards::Card bottom = mirror->getDiscard().front();
  const int seat = mirror->getWhoseTurn();
  // Drawn cards went straight back, which a discard draw would not allow;
  // a stock draw remembers nothing.
  ASSERT_TRUE(seats.at(seat)->stream.Send(DrawStock()).ok());
  auto refilled = mirror->drawStock(seat);
  ASSERT_TRUE(refilled.ok());
  mirror.emplace(*refilled);
  EXPECT_EQ(mirror->getPlayer(seat).hand.back(), bottom);
  for (Seat* hearer : seats) {
    auto update = ReceiveRummy(hearer->stream, "gameState");
    ASSERT_TRUE(update.has_value());
    const RummyView& view = update->as_gameState_or_null()->view;
    ExpectBoard(view, hearer->player_id, *mirror);
    EXPECT_EQ(view.stockCount, 36);
    EXPECT_EQ(view.discardCount, 1);
    EXPECT_EQ(Face(*view.discardTop), Face(Wire(top)));
  }
}

// Two seats, one leaves mid-deal: the deal is abandoned and the table,
// below two seats, closes. The last deal names no winner and scores
// nothing; the table's end is its standings.
TEST_F(RummyGameFixture, LeavingMidGameAbandonsItWithNoWinner) {
  auto table = SeatedRummyTable();
  ASSERT_TRUE(table.has_value());
  auto& alice = table->alice;
  auto& bob = table->bob;
  ASSERT_TRUE(ReceiveRummy(alice.stream, "gameState").has_value());
  ASSERT_TRUE(ReceiveRummy(bob.stream, "gameState").has_value());

  ASSERT_TRUE(
      alice.stream.Send(Rummy(RummyMove::FromLeavegame(moonbase::games::LeaveGame{}))).ok());
  ASSERT_TRUE(ReceiveRummy(alice.stream, "gameLeft").has_value());
  auto final_view = ReceiveRummy(bob.stream, "gameState");
  ASSERT_TRUE(final_view.has_value());
  {
    const RummyView& view = final_view->as_gameState_or_null()->view;
    EXPECT_EQ(view.phase, "ended");
    EXPECT_FALSE(view.currentPlayerId.has_value());
    EXPECT_FALSE(view.stage.has_value());
    ASSERT_EQ(view.players.size(), 1u);
    EXPECT_EQ(view.players[0].playerId, bob.player_id);
    EXPECT_EQ(view.players[0].hand.size(), 7u);
    ASSERT_TRUE(view.lastDeal.has_value());
    EXPECT_FALSE(view.lastDeal->winner.has_value());
    EXPECT_EQ(view.lastDeal->points, 0);
    EXPECT_FALSE(view.choosing.has_value());
  }
  auto ended = ReceiveRummy(bob.stream, "gameEnded");
  ASSERT_TRUE(ended.has_value());
  EXPECT_EQ(ended->as_gameEnded_or_null()->dealsPlayed, 1);
  ASSERT_EQ(ended->as_gameEnded_or_null()->standings.size(), 1u);
  EXPECT_EQ(ended->as_gameEnded_or_null()->standings[0].playerId, bob.player_id);
  EXPECT_EQ(ended->as_gameEnded_or_null()->standings[0].handsWon, 0);
  auto room = ReceiveCase(bob.stream, "roomState");
  ASSERT_TRUE(room.has_value());
  EXPECT_TRUE(room->as_roomState_or_null()->games.empty());
  for (const auto& player : room->as_roomState_or_null()->players) {
    EXPECT_EQ(player.gamesPlayed, player.playerId == bob.player_id ? 1 : 0);
    EXPECT_EQ(player.gamesWon, 0);
  }
}

// Three seats, the one on turn leaves mid-turn: the next seat draws, and
// the table hears the turn move.
TEST_F(RummyGameFixture, ALeaveOnTurnPassesTheDrawToTheNextSeat) {
  auto table = MultiSeatRummyTable(3);
  ASSERT_TRUE(table.has_value());
  std::vector<Seat*> seats;
  for (Seat& seat : table->seats) seats.push_back(&seat);
  for (Seat* seat : seats) ASSERT_TRUE(ReceiveRummy(seat->stream, "gameState").has_value());

  // Seat 1 opens, after the dealer; leaving mid-turn hands the draw to
  // seat 2.
  Seat& opener = table->seats[1];
  ASSERT_TRUE(opener.stream.Send(DrawStock()).ok());
  for (Seat* seat : seats) ASSERT_TRUE(ReceiveRummy(seat->stream, "gameState").has_value());
  ASSERT_TRUE(
      opener.stream.Send(Rummy(RummyMove::FromLeavegame(moonbase::games::LeaveGame{}))).ok());
  for (Seat* seat : {&table->seats[0], &table->seats[2]}) {
    auto view = ReceiveRummy(seat->stream, "gameState");
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->as_gameState_or_null()->view.players.size(), 2u);
    EXPECT_EQ(view->as_gameState_or_null()->view.phase, "playing");
    EXPECT_EQ(view->as_gameState_or_null()->view.currentPlayerId.value_or(""),
              table->seats[2].player_id);
    EXPECT_EQ(view->as_gameState_or_null()->view.stage.value_or(""), "draw");
    auto turn = ReceiveRummy(seat->stream, "turnChanged");
    ASSERT_TRUE(turn.has_value());
    EXPECT_EQ(turn->as_turnChanged_or_null()->playerId, table->seats[2].player_id);
  }
  ASSERT_TRUE(table->seats[2].stream.Send(DrawStock()).ok());
  ASSERT_TRUE(ReceiveRummy(table->seats[2].stream, "gameState").has_value());
}

// Tables of every game share a room; the room says which is which, and a
// move in one game's vocabulary is refused at another's table.
TEST_F(RummyGameFixture, TheRoomListsRummyTablesAndOtherGamesMovesAreRefused) {
  auto table = SeatedRummyTable();
  ASSERT_TRUE(table.has_value());
  auto& alice = table->alice;
  auto& bob = table->bob;
  ASSERT_TRUE(ReceiveRummy(alice.stream, "gameState").has_value());
  ASSERT_TRUE(ReceiveRummy(bob.stream, "gameState").has_value());
  auto room = AwaitRoomState(
      alice.stream,
      [&](const moonbase::games::RoomState& state) {
        return state.games.size() == 1 && state.games[0].status == "playing";
      },
      "the room listing the started rummy table");
  ASSERT_TRUE(room.has_value());
  EXPECT_EQ(room->games[0].game, "rummy");
  EXPECT_EQ(room->games[0].playerCount, 2);
  EXPECT_EQ(TableOf(*room, alice.player_id), AtTable("rummy", table->game_id));

  ASSERT_TRUE(alice.stream.Send(Castle(CastleMove::FromPickup(moonbase::games::PickUp{}))).ok());
  auto refused = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "that table plays rummy");
  ASSERT_TRUE(
      alice.stream.Send(Move(moonbase::games::GolfMove::FromKnock(moonbase::games::Knock{}))).ok());
  refused = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "that table plays rummy");

  // And the other way: a rummy move at a golf table, a rummy join of one.
  auto golf_table = SeatedTable();
  ASSERT_TRUE(golf_table.has_value());
  ASSERT_TRUE(golf_table->alice.stream.Send(DrawStock()).ok());
  refused = ReceiveCase(golf_table->alice.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "that table plays golf");
  auto third = OpenSeat();
  ASSERT_TRUE(third.has_value());
  ASSERT_TRUE(ReceiveCase(third->stream, "sessionReady").has_value());
  moonbase::games::JoinRoom join_room;
  join_room.roomId = golf_table->room_id;
  ASSERT_TRUE(third->stream.Send(GameCommands::FromJoinroom(join_room)).ok());
  ASSERT_TRUE(ReceiveCase(third->stream, "roomState").has_value());
  moonbase::games::JoinGame join_game;
  join_game.gameId = golf_table->game_id;
  ASSERT_TRUE(third->stream.Send(Rummy(RummyMove::FromJoingame(join_game))).ok());
  refused = ReceiveCase(third->stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "that table plays golf");
}

// A browser close parks the seat; the resume token brings back the
// seat's own view in rummy's envelope, redacted for it, mid-turn.
TEST_F(RummyGameFixture, AResumedRummySeatGetsItsOwnViewBack) {
  auto table = SeatedRummyTable();
  ASSERT_TRUE(table.has_value());
  ASSERT_TRUE(ReceiveRummy(table->alice.stream, "gameState").has_value());
  ASSERT_TRUE(ReceiveRummy(table->bob.stream, "gameState").has_value());
  ASSERT_TRUE(table->alice.stream.Send(DrawStock()).ok());
  ASSERT_TRUE(ReceiveRummy(table->alice.stream, "gameState").has_value());
  ASSERT_TRUE(ReceiveRummy(table->bob.stream, "gameState").has_value());

  table->alice.stream.Close();
  ASSERT_TRUE(ReceiveCase(table->bob.stream, "roomState").has_value());
  auto resumed = OpenSeat(table->alice.resume_token);
  ASSERT_TRUE(resumed.has_value());
  auto ready = ReceiveCase(resumed->stream, "sessionReady");
  ASSERT_TRUE(ready.has_value());
  EXPECT_TRUE(ready->as_sessionReady_or_null()->resumed);
  auto joined = ReceiveRummy(resumed->stream, "gameJoined");
  ASSERT_TRUE(joined.has_value());
  const RummyView& view = joined->as_gameJoined_or_null()->view;
  EXPECT_EQ(view.phase, "playing");
  EXPECT_EQ(view.currentPlayerId.value_or(""), table->alice.player_id);
  EXPECT_EQ(view.stage.value_or(""), "play");
  ASSERT_EQ(view.players.size(), 2u);
  EXPECT_EQ(view.players[1].hand.size(), 8u);
  EXPECT_EQ(Face(view.players[1].hand.back()), "J♣");
  EXPECT_TRUE(view.players[0].hand.empty());
  EXPECT_EQ(view.players[0].handCount, 7);
  // The turn is still hers to finish.
  ASSERT_TRUE(resumed->stream.Send(Discard(Named("J", "♣"))).ok());
  auto thrown = ReceiveRummy(table->bob.stream, "gameState");
  ASSERT_TRUE(thrown.has_value());
  EXPECT_EQ(thrown->as_gameState_or_null()->view.currentPlayerId.value_or(""),
            table->bob.player_id);
}

GameCommands Choose(const std::string& variant) {
  moonbase::games::RummyChooseVariant choice;
  choice.variant = variant;
  return Rummy(RummyMove::FromChoosevariant(choice));
}

// The deal passes to the seat after the dealer, and that seat alone picks
// what comes next; the seat after it opens. Standings and the room's
// stats carry across deals.
TEST_F(RummyGameFixture, TheNextDealIsTheNextDealersChoice) {
  auto table = SeatedRummyTable();
  ASSERT_TRUE(table.has_value());
  auto& alice = table->alice;
  auto& bob = table->bob;
  for (auto* seat : {&alice, &bob}) {
    ASSERT_TRUE(ReceiveRummy(seat->stream, "gameState").has_value());
  }
  ASSERT_TRUE(alice.stream.Send(DrawStock()).ok());
  ASSERT_TRUE(
      alice.stream
          .Send(MeldOf({Named("J", "♥"), Named("Q", "♥"), Named("K", "♥"), Named("A", "♥")}))
          .ok());
  ASSERT_TRUE(
      alice.stream
          .Send(MeldOf({Named("J", "♣"), Named("Q", "♣"), Named("K", "♣"), Named("A", "♣")}))
          .ok());
  // The quickest win (see the first test): every chair reads to the deal's
  // end, the choosing view and the room's listing.
  for (auto* seat : {&alice, &bob}) {
    ASSERT_TRUE(AwaitRummyView(
                    seat->stream, [](const RummyView& view) { return view.phase == "choosing"; },
                    "the deal's end")
                    .has_value());
    ASSERT_TRUE(ReceiveCase(seat->stream, "roomState").has_value());
  }
  // Between deals nobody is on turn, and that is no turn to announce.
  ExpectNoEvent(bob.stream);

  // Bob dealt the first; alice deals the second.
  ASSERT_TRUE(bob.stream.Send(Choose("7-card")).ok());
  auto refused = ReceiveCase(bob.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "the dealer chooses");
  // Between deals there is nothing to draw.
  ASSERT_TRUE(alice.stream.Send(DrawStock()).ok());
  refused = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "no deal in play");

  ASSERT_TRUE(alice.stream.Send(Choose("7-card")).ok());
  for (auto* seat : {&alice, &bob}) {
    auto dealt = ReceiveRummy(seat->stream, "gameState");
    ASSERT_TRUE(dealt.has_value());
    const RummyView& view = dealt->as_gameState_or_null()->view;
    EXPECT_EQ(view.phase, "playing");
    EXPECT_EQ(view.dealNumber, 2);
    EXPECT_EQ(view.currentPlayerId.value_or(""), bob.player_id);
    EXPECT_FALSE(view.lastDeal.has_value());
    EXPECT_FALSE(view.choosing.has_value());
    EXPECT_TRUE(view.melds.empty());
    ASSERT_EQ(view.standings.size(), 2u);
    EXPECT_EQ(view.standings[0].handsWon, 0);
    EXPECT_EQ(view.standings[1].handsWon, 1);
    for (const auto& player : view.players) EXPECT_EQ(player.handCount, 7);
    auto room = ReceiveCase(seat->stream, "roomState");
    ASSERT_TRUE(room.has_value());
    EXPECT_EQ(room->as_roomState_or_null()->games[0].status, "playing");
    auto turn = ReceiveRummy(seat->stream, "turnChanged");
    ASSERT_TRUE(turn.has_value());
    EXPECT_EQ(turn->as_turnChanged_or_null()->playerId, bob.player_id);
  }
  // Mid-deal there is nothing to choose.
  ASSERT_TRUE(alice.stream.Send(Choose("7-card")).ok());
  refused = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "not between deals");
  EXPECT_EQ(metrics_->CounterTotal("rummy_commands", {{"command", "chooseVariant"}}), 4);

  // Bob leaving mid-deal closes the table; its standings keep the hand
  // alice won, and both deals count.
  ASSERT_TRUE(bob.stream.Send(Rummy(RummyMove::FromLeavegame(moonbase::games::LeaveGame{}))).ok());
  auto ended = ReceiveRummy(alice.stream, "gameEnded");
  ASSERT_TRUE(ended.has_value());
  EXPECT_EQ(ended->as_gameEnded_or_null()->dealsPlayed, 2);
  ASSERT_EQ(ended->as_gameEnded_or_null()->standings.size(), 1u);
  EXPECT_EQ(ended->as_gameEnded_or_null()->standings[0].playerId, alice.player_id);
  EXPECT_EQ(ended->as_gameEnded_or_null()->standings[0].handsWon, 1);
}

// A dealer the room shows as gone does not stall the table: any seat may
// deal in their place, and the deal still opens after the dealer's seat.
TEST_F(RummyGameFixture, AnAwayDealerLetsAnySeatDeal) {
  auto table = ChoosingRummyTable(3);
  ASSERT_TRUE(table.has_value());
  Seat& dealer = table->seats[0];
  ASSERT_TRUE(table->seats[2].stream.Send(Choose("7-card")).ok());
  auto refused = ReceiveCase(table->seats[2].stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "the dealer chooses");

  dealer.stream.Close();
  auto away = AwaitRoomState(
      table->seats[2].stream,
      [&](const moonbase::games::RoomState& state) {
        for (const auto& player : state.players) {
          if (player.playerId == dealer.player_id) return !player.connected;
        }
        return false;
      },
      "the dealer shown away");
  ASSERT_TRUE(away.has_value());
  ASSERT_TRUE(table->seats[2].stream.Send(Choose("7-card")).ok());
  auto dealt = AwaitRummyView(
      table->seats[2].stream, [](const RummyView& view) { return view.phase == "playing"; },
      "the deal");
  ASSERT_TRUE(dealt.has_value());
  EXPECT_EQ(dealt->currentPlayerId.value_or(""), table->seats[1].player_id);
  EXPECT_EQ(dealt->players.size(), 3u);
}

// The dealer's chair follows the table: a dealer who leaves between deals
// passes it to the seat after them, and the table deals on while two
// remain; the last but one leaving closes it with its standings.
TEST_F(RummyGameFixture, ADealerLeavingBetweenDealsPassesTheChair) {
  auto table = ChoosingRummyTable(3);
  ASSERT_TRUE(table.has_value());
  ASSERT_TRUE(table->seats[0]
                  .stream.Send(Rummy(RummyMove::FromLeavegame(moonbase::games::LeaveGame{})))
                  .ok());
  for (Seat* seat : {&table->seats[1], &table->seats[2]}) {
    auto view = AwaitRummyView(
        seat->stream, [](const RummyView& view) { return view.players.size() == 2; },
        "the table without its dealer");
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->phase, "choosing");
    ASSERT_TRUE(view->choosing.has_value());
    EXPECT_EQ(view->choosing->dealer, table->seats[1].player_id);
    EXPECT_EQ(view->standings.size(), 2u);
  }

  ASSERT_TRUE(table->seats[1]
                  .stream.Send(Rummy(RummyMove::FromLeavegame(moonbase::games::LeaveGame{})))
                  .ok());
  auto closed = AwaitRummyView(
      table->seats[2].stream, [](const RummyView& view) { return view.phase == "ended"; },
      "the table closed");
  ASSERT_TRUE(closed.has_value());
  EXPECT_FALSE(closed->choosing.has_value());
  EXPECT_FALSE(closed->lastDeal.has_value());
  auto ended = ReceiveRummy(table->seats[2].stream, "gameEnded");
  ASSERT_TRUE(ended.has_value());
  EXPECT_EQ(ended->as_gameEnded_or_null()->dealsPlayed, 0);
  ASSERT_EQ(ended->as_gameEnded_or_null()->standings.size(), 1u);
  EXPECT_EQ(ended->as_gameEnded_or_null()->standings[0].playerId, table->seats[2].player_id);
  // Nothing was dealt, so nothing was played: the room's stats stand.
  auto room = AwaitRoomState(
      table->seats[2].stream,
      [](const moonbase::games::RoomState& state) { return state.games.empty(); },
      "the table gone from the room");
  ASSERT_TRUE(room.has_value());
  for (const auto& player : room->players) EXPECT_EQ(player.gamesPlayed, 0);
}

// Before the deal a table is a roster: no cards anywhere, no turn.
TEST_F(RummyGameFixture, AWaitingTableIsARosterWithNoCards) {
  auto room = SeatedRoom(1);
  ASSERT_TRUE(room.has_value());
  Seat& host = room->seats.front();
  ASSERT_TRUE(
      host.stream.Send(Rummy(RummyMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
  auto joined = ReceiveRummy(host.stream, "gameJoined");
  ASSERT_TRUE(joined.has_value());
  const RummyView& view = joined->as_gameJoined_or_null()->view;
  EXPECT_EQ(view.phase, "waiting");
  EXPECT_FALSE(view.currentPlayerId.has_value());
  EXPECT_FALSE(view.stage.has_value());
  EXPECT_EQ(view.stockCount, 0);
  EXPECT_FALSE(view.canDrawStock);
  EXPECT_FALSE(view.discardTop.has_value());
  EXPECT_TRUE(view.melds.empty());
  ASSERT_EQ(view.players.size(), 1u);
  EXPECT_EQ(view.players[0].playerId, host.player_id);
  EXPECT_EQ(view.players[0].handCount, 0);
  // One seat is no game.
  ASSERT_TRUE(host.stream.Send(Rummy(RummyMove::FromStartgame(moonbase::games::StartGame{}))).ok());
  auto refused = ReceiveCase(host.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "need at least 2 players to start");
  ASSERT_TRUE(host.stream.Send(DrawStock()).ok());
  refused = ReceiveCase(host.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "game not started");
}

}  // namespace
}  // namespace games_hub
