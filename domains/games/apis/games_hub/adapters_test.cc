#include "domains/games/apis/games_hub/adapters.h"

#include <gtest/gtest.h>

#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

#include "domains/games/libs/cards/castle/game_state.h"
#include "domains/games/libs/cards/dealer.h"
#include "domains/games/libs/chess_play/game_state.h"
#include "domains/games/libs/chess_play/table.h"
#include "domains/games/libs/pong/table.h"

namespace games_hub {
namespace {

using moonbase::games::GameEvents;

// White Kg6 Pe7 against Kh8: e7e8q mates.
constexpr char kMate[] = "7k/4P3/6K1/8/8/8/8/8 w - - 0 1";

chess_play::Table OpenChess() {
  auto opened = chess_play::Table::open({"alice", "bob"}, "kpk", {kMate, 0}, {60'000, 0}, 0,
                                        std::string(chess_play::kRandomKpkSetup));
  EXPECT_TRUE(opened.ok()) << opened.status();
  return *opened;
}

chess_play::Table Mated(const chess_play::Table& table) {
  const int white = table.game().whiteSeat();
  return *table.inGame(
      [&](const chess_play::GameState& game) { return game.move(white, "e7e8q", 0); });
}

castle::GameState DealCastle() {
  cards::Dealer dealer;
  auto dealt = castle::dealCastleGame("tbl", {"ann", "bo"}, dealer.DealNewUnshuffledDeck());
  EXPECT_TRUE(dealt.ok()) << dealt.status();
  return *dealt;
}

// What a table's views read, held for a test.
struct Table {
  std::string game_id = "tbl";
  std::vector<std::string> roster = {};
  std::set<std::string> watchers = {};
  std::optional<ChessTerms> terms = std::nullopt;
  std::optional<EventTag> event = std::nullopt;
  TableContext Context() const { return {game_id, roster, watchers, terms, event, 0}; }
};

std::vector<std::string> Recipients(const Staged& staged) {
  std::vector<std::string> to;
  for (const auto& [recipient, event] : staged) to.push_back(recipient);
  return to;
}

// The redaction discipline: at a table that hides cards, each seat's
// view is built for that seat, so its hand shows to it alone.
TEST(StageViews, EachSeatOfATableThatHidesCardsSeesOnlyItsOwnHand) {
  const Table table{.roster = {"ann", "bo"}};
  const std::optional<HostedState> state = HostedState(DealCastle());
  Staged staged;
  StageViews(GameKind::kCastle, table.Context(), state, 0, staged);
  ASSERT_EQ(Recipients(staged), (std::vector<std::string>{"ann", "bo"}));
  for (std::size_t seat = 0; seat < staged.size(); ++seat) {
    const auto* castle = staged[seat].second.as_castle_or_null();
    ASSERT_NE(castle, nullptr);
    const auto* update = castle->update.as_gameState_or_null();
    ASSERT_NE(update, nullptr);
    const auto& players = update->view.players;
    ASSERT_EQ(players.size(), 2u);
    for (std::size_t other = 0; other < players.size(); ++other) {
      EXPECT_EQ(players[other].hand.empty(), other != seat)
          << staged[seat].first << " sees " << players[other].playerId << "'s hand";
    }
  }
}

// A watchable table hides nothing: its seats and its watchers hear the
// same view, the watchers after the seats.
TEST(StageViews, AWatchableTableShowsSeatsAndWatchersOneView) {
  const Table table{.roster = {"alice", "bob"}, .watchers = {"wes"}};
  const std::optional<HostedState> state = HostedState(OpenChess());
  Staged staged;
  EXPECT_EQ(StageViews(GameKind::kChess, table.Context(), state, 0, staged), 0u);
  ASSERT_EQ(Recipients(staged), (std::vector<std::string>{"alice", "bob", "wes"}));
  EXPECT_EQ(staged[0].second, staged[1].second);
  EXPECT_EQ(staged[0].second, staged[2].second);
}

// A game ended since the table last told of one is owed once, after the
// views and to everyone who saw them; the count returned is what the
// table has now told.
TEST(StageViews, AGameEndedSinceLastToldFollowsTheViewsOnce) {
  const Table table{.roster = {"alice", "bob"}, .watchers = {"wes"}};
  const std::optional<HostedState> state = HostedState(Mated(OpenChess()));
  Staged owed;
  EXPECT_EQ(StageViews(GameKind::kChess, table.Context(), state, 0, owed), 1u);
  ASSERT_EQ(Recipients(owed),
            (std::vector<std::string>{"alice", "bob", "wes", "alice", "bob", "wes"}));
  for (std::size_t i = 3; i < owed.size(); ++i) {
    const auto* chess = owed[i].second.as_chess_or_null();
    ASSERT_NE(chess, nullptr);
    ASSERT_NE(chess->update.as_gameEnded_or_null(), nullptr);
    EXPECT_EQ(chess->update.as_gameEnded().result.winner, std::optional<std::string>("alice"));
  }

  Staged told;
  EXPECT_EQ(StageViews(GameKind::kChess, table.Context(), state, 1, told), 1u);
  EXPECT_EQ(Recipients(told), (std::vector<std::string>{"alice", "bob", "wes"}));
}

TEST(StageViews, AWaitingTableShowsItsRosterAndKeepsItsCount) {
  const Table table{.roster = {"alice"}};
  Staged staged;
  EXPECT_EQ(StageViews(GameKind::kPong, table.Context(), std::nullopt, 3, staged), 3u);
  ASSERT_EQ(Recipients(staged), (std::vector<std::string>{"alice"}));
  const auto* pong = staged[0].second.as_pong_or_null();
  ASSERT_NE(pong, nullptr);
  EXPECT_EQ(pong->update.as_gameState().view.phase, "waiting");
}

// A closed castle table names its finish order after the final views; a
// chess table's results were each game's own gameEnded, so its close adds
// nothing.
TEST(StageClosedNotice, OnlyAGameWithACloseOfItsOwnTellsOne) {
  const Table castle_table{.roster = {"ann", "bo"}};
  Staged castle;
  StageClosedNotice(GameKind::kCastle, castle_table.Context(), HostedState(DealCastle()), castle);
  ASSERT_EQ(Recipients(castle), (std::vector<std::string>{"ann", "bo"}));
  ASSERT_NE(castle[0].second.as_castle_or_null(), nullptr);
  EXPECT_NE(castle[0].second.as_castle().update.as_gameEnded_or_null(), nullptr);

  const Table chess_table{.roster = {"alice", "bob"}, .watchers = {"wes"}};
  Staged chess;
  StageClosedNotice(GameKind::kChess, chess_table.Context(), HostedState(Mated(OpenChess())),
                    chess);
  EXPECT_TRUE(chess.empty());
}

TEST(Decode, AGolfCardIndexOffTheHandIsInvalid) {
  using Golf = Adapter<golf::GameState>;
  moonbase::games::PeekCard peek;
  peek.cardIndex = 4;
  const auto action = Golf::Decode(moonbase::games::GolfMove::FromPeekcard(peek), "ann");
  ASSERT_TRUE(std::holds_alternative<Refusal>(action));
  EXPECT_EQ(std::get<Refusal>(action).kind, RejectKind::kInvalid);
  EXPECT_EQ(std::get<Refusal>(action).reason, "invalid card index");
}

// A knock is told ahead of the views, naming the knocker, and passes no
// turn of its own; a swap passes the turn.
TEST(Decode, AGolfKnockAnnouncesItsKnockerAndNoTurn) {
  using Golf = Adapter<golf::GameState>;
  const auto knock =
      Golf::Decode(moonbase::games::GolfMove::FromKnock(moonbase::games::Knock{}), "ann");
  const auto* transition = std::get_if<Transition<golf::GameState>>(&knock);
  ASSERT_NE(transition, nullptr);
  EXPECT_FALSE(transition->announce_turn);
  ASSERT_TRUE(transition->first.has_value());
  const auto* golf = transition->first->as_golf_or_null();
  ASSERT_NE(golf, nullptr);
  ASSERT_NE(golf->update.as_playerKnocked_or_null(), nullptr);
  EXPECT_EQ(golf->update.as_playerKnocked().playerId, "ann");

  moonbase::games::SwapCard swap;
  swap.cardIndex = 0;
  const auto swapped = Golf::Decode(moonbase::games::GolfMove::FromSwapcard(swap), "ann");
  const auto* swapping = std::get_if<Transition<golf::GameState>>(&swapped);
  ASSERT_NE(swapping, nullptr);
  EXPECT_TRUE(swapping->announce_turn);
  EXPECT_FALSE(swapping->first.has_value());
}

TEST(Decode, AChallengeOnAnUnknownSetupIsTheRulesRefusal) {
  using Chess = Adapter<chess_play::Table>;
  moonbase::games::ChessStartGame terms;
  terms.setupId = "no-such-setup";
  const auto action = Chess::Decode(moonbase::games::ChessMove::FromChallenge(terms), "alice");
  ASSERT_TRUE(std::holds_alternative<Refusal>(action));
  EXPECT_EQ(std::get<Refusal>(action).kind, RejectKind::kRules);
  EXPECT_EQ(std::get<Refusal>(action).reason, "unknown chess setup: no-such-setup");

  terms.setupId = std::string(chess_play::kDefaultChessSetup);
  const auto posted = Chess::Decode(moonbase::games::ChessMove::FromChallenge(terms), "alice");
  ASSERT_TRUE(std::holds_alternative<PostTerms>(posted));
  EXPECT_EQ(std::get<PostTerms>(posted).terms.time_control.initial_ms,
            kDefaultChessClock.initial_ms);
}

// startGame on a seated chess or pong table plays its next game; every
// other game's startGame only ever opens a table.
TEST(Decode, OnlyAGameThatPlaysOnStartsANextGame) {
  const auto chess = Adapter<chess_play::Table>::Decode(
      moonbase::games::ChessMove::FromStartgame(moonbase::games::ChessStartGame{}), "alice");
  ASSERT_TRUE(std::holds_alternative<StartGame<Adapter<chess_play::Table>::Start>>(chess));
  EXPECT_TRUE(std::get<StartGame<Adapter<chess_play::Table>::Start>>(chess).next_game);

  const auto castle = Adapter<castle::GameState>::Decode(
      moonbase::games::CastleMove::FromStartgame(moonbase::games::StartGame{}), "ann");
  ASSERT_TRUE(std::holds_alternative<StartGame<Adapter<castle::GameState>::Start>>(castle));
  EXPECT_FALSE(std::get<StartGame<Adapter<castle::GameState>::Start>>(castle).next_game);
}

// A challenge's table starts on its terms as a first game; a pairing's
// pong table starts on its round robin's variant, rereading it, as a
// startGame would; any other table waits for one.
TEST(FilledStart, OnlyAChallengeOrAPairingStartsWhenFull) {
  const ChessTerms terms{"standard", {60'000, 1'000}};
  const auto challenge = Adapter<chess_play::Table>::FilledStart(terms, std::nullopt);
  ASSERT_TRUE(challenge.has_value());
  EXPECT_FALSE(challenge->next_game);
  EXPECT_EQ(challenge->start.terms, terms);
  EXPECT_FALSE(Adapter<chess_play::Table>::FilledStart(std::nullopt, std::nullopt).has_value());

  const auto pairing = Adapter<pong::Table>::FilledStart(std::nullopt, EventTag{"rr", 0});
  ASSERT_TRUE(pairing.has_value());
  EXPECT_TRUE(pairing->next_game);
  EXPECT_FALSE(Adapter<pong::Table>::FilledStart(std::nullopt, std::nullopt).has_value());

  EXPECT_FALSE(Adapter<castle::GameState>::FilledStart(terms, EventTag{"rr", 0}).has_value());
}

// Each game's envelope counts on the series named for its game.
TEST(GameCounterSeries, EachEnvelopeCountsOnItsOwnGamesSeries) {
  AnyAdapter([&](auto adapter) {
    using A = decltype(adapter);
    const std::string game(GameKindName(A::kKind));
    EXPECT_EQ(A::kCommandSeries, game + "_commands");
    EXPECT_EQ(A::kEventSeries, game + "_events");
    return false;
  });
}

// The machinery that is one game's alone reaches its tables by the
// capability, and passes every other table by.
TEST(Capabilities, OneGamesMachineryFindsOnlyThatGamesTables) {
  static_assert(std::is_same_v<EngineWith<kClocked>, chess_play::Table>);
  static_assert(std::is_same_v<EngineWith<kRealtime>, pong::Table>);
  const std::optional<HostedState> chess = HostedState(OpenChess());
  EXPECT_NE(CapableEngine<kClocked>(chess), nullptr);
  EXPECT_EQ(CapableEngine<kRealtime>(chess), nullptr);
  EXPECT_EQ(CapableEngine<kClocked>(std::nullopt), nullptr);

  const std::optional<HostedState> mated = HostedState(Mated(OpenChess()));
  EXPECT_TRUE(GameOverWith<kRoundRobins>(GameKind::kChess, mated));
  EXPECT_FALSE(GameOverWith<kRoundRobins>(GameKind::kChess, chess));
  EXPECT_FALSE(GameOverWith<kRoundRobins>(GameKind::kChess, std::nullopt));
  EXPECT_FALSE(GameOverWith<kRoundRobins>(GameKind::kCastle, HostedState(DealCastle())));

  EXPECT_TRUE(HasCapability(GameKind::kPong, kWatchable));
  EXPECT_FALSE(HasCapability(GameKind::kSpades, kWatchable));
  EXPECT_TRUE(IsBotId("stockfish@1320"));
  EXPECT_FALSE(IsBotId("alice"));
}

}  // namespace
}  // namespace games_hub
