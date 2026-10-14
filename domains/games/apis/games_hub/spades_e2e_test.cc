// Spades on the room stream (#1658), end to end through the generated
// client.
//
// The NoShuffleDealer deals the pristine deck front first, one card a
// seat from the dealer's left; card i is suit i % 4 (♣ ♦ ♥ ♠), so every
// seat holds one suit and the dealer holds every spade. The dealer then
// takes every trick: the first hand, dealt by seat 0, goes thirteen
// tricks to nothing for team 0.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/time/time.h"
#include "domains/games/apis/games_hub/stream_test_fixture.h"
#include "gmock/gmock.h"

namespace games_hub {
namespace {

using moonbase::games::GameCommands;
using moonbase::games::SpadesMove;
using moonbase::games::SpadesUpdate;
using moonbase::games::SpadesView;

GameCommands Spades(SpadesMove move) {
  moonbase::games::SpadesCommand command;
  command.move = std::move(move);
  return GameCommands::FromSpades(std::move(command));
}

GameCommands Bid(int tricks) {
  moonbase::games::SpadesBid bid;
  bid.tricks = tricks;
  return Spades(SpadesMove::FromBid(bid));
}

GameCommands PlayCard(moonbase::games::Card card) {
  moonbase::games::SpadesPlay play;
  play.card = std::move(card);
  return Spades(SpadesMove::FromPlay(std::move(play)));
}

GameCommands DealNext() { return Spades(SpadesMove::FromDeal(moonbase::games::SpadesDeal{})); }

GameCommands StartTable(std::optional<std::string> mode = std::nullopt) {
  moonbase::games::SpadesStartGame start;
  start.mode = std::move(mode);
  return Spades(SpadesMove::FromStartgame(std::move(start)));
}

GameCommands LeaveTable() {
  return Spades(SpadesMove::FromLeavegame(moonbase::games::LeaveGame{}));
}

std::optional<SpadesUpdate> ReceiveSpades(moonbase::games::PlayClientStream& stream,
                                          const std::string& wanted,
                                          std::chrono::milliseconds budget = kReceiveBudget) {
  return ReceiveEnvelope(
      stream, wanted, "spades",
      [](const moonbase::games::GameEvents& event) { return event.as_spades_or_null(); }, budget);
}

std::string Face(const moonbase::games::Card& card) { return card.rank + card.suit; }

class SpadesFixture : public GamesHubStreamFixture {
 protected:
  void SetUp() override {
    GamesHubStreamFixture::SetUp();
    hub_->SetEventWriter([this](absl::Time, std::string_view line) {
      if (line.find(R"("event":"game_)") != std::string::npos) games_.emplace_back(line);
    });
  }
  // The writer goes first: the hub's teardown can end a game, and
  // `games_` dies before the base's hub does.
  void TearDown() override {
    if (hub_ != nullptr) hub_->SetEventWriter(nullptr);
    GamesHubStreamFixture::TearDown();
  }
  // The stats archive's game lines (#1571), in order.
  std::vector<std::string> games_;

  // A started table, four seats in partners unless asked otherwise, the
  // first seat its creator and first dealer; each seat has read its first
  // view, `views` in seat order.
  struct Started {
    ManySeats table;
    std::vector<SpadesView> views;
  };
  std::optional<Started> StartedTable(int seats = 4,
                                      std::optional<std::string> mode = std::nullopt) {
    auto table = WaitingTable(seats);
    if (!table.has_value()) return std::nullopt;
    if (!table->seats.front().stream.Send(StartTable(std::move(mode))).ok()) return std::nullopt;
    for (Seat& seat : table->seats) {
      if (!ReceiveSpades(seat.stream, "gameStarted").has_value()) return std::nullopt;
    }
    Started started{std::move(*table), std::vector<SpadesView>(seats)};
    if (!ReadViews(started)) return std::nullopt;
    return started;
  }

  // Every seat joined, nobody started.
  std::optional<ManySeats> WaitingTable(int seats) {
    auto room = SeatedRoom(seats);
    if (!room.has_value()) return std::nullopt;
    ManySeats table{std::move(room->seats), room->room_id, ""};
    if (!table.seats[0]
             .stream.Send(Spades(SpadesMove::FromCreategame(moonbase::games::CreateGame{})))
             .ok()) {
      return std::nullopt;
    }
    auto created = ReceiveSpades(table.seats[0].stream, "gameJoined");
    if (!created.has_value()) return std::nullopt;
    table.game_id = created->as_gameJoined_or_null()->view.gameId;
    moonbase::games::JoinGame join;
    join.gameId = table.game_id;
    for (std::size_t i = 1; i < table.seats.size(); ++i) {
      if (!table.seats[i].stream.Send(Spades(SpadesMove::FromJoingame(join))).ok()) {
        return std::nullopt;
      }
      if (!ReceiveSpades(table.seats[i].stream, "gameJoined").has_value()) return std::nullopt;
    }
    return table;
  }

  // Each seat's next view, into `views`.
  static bool ReadViews(Started& started) {
    for (std::size_t i = 0; i < started.views.size(); ++i) {
      auto state = ReceiveSpades(started.table.seats[i].stream, "gameState");
      if (!state.has_value()) return false;
      started.views[i] = state->as_gameState_or_null()->view;
    }
    return true;
  }

  static int SeatOf(const Started& started, const std::string& id) {
    for (int i = 0; i < static_cast<int>(started.views.size()); ++i) {
      if (started.table.seats[i].player_id == id) return i;
    }
    return -1;
  }

  // The seat on turn makes `command`; every seat reads the view after it.
  static void Act(Started& started, const GameCommands& command) {
    const int seat = SeatOf(started, *started.views[0].currentPlayerId);
    ASSERT_GE(seat, 0);
    ASSERT_TRUE(started.table.seats[seat].stream.Send(command).ok());
    ASSERT_TRUE(ReadViews(started));
  }

  // Bids `bids` round from the dealer's left, then plays the first legal
  // card of whoever is on turn until the hand is out.
  static void PlayOut(Started& started, const std::vector<int>& bids) {
    while (started.views[0].phase == "bidding") {
      const int seat = SeatOf(started, *started.views[0].currentPlayerId);
      ASSERT_NO_FATAL_FAILURE(Act(started, Bid(bids[seat])));
    }
    while (started.views[0].phase == "playing") {
      const int seat = SeatOf(started, *started.views[0].currentPlayerId);
      ASSERT_FALSE(started.views[seat].legal.empty());
      ASSERT_NO_FATAL_FAILURE(Act(started, PlayCard(started.views[seat].legal.front())));
    }
  }

  // The dealer takes every trick: dealer bids 13 and partner nil, the
  // other team one each. Team 0 deals hands 1, 3 and 5, and is out at the
  // fifth: 230, 210, 440, 420, 650 to team 1's -20, 210, 190, 420, 400.
  static void WinTheFirstGame(Started& started) {
    for (int hand = 0; hand < 5; ++hand) {
      const int dealer = hand % 4;
      std::vector<int> bids(4);
      for (int seat = 0; seat < 4; ++seat) {
        bids[seat] = seat == dealer ? 13 : seat == (dealer + 2) % 4 ? 0 : 1;
      }
      ASSERT_NO_FATAL_FAILURE(PlayOut(started, bids));
      if (hand < 4) {
        ASSERT_EQ(started.views[0].phase, "scored") << "hand " << hand + 1;
        ASSERT_TRUE(started.table.seats[0].stream.Send(DealNext()).ok());
        ASSERT_TRUE(ReadViews(started));
      }
    }
  }

  // No gameEnded arrives within a moment.
  static void ExpectNoGameEnded(moonbase::games::PlayClientStream& stream) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    while (true) {
      auto received = ReceiveWithin(stream, deadline);
      if (!received.ok() || !received->has_value()) return;
      if (const auto* spades = (*received)->as_spades_or_null()) {
        EXPECT_FALSE(spades->update.as_gameEnded_or_null() != nullptr) << "a game said twice";
      }
    }
  }

