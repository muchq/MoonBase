// Rummy on the room stream (#245): the third game on the hub, end to end
// through the generated client.
//
// Two kinds of deal. The NoShuffleDealer deals the pristine deck one card
// a seat from the back, so every card is known: at two seats alice holds
// A♠ A♦ K♠ K♦ Q♠ Q♦ J♠ J♦ 10♠ 10♦, bob the hearts and clubs of the same
// ranks, 9♠ is turned up and 9♥ tops the stock. The SeededDealer shuffles
// with a fixed seed for whole games, which a local engine mirror plays
// alongside the hub: every turn the mirror picks a move by a simple
// policy, the same command goes to the hub, and every seat's view must
// agree with the mirror.

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
  // The same deal the hub made, as an engine value.
  rummy::GameState MirrorDeal(const std::string& game_id, const std::vector<std::string>& ids) {
    auto dealer = MakeDealer();
    std::deque<cards::Card> deck = dealer->DealNewUnshuffledDeck();
    dealer->ShuffleDeck(deck);
    auto state = rummy::dealRummyGame(game_id, ids, std::move(deck));
    EXPECT_TRUE(state.ok()) << state.status();
    return *state;
  }

  // The board, as one chair sees it, against the mirror: every public
  // fact, the last move, and hand faces only for the viewer's own seat.
  void ExpectBoard(const RummyView& seen, const std::string& viewer,
                   const rummy::GameState& mirror) {
    const bool ended = mirror.isOver();
    EXPECT_EQ(seen.phase, ended ? "ended" : "playing");
    if (ended) {
      EXPECT_FALSE(seen.currentPlayerId.has_value());
      EXPECT_FALSE(seen.stage.has_value());
      EXPECT_FALSE(seen.canDrawStock);
    } else {
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
    ASSERT_EQ(seen.takenDiscard.has_value(), !ended && mirror.getTakenDiscard().has_value());
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

  // The end, from every chair: final views with every hand face up and
  // nobody to play, the result, then the room's stats crediting the seat
  // that went out and nobody else.
  void ExpectEnding(const std::vector<Seat*>& seats, const rummy::GameState& mirror) {
    ASSERT_EQ(mirror.getPhase(), rummy::Phase::Over);
    const std::string winner = mirror.winner().value_or("");
    ASSERT_FALSE(winner.empty());
    for (Seat* seat : seats) {
      auto final_view = ReceiveRummy(seat->stream, "gameState");
      ASSERT_TRUE(final_view.has_value());
      ExpectBoard(final_view->as_gameState_or_null()->view, seat->player_id, mirror);
      auto ended = ReceiveRummy(seat->stream, "gameEnded");
      ASSERT_TRUE(ended.has_value());
      const auto& result = *ended->as_gameEnded_or_null();
      EXPECT_EQ(result.winner.value_or(""), winner);
      EXPECT_EQ(result.points, mirror.winnerPoints());
      ASSERT_EQ(result.scores.size(), mirror.getPlayers().size());
      int total = 0;
      for (std::size_t i = 0; i < result.scores.size(); ++i) {
        EXPECT_EQ(result.scores[i].playerId, mirror.getPlayer(static_cast<int>(i)).id);
        EXPECT_EQ(result.scores[i].deadwood, mirror.deadwood(static_cast<int>(i)));
        total += result.scores[i].deadwood;
      }
      EXPECT_EQ(result.points, total);
      auto room = ReceiveCase(seat->stream, "roomState");
      ASSERT_TRUE(room.has_value());
      EXPECT_TRUE(room->as_roomState_or_null()->games.empty());
      for (const auto& player : room->as_roomState_or_null()->players) {
        EXPECT_EQ(player.gamesPlayed, 1);
        EXPECT_EQ(player.gamesWon, player.playerId == winner ? 1 : 0);
        // Rummy's points are the winner's, on a scale golf's running total
        // does not share: they ride gameEnded and leave the total alone.
        EXPECT_EQ(player.totalScore, 0);
        EXPECT_FALSE(player.table.has_value());
      }
    }
  }
};

// The deal from each chair, then the quickest win the pristine deck
// allows: alice takes the turned-up 9♠ and lays down her whole hand as
// two runs — no discard needed to go out.
TEST_F(RummyGameFixture, TheDealIsRedactedPerSeatAndLayingDownEverythingWins) {
  auto table = SeatedRummyTable();
  ASSERT_TRUE(table.has_value());
  auto& alice = table->alice;
  auto& bob = table->bob;
  std::optional<rummy::GameState> mirror(
      MirrorDeal(table->game_id, {alice.player_id, bob.player_id}));

  // The dealt view, in rummy's envelope, from each chair.
  ASSERT_NO_FATAL_FAILURE(ExpectViews({&alice, &bob}, *mirror, "the deal"));

  ASSERT_TRUE(alice.stream.Send(DrawDiscard()).ok());
  auto drew = mirror->drawDiscard(0);
  ASSERT_TRUE(drew.ok());
  mirror.emplace(*drew);
  ASSERT_NO_FATAL_FAILURE(ExpectViews({&alice, &bob}, *mirror, "drawDiscard"));

  ASSERT_TRUE(alice.stream
                  .Send(MeldOf({Named("A", "♠"), Named("9", "♠"), Named("K", "♠"), Named("J", "♠"),
                                Named("10", "♠"), Named("Q", "♠")}))
                  .ok());
  auto spades = mirror->meld(0, {cards::Card{cards::Suit::Spades, cards::Rank::Ace},
                                 cards::Card{cards::Suit::Spades, cards::Rank::Nine},
                                 cards::Card{cards::Suit::Spades, cards::Rank::King},
                                 cards::Card{cards::Suit::Spades, cards::Rank::Jack},
                                 cards::Card{cards::Suit::Spades, cards::Rank::Ten},
                                 cards::Card{cards::Suit::Spades, cards::Rank::Queen}});
  ASSERT_TRUE(spades.ok()) << spades.status();
  mirror.emplace(*spades);
  auto after_spades = ReceiveRummy(bob.stream, "gameState");
  ASSERT_TRUE(after_spades.has_value());
  ExpectBoard(after_spades->as_gameState_or_null()->view, bob.player_id, *mirror);
  // Laid low to high, whatever order it was named in.
  EXPECT_EQ(Faces(after_spades->as_gameState_or_null()->view.melds.at(0).cards),
            (std::vector<std::string>{"9♠", "10♠", "J♠", "Q♠", "K♠", "A♠"}));
  ASSERT_TRUE(ReceiveRummy(alice.stream, "gameState").has_value());

  ASSERT_TRUE(alice.stream
                  .Send(MeldOf({Named("10", "♦"), Named("J", "♦"), Named("Q", "♦"), Named("K", "♦"),
                                Named("A", "♦")}))
                  .ok());
  auto diamonds = mirror->meld(0, {cards::Card{cards::Suit::Diamonds, cards::Rank::Ten},
                                   cards::Card{cards::Suit::Diamonds, cards::Rank::Jack},
                                   cards::Card{cards::Suit::Diamonds, cards::Rank::Queen},
                                   cards::Card{cards::Suit::Diamonds, cards::Rank::King},
                                   cards::Card{cards::Suit::Diamonds, cards::Rank::Ace}});
  ASSERT_TRUE(diamonds.ok()) << diamonds.status();
  mirror.emplace(*diamonds);
  ASSERT_NO_FATAL_FAILURE(ExpectEnding({&alice, &bob}, *mirror));
  // Bob held A♥ A♣ and the hearts and clubs from ten to king: 1+1+80.
  EXPECT_EQ(mirror->winnerPoints(), 82);
  EXPECT_EQ(metrics_->CounterTotal("rummy_commands", {{"command", "meld"}}), 2);
  EXPECT_EQ(metrics_->CounterTotal("rummy_events", {{"event", "gameEnded"}}), 2);
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
  EXPECT_EQ(Face(her_view.players[0].hand.back()), "9♥");
  EXPECT_TRUE(his_view.players[0].hand.empty());
  EXPECT_EQ(his_view.players[0].handCount, 11);
  EXPECT_EQ(his_view.stockCount, 30);
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
  ASSERT_TRUE(bob.stream.Send(Discard(Named("A", "♠"))).ok());
  refused = ReceiveCase(bob.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "not your turn");

  ASSERT_TRUE(alice.stream.Send(Discard(Named("A", "♠"))).ok());
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
  ASSERT_TRUE(alice.stream.Send(MeldOf({Named("A", "♠"), Named("K", "♠")})).ok());
  refused = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "those cards are not a set or a run");
  ASSERT_TRUE(alice.stream.Send(LayOff(Named("K", "♠"), 0)).ok());
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

  // A♥ is bob's.
  ASSERT_TRUE(alice.stream.Send(Discard(Named("A", "♥"))).ok());
  auto refused = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "not in your hand: A♥");
  ASSERT_TRUE(alice.stream.Send(MeldOf({Named("A", "♠"), Named("K", "♠"), Named("Q", "♥")})).ok());
  refused = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "not in your hand: Q♥");
  // CardMapper's letters are not the wire's glyphs.
  ASSERT_TRUE(alice.stream.Send(Discard(Named("A", "S"))).ok());
  refused = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "no such card: AS");
  ASSERT_TRUE(alice.stream.Send(MeldOf({Named("A", "♠"), Named("A", "♠"), Named("A", "♦")})).ok());
  refused = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "named twice: A♠");
  EXPECT_EQ(metrics_->CounterTotal("hub_rejections", {{"kind", "state"}}), 2);
  EXPECT_EQ(metrics_->CounterTotal("hub_rejections", {{"kind", "invalid"}}), 2);
  EXPECT_EQ(metrics_->CounterTotal("hub_rejections", {{"kind", "rules"}}), 0);
  ExpectNoEvent(bob.stream);

  // The card she does hold goes, and it is the card she named.
  ASSERT_TRUE(alice.stream.Send(Discard(Named("J", "♦"))).ok());
  auto thrown = ReceiveRummy(bob.stream, "gameState");
  ASSERT_TRUE(thrown.has_value());
  const RummyView& view = thrown->as_gameState_or_null()->view;
  ASSERT_TRUE(view.discardTop.has_value());
  EXPECT_EQ(Face(*view.discardTop), "J♦");
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
  ASSERT_TRUE(alice.stream.Send(MeldOf({Named("A", "♠"), Named("K", "♠"), Named("Q", "♠")})).ok());
  ASSERT_TRUE(ReceiveRummy(bob.stream, "gameState").has_value());
  ExpectNoEvent(bob.stream);
  ASSERT_TRUE(alice.stream.Send(LayOff(Named("J", "♠"), 0)).ok());
  ASSERT_TRUE(ReceiveRummy(bob.stream, "gameState").has_value());
  ExpectNoEvent(bob.stream);
  // Its twin: the discard does announce.
  ASSERT_TRUE(alice.stream.Send(Discard(Named("A", "♦"))).ok());
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
  EXPECT_EQ(Face(*view.takenDiscard), "9♠");
  EXPECT_EQ(view.lastMove->move, "drawDiscard");
  EXPECT_EQ(Faces(view.lastMove->cards), std::vector<std::string>{"9♠"});
  EXPECT_FALSE(view.discardTop.has_value());
  EXPECT_EQ(view.discardCount, 0);

  ASSERT_TRUE(alice.stream.Send(Discard(Named("9", "♠"))).ok());
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
  std::shared_ptr<cards::Dealer> MakeDealer() override {
    return std::make_shared<SeededDealer>(1234);
  }
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
  ASSERT_NO_FATAL_FAILURE(ExpectEnding(seats, *mirror));
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
// deck at two seats leaves 31 in the stock: alice and bob each draw and
// throw back what they drew until it is gone, and the next draw refills
// it from under the top card, the table seeing the counts move.
TEST_F(RummyGameFixture, AnEmptyStockIsRefilledFromTheDiscardPile) {
  auto table = SeatedRummyTable();
  ASSERT_TRUE(table.has_value());
  std::vector<Seat*> seats{&table->alice, &table->bob};
  std::optional<rummy::GameState> mirror(
      MirrorDeal(table->game_id, {table->alice.player_id, table->bob.player_id}));
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
  ASSERT_EQ(mirror->getDiscard().size(), 32u);
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
    EXPECT_EQ(view.stockCount, 30);
    EXPECT_EQ(view.discardCount, 1);
    EXPECT_EQ(Face(*view.discardTop), Face(Wire(top)));
  }
}