  // The refusal the hub sends `seat` next.
  static std::string RefusalTo(Seat& seat) {
    auto refused = ReceiveCase(seat.stream, "commandRejected");
    return refused.has_value() ? refused->as_commandRejected_or_null()->reason : "";
  }
};

// Partners seats four; unnamed, fewer seats play cutthroat, a team of one
// a seat, and three are dealt seventeen each.
TEST_F(SpadesFixture, PartnersSeatsFourAndFewerSeatsPlayCutthroat) {
  auto waiting = WaitingTable(3);
  ASSERT_TRUE(waiting.has_value());
  Seat& host = waiting->seats[0];
  ASSERT_TRUE(host.stream.Send(StartTable("partners")).ok());
  EXPECT_EQ(RefusalTo(host), "partners seats four");
  ASSERT_TRUE(host.stream.Send(StartTable("solo")).ok());
  EXPECT_EQ(RefusalTo(host), "unknown spades mode: solo");
  ASSERT_TRUE(host.stream.Send(StartTable()).ok());
  ASSERT_TRUE(ReceiveSpades(host.stream, "gameStarted").has_value());
  auto state = ReceiveSpades(host.stream, "gameState");
  ASSERT_TRUE(state.has_value());
  const SpadesView& view = state->as_gameState_or_null()->view;
  EXPECT_EQ(view.mode, "cutthroat");
  EXPECT_EQ(view.handSize, 17);
  ASSERT_EQ(view.players.size(), 3u);
  ASSERT_EQ(view.teams.size(), 3u);
  for (int seat = 0; seat < 3; ++seat) {
    EXPECT_EQ(view.players[seat].team, seat);
    EXPECT_EQ(view.players[seat].handCount, 17);
    EXPECT_EQ(view.teams[seat].players, (std::vector<std::string>{waiting->ids()[seat]}));
  }
  EXPECT_EQ(view.players[0].hand.size(), 17u);
}

TEST_F(SpadesFixture, FourSeatsPlayPartnersUnlessCutthroatIsNamed) {
  auto partners = StartedTable();
  ASSERT_TRUE(partners.has_value());
  EXPECT_EQ(partners->views[0].mode, "partners");
  EXPECT_EQ(partners->views[0].handSize, 13);
  ASSERT_EQ(partners->views[0].teams.size(), 2u);
  EXPECT_EQ(partners->views[0].players[2].team, 0);

  auto cutthroat = StartedTable(4, "cutthroat");
  ASSERT_TRUE(cutthroat.has_value());
  EXPECT_EQ(cutthroat->views[0].mode, "cutthroat");
  ASSERT_EQ(cutthroat->views[0].teams.size(), 4u);
  EXPECT_EQ(cutthroat->views[0].players[2].team, 2);
}

// Three seats, seventeen each, the dealer bidding all seventeen and the
// others nil: unshuffled, that ruins everyone, and after two hands every
// seat is bust at -270, -270 and -200. The least bust wins: seat 2 alone,
// not the seats a partnership would pair it with — said once, and the
// room counts it so.
TEST_F(SpadesFixture, ACutthroatGameIsWonByOneSeat) {
  auto started = StartedTable(3);
  ASSERT_TRUE(started.has_value());
  for (int hand = 0; hand < 2; ++hand) {
    std::vector<int> bids = {0, 0, 0};
    bids[hand] = 17;
    ASSERT_NO_FATAL_FAILURE(PlayOut(*started, bids));
    if (hand < 1) {
      ASSERT_EQ(started->views[0].phase, "scored");
      ASSERT_TRUE(started->table.seats[0].stream.Send(DealNext()).ok());
      ASSERT_TRUE(ReadViews(*started));
    }
  }
  const std::vector<std::string> ids = started->table.ids();
  const SpadesView& won = started->views[1];
  ASSERT_EQ(won.phase, "won");
  ASSERT_TRUE(won.result.has_value());
  EXPECT_EQ(won.result->winners, (std::vector<std::string>{ids[2]}));
  EXPECT_EQ(won.result->scores, (std::vector<int>{-270, -270, -200}));
  EXPECT_EQ(won.teams[2].gamesWon, 1);
  EXPECT_EQ(won.teams[0].gamesWon, 0);
  for (Seat& seat : started->table.seats) {
    auto ended = ReceiveSpades(seat.stream, "gameEnded");
    ASSERT_TRUE(ended.has_value());
    EXPECT_EQ(ended->as_gameEnded_or_null()->result.winners, (std::vector<std::string>{ids[2]}));
  }
  auto room = AwaitRoomState(
      started->table.seats[0].stream,
      [](const moonbase::games::RoomState& state) {
        for (const auto& player : state.players) {
          if (player.gamesPlayed == 0) return false;
        }
        return true;
      },
      "every seat's game counted");
  ASSERT_TRUE(room.has_value());
  for (const auto& player : room->players) {
    EXPECT_EQ(player.gamesWon, player.playerId == ids[2] ? 1 : 0) << player.playerId;
  }
}

TEST_F(SpadesFixture, TheFirstHandIsDealtAndEachSeatSeesOnlyItsOwnCards) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  const std::vector<std::string> ids = started->table.ids();
  for (int seat = 0; seat < 4; ++seat) {
    const SpadesView& view = started->views[seat];
    EXPECT_EQ(view.phase, "bidding");
    EXPECT_EQ(view.handNumber, 1);
    EXPECT_EQ(view.dealer, ids[0]);
    EXPECT_EQ(view.currentPlayerId, ids[1]);
    ASSERT_EQ(view.players.size(), 4u);
    for (int other = 0; other < 4; ++other) {
      EXPECT_EQ(view.players[other].playerId, ids[other]);
      EXPECT_EQ(view.players[other].team, other % 2);
      EXPECT_EQ(view.players[other].handCount, 13);
      EXPECT_EQ(view.players[other].hand.size(), other == seat ? 13u : 0u);
      EXPECT_FALSE(view.players[other].bid.has_value());
    }
    EXPECT_TRUE(view.legal.empty());
    ASSERT_EQ(view.teams.size(), 2u);
    EXPECT_EQ(view.teams[0].players, (std::vector<std::string>{ids[0], ids[2]}));
    EXPECT_EQ(view.teams[1].players, (std::vector<std::string>{ids[1], ids[3]}));
    EXPECT_EQ(view.teams[0].score, 0);
  }
  // Seat 0 dealt itself every spade, low to high.
  const auto& spades = started->views[0].players[0].hand;
  EXPECT_EQ(Face(spades.front()), "2♠");
  EXPECT_EQ(Face(spades.back()), "A♠");
}

TEST_F(SpadesFixture, BidsGoRoundInTurnAndTheLeadIsTheDealersLeft) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  ASSERT_TRUE(started->table.seats[2].stream.Send(Bid(3)).ok());
  EXPECT_EQ(RefusalTo(started->table.seats[2]), "not your bid");
  for (int tricks : {3, 0, 2, 5}) ASSERT_NO_FATAL_FAILURE(Act(*started, Bid(tricks)));
  const SpadesView& view = started->views[1];
  EXPECT_EQ(view.phase, "playing");
  EXPECT_EQ(view.currentPlayerId, started->table.ids()[1]);
  EXPECT_EQ(view.players[2].bid, 0);
  // Seat 1 holds only clubs, and leads one.
  EXPECT_EQ(view.legal.size(), 13u);
  EXPECT_EQ(view.legal.front().suit, "♣");
  EXPECT_TRUE(started->views[0].legal.empty());
}

// A card the hand lacks is a stale view; a card it holds but may not
// play is the rules'.
TEST_F(SpadesFixture, APlayNamesACardItHoldsAndMayPlay) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  for (int tricks : {3, 0, 2, 5}) ASSERT_NO_FATAL_FAILURE(Act(*started, Bid(tricks)));
  Seat& lead = started->table.seats[1];
  ASSERT_TRUE(lead.stream.Send(PlayCard(Named("A", "♠"))).ok());
  EXPECT_EQ(RefusalTo(lead), "not in your hand: A♠");
  ASSERT_NO_FATAL_FAILURE(Act(*started, PlayCard(Named("2", "♣"))));
  EXPECT_EQ(started->views[0].trick.size(), 1u);
  EXPECT_EQ(Face(started->views[0].trick[0].card), "2♣");
}

TEST_F(SpadesFixture, AHandPlayedOutIsScoredAndAnySeatDealsTheNext) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  ASSERT_NO_FATAL_FAILURE(PlayOut(*started, {5, 3, 0, 2}));
  const SpadesView& scored = started->views[3];
  EXPECT_EQ(scored.phase, "scored");
  EXPECT_FALSE(scored.currentPlayerId.has_value());
  EXPECT_EQ(scored.players[0].tricks, 13);
  EXPECT_EQ(scored.teams[0].score, 158);
  EXPECT_EQ(scored.teams[0].bags, 8);
  EXPECT_EQ(scored.teams[1].score, -50);
  ASSERT_EQ(scored.hands.size(), 1u);
  EXPECT_EQ(scored.hands[0].bids, (std::vector<int>{5, 3, 0, 2}));
  EXPECT_EQ(scored.hands[0].points, (std::vector<int>{158, -50}));
  ASSERT_TRUE(scored.lastTrick.has_value());
  EXPECT_EQ(scored.lastTrick->winner, started->table.ids()[0]);

  ASSERT_TRUE(started->table.seats[3].stream.Send(DealNext()).ok());
  ASSERT_TRUE(ReadViews(*started));
  EXPECT_EQ(started->views[2].phase, "bidding");
  EXPECT_EQ(started->views[2].handNumber, 2);
  EXPECT_EQ(started->views[2].dealer, started->table.ids()[1]);
  EXPECT_EQ(started->views[2].currentPlayerId, started->table.ids()[2]);
  EXPECT_EQ(started->views[2].teams[0].score, 158);
}