// Two seats, one leaves: the engine abandons rather than plays on. The
// final view is over and the ending names no winner and scores nothing.
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
    EXPECT_EQ(view.players[0].hand.size(), 10u);
  }
  auto ended = ReceiveRummy(bob.stream, "gameEnded");
  ASSERT_TRUE(ended.has_value());
  EXPECT_FALSE(ended->as_gameEnded_or_null()->winner.has_value());
  EXPECT_EQ(ended->as_gameEnded_or_null()->points, 0);
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

  Seat& first = table->seats[0];
  ASSERT_TRUE(first.stream.Send(DrawStock()).ok());
  for (Seat* seat : seats) ASSERT_TRUE(ReceiveRummy(seat->stream, "gameState").has_value());
  ASSERT_TRUE(
      first.stream.Send(Rummy(RummyMove::FromLeavegame(moonbase::games::LeaveGame{}))).ok());
  for (Seat* seat : {&table->seats[1], &table->seats[2]}) {
    auto view = ReceiveRummy(seat->stream, "gameState");
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->as_gameState_or_null()->view.players.size(), 2u);
    EXPECT_EQ(view->as_gameState_or_null()->view.phase, "playing");
    EXPECT_EQ(view->as_gameState_or_null()->view.currentPlayerId.value_or(""),
              table->seats[1].player_id);
    EXPECT_EQ(view->as_gameState_or_null()->view.stage.value_or(""), "draw");
    auto turn = ReceiveRummy(seat->stream, "turnChanged");
    ASSERT_TRUE(turn.has_value());
    EXPECT_EQ(turn->as_turnChanged_or_null()->playerId, table->seats[1].player_id);
  }
  ASSERT_TRUE(table->seats[1].stream.Send(DrawStock()).ok());
  ASSERT_TRUE(ReceiveRummy(table->seats[1].stream, "gameState").has_value());
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
  EXPECT_EQ(view.players[0].hand.size(), 11u);
  EXPECT_EQ(Face(view.players[0].hand.back()), "9♥");
  EXPECT_TRUE(view.players[1].hand.empty());
  EXPECT_EQ(view.players[1].handCount, 10);
  // The turn is still hers to finish.
  ASSERT_TRUE(resumed->stream.Send(Discard(Named("9", "♥"))).ok());
  auto thrown = ReceiveRummy(table->bob.stream, "gameState");
  ASSERT_TRUE(thrown.has_value());
  EXPECT_EQ(thrown->as_gameState_or_null()->view.currentPlayerId.value_or(""),
            table->bob.player_id);
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