TEST_F(SpadesFixture, NoDealUntilTheHandIsScored) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  ASSERT_TRUE(started->table.seats[0].stream.Send(DealNext()).ok());
  EXPECT_EQ(RefusalTo(started->table.seats[0]), "the hand is not over");
}

TEST_F(SpadesFixture, ATeamThatPassesFiveHundredWinsAndTheRoomCountsIt) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  ASSERT_NO_FATAL_FAILURE(WinTheFirstGame(*started));
  const SpadesView& won = started->views[1];
  EXPECT_EQ(won.phase, "won");
  ASSERT_TRUE(won.result.has_value());
  EXPECT_EQ(won.result->winners,
            (std::vector<std::string>{started->table.ids()[0], started->table.ids()[2]}));
  EXPECT_EQ(won.teams[0].score, 650);
  EXPECT_EQ(won.teams[1].score, 400);
  EXPECT_EQ(won.teams[0].gamesWon, 1);
  const std::vector<std::string> ids = started->table.ids();
  for (Seat& seat : started->table.seats) {
    auto ended = ReceiveSpades(seat.stream, "gameEnded");
    ASSERT_TRUE(ended.has_value());
    EXPECT_EQ(ended->as_gameEnded_or_null()->result.ending, "points");
    EXPECT_EQ(ended->as_gameEnded_or_null()->result.winners,
              (std::vector<std::string>{ids[0], ids[2]}));
    EXPECT_EQ(ended->as_gameEnded_or_null()->result.scores, (std::vector<int>{650, 400}));
  }
  auto room = AwaitRoomState(
      started->table.seats[0].stream,
      [](const moonbase::games::RoomState& state) {
        for (const auto& player : state.players) {
          if (player.gamesPlayed == 0) return false;
        }
        return true;
      },
      "every seat's game counted");
  ASSERT_TRUE(room.has_value());
  for (const auto& player : room->players) {
    const bool winner = player.playerId == ids[0] || player.playerId == ids[2];
    EXPECT_EQ(player.gamesWon, winner ? 1 : 0) << player.playerId;
  }

  // The archive has the game started at four seats and finished, once.
  ASSERT_EQ(games_.size(), 2u);
  EXPECT_THAT(games_[0], ::testing::HasSubstr(R"("event":"game_started")"));
  EXPECT_THAT(games_[0], ::testing::HasSubstr(R"("variant":"spades")"));
  EXPECT_THAT(games_[0], ::testing::HasSubstr(R"("players":4)"));
  EXPECT_THAT(games_[1], ::testing::HasSubstr(R"("event":"game_finished")"));
  EXPECT_THAT(games_[1], ::testing::HasSubstr(R"("outcome":"completed")"));

  // The next deal starts a new game, scores at nothing, and says nothing
  // more of the last.
  ASSERT_TRUE(started->table.seats[2].stream.Send(DealNext()).ok());
  ASSERT_TRUE(ReadViews(*started));
  ExpectNoGameEnded(started->table.seats[1].stream);
  ASSERT_EQ(games_.size(), 3u);
  EXPECT_THAT(games_[2], ::testing::HasSubstr(R"("event":"game_started")"));
  EXPECT_EQ(started->views[0].phase, "bidding");
  EXPECT_EQ(started->views[0].handNumber, 1);
  EXPECT_EQ(started->views[0].teams[0].score, 0);
  EXPECT_EQ(started->views[0].teams[0].gamesWon, 1);
  EXPECT_TRUE(started->views[0].hands.empty());
}

// A leave closes the table: the game in play is abandoned, with no
// winner, and the others see it closed.
TEST_F(SpadesFixture, ALeaveClosesTheTableAndAbandonsTheGame) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  const std::string leaver = started->table.ids()[3];
  ASSERT_TRUE(started->table.seats[3].stream.Send(LeaveTable()).ok());
  ASSERT_TRUE(ReceiveSpades(started->table.seats[3].stream, "gameLeft").has_value());
  for (int seat = 0; seat < 3; ++seat) {
    Seat& stayer = started->table.seats[seat];
    auto state = ReceiveSpades(stayer.stream, "gameState");
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->as_gameState_or_null()->view.phase, "closed");
    EXPECT_EQ(state->as_gameState_or_null()->view.left, leaver);
    ASSERT_TRUE(state->as_gameState_or_null()->view.result.has_value());
    EXPECT_EQ(state->as_gameState_or_null()->view.result->ending, "abandoned");
    auto ended = ReceiveSpades(stayer.stream, "gameEnded");
    ASSERT_TRUE(ended.has_value());
    EXPECT_EQ(ended->as_gameEnded_or_null()->result.ending, "abandoned");
    EXPECT_TRUE(ended->as_gameEnded_or_null()->result.winners.empty());
  }
  auto room = AwaitRoomState(
      started->table.seats[0].stream,
      [](const moonbase::games::RoomState& state) { return state.games.empty(); },
      "the table gone from the room");
  ASSERT_TRUE(room.has_value());
  // Everyone dealt in played it; nobody won it.
  for (const auto& player : room->players) {
    EXPECT_EQ(player.gamesPlayed, 1) << player.playerId;
    EXPECT_EQ(player.gamesWon, 0) << player.playerId;
  }
  ASSERT_EQ(games_.size(), 2u);
  EXPECT_THAT(games_[1], ::testing::HasSubstr(R"("outcome":"abandoned")"));
}

TEST_F(SpadesFixture, TheRoomListsTheTableAsSpades) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  auto room = AwaitRoomState(
      started->table.seats[0].stream,
      [](const moonbase::games::RoomState& state) {
        return !state.games.empty() && state.games[0].status == "bidding";
      },
      "a spades table bidding");
  ASSERT_TRUE(room.has_value());
  EXPECT_EQ(room->games[0].game, "spades");
  EXPECT_EQ(TableOf(*room, started->table.ids()[0]), AtTable("spades", started->table.game_id));
}

// A leave after a won game abandons nothing: the game was said and
// counted when it was won.
TEST_F(SpadesFixture, ALeaveAfterAWonGameEndsNothingFurther) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  ASSERT_NO_FATAL_FAILURE(WinTheFirstGame(*started));
  for (int seat = 0; seat < 3; ++seat) {
    ASSERT_TRUE(ReceiveSpades(started->table.seats[seat].stream, "gameEnded").has_value());
  }
  ASSERT_TRUE(started->table.seats[3].stream.Send(LeaveTable()).ok());
  auto closed = ReceiveSpades(started->table.seats[0].stream, "gameState");
  ASSERT_TRUE(closed.has_value());
  EXPECT_EQ(closed->as_gameState_or_null()->view.phase, "closed");
  EXPECT_FALSE(closed->as_gameState_or_null()->view.result.has_value());
  auto room = AwaitRoomState(
      started->table.seats[0].stream,
      [](const moonbase::games::RoomState& state) { return state.games.empty(); },
      "the table gone from the room");
  ASSERT_TRUE(room.has_value());
  for (const auto& player : room->players) EXPECT_EQ(player.gamesPlayed, 1) << player.playerId;
  ASSERT_EQ(games_.size(), 2u);
  ExpectNoEvent(started->table.seats[0].stream, std::chrono::milliseconds(200));
}

// A fixed shuffle: a hand of every suit.
class SeededDealer : public cards::Dealer {
 public:
  void ShuffleDeck(std::deque<cards::Card>& deck) override {
    std::mt19937 generator(1658);
    std::shuffle(deck.begin(), deck.end(), generator);
  }
};

class ShuffledSpadesFixture : public SpadesFixture {
 protected:
  std::shared_ptr<cards::Dealer> MakeDealer() override { return std::make_shared<SeededDealer>(); }
};

// Your own hand by suit — spades, hearts, clubs, diamonds, the colours
// alternating — and by rank within it.
TEST_F(ShuffledSpadesFixture, YourHandIsSortedBySuitThenRank) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  const auto& hand = started->views[0].players[0].hand;
  ASSERT_EQ(hand.size(), 13u);
  const std::string suits = "♠♥♣♦";
  const std::string ranks[] = {"2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A"};
  const auto order = [&](const moonbase::games::Card& card) {
    const auto rank = std::find(std::begin(ranks), std::end(ranks), card.rank) - std::begin(ranks);
    return std::make_pair(suits.find(card.suit), rank);
  };
  std::set<std::string> seen;
  for (std::size_t i = 0; i < hand.size(); ++i) {
    seen.insert(hand[i].suit);
    if (i > 0) EXPECT_LT(order(hand[i - 1]), order(hand[i])) << Face(hand[i - 1]) << Face(hand[i]);
  }
  EXPECT_GE(seen.size(), 3u) << "the seed should deal a mixed hand";
}


// The seat cap (#1621), pinned before it moves to the game's adapter.
TEST_F(SpadesFixture, AFifthSeatIsRefused) {
  ExpectSeatCap<SpadesMove>(4, Spades,
                      [](moonbase::games::PlayClientStream& stream, const std::string& wanted) {
                        return ReceiveSpades(stream, wanted);
                      });
}


// Spades's branches of the hub (#1621), pinned before they move to
// spades's adapter: the commands spades counts, and the view a resumed
// seat gets back.
TEST_F(SpadesFixture, CommandsAreCountedOnSpadessSeries) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  EXPECT_EQ(metrics_->CounterTotal("spades_commands", {{"command", "createGame"}}), 1);
  EXPECT_EQ(metrics_->CounterTotal("spades_commands", {{"command", "joinGame"}}), 3);
  EXPECT_EQ(metrics_->CounterTotal("spades_commands", {{"command", "startGame"}}), 1);
}

TEST_F(SpadesFixture, AResumedSpadesSeatGetsItsOwnViewBack) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  Seat& dealer = started->table.seats[0];
  const SpadesView& before = started->views[0];
  dealer.stream.Close();
  ASSERT_TRUE(ReceiveCase(started->table.seats[1].stream, "roomState").has_value());
  auto resumed = OpenSeat(dealer.resume_token);
  ASSERT_TRUE(resumed.has_value());
  EXPECT_EQ(resumed->player_id, dealer.player_id);
  auto ready = ReceiveCase(resumed->stream, "sessionReady");
  ASSERT_TRUE(ready.has_value());
  EXPECT_TRUE(ready->as_sessionReady_or_null()->resumed);
  // The seat's own view, in spades's envelope, redacted for its viewer.
  auto joined = ReceiveSpades(resumed->stream, "gameJoined");
  ASSERT_TRUE(joined.has_value());
  const auto& view = joined->as_gameJoined_or_null()->view;
  EXPECT_EQ(view.gameId, started->table.game_id);
  EXPECT_EQ(view.phase, before.phase);
  ASSERT_EQ(view.players.size(), 4u);
  EXPECT_EQ(view.players[0].hand.size(), before.players[0].hand.size());
  EXPECT_FALSE(view.players[0].hand.empty());
  for (std::size_t i = 1; i < view.players.size(); ++i) {
    EXPECT_TRUE(view.players[i].hand.empty()) << "seat " << i;
  }
}

}  // namespace
}  // namespace games_hub
