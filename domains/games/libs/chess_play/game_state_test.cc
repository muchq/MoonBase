#include "domains/games/libs/chess_play/game_state.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/random/random.h"
#include "absl/status/status.h"

namespace chess_play {
namespace {

// White Ke1 Pe2 against Ke5, White to move.
constexpr char kKpk[] = "8/8/8/4k3/8/8/4P3/4K3 w - - 0 1";
constexpr TimeControl kFiveAndTwo{300'000, 2'000};
constexpr int64_t kT0 = 1'000'000;

GameState Start(const std::string& fen = kKpk, int white_seat = 0, TimeControl tc = kFiveAndTwo) {
  auto state = GameState::start({"alice", "bob"}, "kpk", Opening{fen, white_seat}, tc, kT0);
  EXPECT_TRUE(state.ok()) << state.status();
  return *state;
}

GameState Play(const GameState& state, const std::vector<std::string>& moves, int64_t now = kT0) {
  GameState at = state;
  for (const std::string& uci : moves) {
    auto next = at.move(at.whoseTurn(), uci, now);
    EXPECT_TRUE(next.ok()) << uci << ": " << next.status();
    at = *next;
  }
  return at;
}

TEST(GameStateTest, StartsWithWhiteOnTurnAndItsClockRunning) {
  const GameState state = Start(kKpk, /*white_seat=*/1);
  EXPECT_EQ(state.whiteSeat(), 1);
  EXPECT_EQ(state.seatOf(Color::kWhite), 1);
  EXPECT_EQ(state.colorOf(0), Color::kBlack);
  EXPECT_EQ(state.sideToMove(), Color::kWhite);
  EXPECT_EQ(state.whoseTurn(), 1);
  EXPECT_FALSE(state.isOver());
  EXPECT_FALSE(state.inCheck());
  EXPECT_EQ(state.fen(), kKpk);
  EXPECT_EQ(state.clock().turn_started_ms, kT0);
  EXPECT_EQ(state.remainingMs(Color::kWhite, kT0 + 1'000), 299'000);
  EXPECT_EQ(state.remainingMs(Color::kBlack, kT0 + 1'000), 300'000);
}

TEST(GameStateTest, LegalMovesAreTheSideToMovesInUciSorted) {
  const std::vector<std::string> legal = Start().legalMoves();
  EXPECT_TRUE(std::is_sorted(legal.begin(), legal.end()));
  // The king's four free squares and the pawn's two pushes.
  EXPECT_EQ(legal, (std::vector<std::string>{"e1d1", "e1d2", "e1f1", "e1f2", "e2e3", "e2e4"}));
}

TEST(GameStateTest, AMoveHandsTheTurnOverAndChargesTheMoversClock) {
  const GameState state = Start();
  auto next = state.move(0, "e2e4", kT0 + 10'000);
  ASSERT_TRUE(next.ok()) << next.status();
  EXPECT_EQ(next->moves(), std::vector<std::string>{"e2e4"});
  EXPECT_EQ(next->sideToMove(), Color::kBlack);
  EXPECT_EQ(next->whoseTurn(), 1);
  EXPECT_EQ(next->fen(), "8/8/8/4k3/4P3/8/8/4K3 b - - 0 1");
  // Ten seconds spent, two back.
  EXPECT_EQ(next->clock().remaining_ms[0], 292'000);
  EXPECT_EQ(next->clock().turn_started_ms, kT0 + 10'000);
  EXPECT_EQ(next->remainingMs(Color::kBlack, kT0 + 11'000), 299'000);
}

TEST(GameStateTest, OffTurnIsRefused) {
  const auto next = Start().move(1, "e5e4", kT0);
  EXPECT_EQ(next.status().code(), absl::StatusCode::kFailedPrecondition);
}

TEST(GameStateTest, AMoveNotLegalHereIsRefused) {
  const GameState state = Start();
  for (const char* uci : {"e2e5", "e1e2", "", "e2e4q", "zz", "e2e4 "}) {
    EXPECT_EQ(state.move(0, uci, kT0).status().code(), absl::StatusCode::kInvalidArgument) << uci;
  }
  // A seat that is not at the table.
  EXPECT_FALSE(state.move(2, "e2e4", kT0).ok());
  EXPECT_FALSE(state.move(-1, "e2e4", kT0).ok());
}

TEST(GameStateTest, APromotionNamesItsPiece) {
  const GameState state = Start("8/4P3/8/8/8/8/k7/4K3 w - - 0 1");
  EXPECT_FALSE(state.move(0, "e7e8", kT0).ok());
  const auto queened = state.move(0, "e7e8q", kT0);
  ASSERT_TRUE(queened.ok()) << queened.status();
  EXPECT_EQ(queened->fen(), "4Q3/8/8/8/8/8/k7/4K3 b - - 0 1");
  const auto knighted = state.move(0, "e7e8n", kT0);
  ASSERT_TRUE(knighted.ok());
  EXPECT_EQ(knighted->fen(), "4N3/8/8/8/8/8/k7/4K3 b - - 0 1");
}

TEST(GameStateTest, CheckmateWinsForTheMover) {
  const GameState mated = Play(Start("7k/8/6K1/8/8/8/8/1Q6 w - - 0 1"), {"b1b8"});
  ASSERT_TRUE(mated.isOver());
  EXPECT_EQ(*mated.result(), (Result{Color::kWhite, Ending::kCheckmate}));
  EXPECT_TRUE(mated.inCheck());
  EXPECT_EQ(mated.whoseTurn(), -1);
  EXPECT_TRUE(mated.legalMoves().empty());
}

TEST(GameStateTest, StalemateDraws) {
  const GameState stale = Play(Start("7k/8/6K1/8/8/8/8/5Q2 w - - 0 1"), {"f1f7"});
  ASSERT_TRUE(stale.isOver());
  EXPECT_EQ(*stale.result(), (Result{std::nullopt, Ending::kStalemate}));
}

TEST(GameStateTest, TakingThePawnDrawsOnMaterial) {
  const GameState bare = Play(Start("8/8/8/8/8/8/3kP3/7K w - - 0 1"), {"h1g1", "d2e2"});
  ASSERT_TRUE(bare.isOver());
  EXPECT_EQ(*bare.result(), (Result{std::nullopt, Ending::kInsufficientMaterial}));
}

TEST(GameStateTest, TheThirdOccurrenceOfAPositionDraws) {
  const std::vector<std::string> shuffle = {"e1d1", "e5d5", "d1e1", "d5e5", "e1d1", "e5d5", "d1e1"};
  const GameState twice = Play(Start(), shuffle);
  EXPECT_FALSE(twice.isOver());
  const GameState thrice = Play(twice, {"d5e5"});
  ASSERT_TRUE(thrice.isOver());
  EXPECT_EQ(*thrice.result(), (Result{std::nullopt, Ending::kRepetition}));
}

TEST(GameStateTest, TheFiftyMoveRuleDrawsAndAPawnMoveResetsIt) {
  const GameState start = Start("8/8/8/4k3/8/8/4P3/4K3 w - - 99 60");
  const GameState king = Play(start, {"e1d1"});
  ASSERT_TRUE(king.isOver());
  EXPECT_EQ(*king.result(), (Result{std::nullopt, Ending::kFiftyMoves}));
  EXPECT_FALSE(Play(start, {"e2e3"}).isOver());
}

TEST(GameStateTest, AFinishedGameRefusesMoves) {
  const GameState mated = Play(Start("7k/8/6K1/8/8/8/8/1Q6 w - - 0 1"), {"b1b8"});
  EXPECT_EQ(mated.move(1, "h8h7", kT0).status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_FALSE(mated.resign(1, kT0).ok());
  EXPECT_FALSE(mated.flag(kT0 + 10'000'000).ok());
}

TEST(GameStateTest, EitherSeatMayResignOnTurnOrNot) {
  const GameState state = Start(kKpk, /*white_seat=*/1);
  const auto black_resigns = state.resign(0, kT0 + 5'000);
  ASSERT_TRUE(black_resigns.ok());
  EXPECT_EQ(*black_resigns->result(), (Result{Color::kWhite, Ending::kResignation}));
  const auto white_resigns = state.resign(1, kT0 + 5'000);
  ASSERT_TRUE(white_resigns.ok());
  EXPECT_EQ(*white_resigns->result(), (Result{Color::kBlack, Ending::kResignation}));
  // The clock stops where resignation found it.
  EXPECT_EQ(white_resigns->remainingMs(Color::kWhite, kT0 + 60'000), 295'000);
  EXPECT_FALSE(state.resign(2, kT0).ok());
}

TEST(GameStateTest, FlagIsRefusedWhileTimeRemains) {
  const GameState state = Start();
  EXPECT_EQ(state.flag(kT0 + 299'999).status().code(), absl::StatusCode::kFailedPrecondition);
}

TEST(GameStateTest, TheDefenderOutOfTimeLoses) {
  const GameState black_on_turn = Play(Start(), {"e2e4"});
  const auto flagged = black_on_turn.flag(kT0 + 300'000);
  ASSERT_TRUE(flagged.ok()) << flagged.status();
  EXPECT_EQ(*flagged->result(), (Result{Color::kWhite, Ending::kTimeout}));
  EXPECT_EQ(flagged->remainingMs(Color::kBlack, kT0 + 900'000), 0);
}

TEST(GameStateTest, ThePawnSideOutOfTimeOnlyDrawsAgainstABareKing) {
  const auto flagged = Start().flag(kT0 + 300'000);
  ASSERT_TRUE(flagged.ok());
  EXPECT_EQ(*flagged->result(), (Result{std::nullopt, Ending::kTimeout}));
}

TEST(GameStateTest, AnyMaterialBesideTheKingWinsOnTime) {
  // Black has a knight: White's flag loses.
  const auto flagged = Start("n7/8/8/4k3/8/8/4P3/4K3 w - - 0 1").flag(kT0 + 300'000);
  ASSERT_TRUE(flagged.ok());
  EXPECT_EQ(*flagged->result(), (Result{Color::kBlack, Ending::kTimeout}));
}

TEST(GameStateTest, AMoveThatArrivesAfterTheFlagEndsTheGameOnTimeInstead) {
  const auto late = Play(Start(), {"e2e4"}).move(1, "e5e4", kT0 + 300'001);
  ASSERT_TRUE(late.ok()) << late.status();
  EXPECT_EQ(*late->result(), (Result{Color::kWhite, Ending::kTimeout}));
  EXPECT_EQ(late->moves(), std::vector<std::string>{"e2e4"});
}

TEST(GameStateTest, TheRunningClockNeverShowsBelowZeroNorAClockFromTheFuture) {
  const GameState state = Start();
  EXPECT_EQ(state.remainingMs(Color::kWhite, kT0 + 10'000'000), 0);
  // Another instance's clock a little behind the one that started the turn.
  EXPECT_EQ(state.remainingMs(Color::kWhite, kT0 - 500), 300'000);
  const auto early = state.move(0, "e2e4", kT0 - 500);
  ASSERT_TRUE(early.ok());
  EXPECT_EQ(early->clock().remaining_ms[0], 302'000);
  // Nor does the next turn start before this one did: a move stamped
  // early costs the opponent nothing.
  EXPECT_EQ(early->clock().turn_started_ms, kT0);
}

TEST(GameStateTest, ALeaverLosesByAbandonment) {
  const GameState state = Start(kKpk, /*white_seat=*/0);
  const auto left = state.removePlayer(0, kT0 + 100'000);
  ASSERT_TRUE(left.ok());
  EXPECT_EQ(*left->result(), (Result{Color::kBlack, Ending::kAbandoned}));
  // The clock stops where the leave found it.
  EXPECT_EQ(left->remainingMs(Color::kWhite, kT0 + 900'000), 200'000);
  const auto other = state.removePlayer(1, kT0);
  ASSERT_TRUE(other.ok());
  EXPECT_EQ(*other->result(), (Result{Color::kWhite, Ending::kAbandoned}));
  // Leaving a finished game changes nothing.
  const auto after = left->removePlayer(1, kT0 + 200'000);
  ASSERT_TRUE(after.ok());
  EXPECT_EQ(*after->result(), *left->result());
  EXPECT_FALSE(state.removePlayer(2, kT0).ok());
}

// A flag that fell before the leave is what ended the game: the leave
// cannot turn the pawn side's draw on time into a win.
TEST(GameStateTest, ALeaveAfterTheFlagIsTheFlag) {
  const auto left = Start().removePlayer(1, kT0 + 300'000);
  ASSERT_TRUE(left.ok());
  EXPECT_EQ(*left->result(), (Result{std::nullopt, Ending::kTimeout}));
  // The control: a moment earlier, the same leave is an abandonment.
  EXPECT_EQ(*Start().removePlayer(1, kT0 + 299'999)->result(),
            (Result{Color::kWhite, Ending::kAbandoned}));
}

TEST(GameStateTest, StartRefusesWhatIsNotAGame) {
  const Opening kpk{kKpk, 0};
  EXPECT_FALSE(GameState::start({"alice"}, "kpk", kpk, kFiveAndTwo, kT0).ok());
  EXPECT_FALSE(GameState::start({"alice", "bob", "carol"}, "kpk", kpk, kFiveAndTwo, kT0).ok());
  EXPECT_FALSE(GameState::start({"alice", "alice"}, "kpk", kpk, kFiveAndTwo, kT0).ok());
  EXPECT_FALSE(GameState::start({"alice", "bob"}, "atomic", kpk, kFiveAndTwo, kT0).ok());
  EXPECT_FALSE(GameState::start({"alice", "bob"}, "kpk", {kKpk, 2}, kFiveAndTwo, kT0).ok());
  EXPECT_FALSE(GameState::start({"alice", "bob"}, "kpk", kpk, {0, 0}, kT0).ok());
  EXPECT_FALSE(GameState::start({"alice", "bob"}, "kpk", kpk, {1000, -1}, kT0).ok());
  for (const char* fen : {"", "not a fen", "8/8/8/8/8/8/8/8 w - - 0 1",
                          // Two white kings.
                          "8/8/8/4k3/8/8/4P3/3KK3 w - - 0 1",
                          // Black, not to move, in check.
                          "4k3/8/8/8/8/8/8/4RK2 w - - 0 1",
                          // Stalemate: already over.
                          "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"}) {
    EXPECT_FALSE(GameState::start({"alice", "bob"}, "kpk", {fen, 0}, kFiveAndTwo, kT0).ok()) << fen;
  }
  // The control: the same call with a real position starts.
  EXPECT_TRUE(GameState::start({"alice", "bob"}, "kpk", kpk, kFiveAndTwo, kT0).ok());
}

TEST(GameStateTest, RestoreReplaysTheMovesAndRefusesAnIllegalOne) {
  const GameState played = Play(Start(), {"e2e4", "e5e4"});
  const auto restored =
      GameState::restore(played.players(), "kpk", played.startFen(), played.whiteSeat(),
                         played.moves(), played.timeControl(), played.clock(), played.result());
  ASSERT_TRUE(restored.ok()) << restored.status();
  EXPECT_EQ(restored->fen(), played.fen());
  EXPECT_EQ(restored->legalMoves(), played.legalMoves());

  EXPECT_FALSE(GameState::restore(played.players(), "kpk", played.startFen(), 0, {"e2e4", "e5e5"},
                                  played.timeControl(), played.clock(), std::nullopt)
                   .ok());
}

TEST(GameStateTest, RestoreRefusesAResultTheMovesContradict) {
  const GameState mated = Play(Start("7k/8/6K1/8/8/8/8/1Q6 w - - 0 1"), {"b1b8"});
  const auto restore = [&](std::optional<Result> result) {
    return GameState::restore(mated.players(), "kpk", mated.startFen(), 0, mated.moves(),
                              mated.timeControl(), mated.clock(), result);
  };
  ASSERT_TRUE(restore(mated.result()).ok());
  // Mated on the board, but stored as unfinished, or as another ending.
  EXPECT_FALSE(restore(std::nullopt).ok());
  EXPECT_FALSE(restore(Result{Color::kBlack, Ending::kCheckmate}).ok());
  EXPECT_FALSE(restore(Result{Color::kWhite, Ending::kResignation}).ok());
  // A board ending on a board that has not ended.
  const GameState open = Start();
  EXPECT_FALSE(GameState::restore(open.players(), "kpk", kKpk, 0, {}, kFiveAndTwo, open.clock(),
                                  Result{Color::kWhite, Ending::kCheckmate})
                   .ok());
  EXPECT_TRUE(GameState::restore(open.players(), "kpk", kKpk, 0, {}, kFiveAndTwo, open.clock(),
                                 Result{Color::kWhite, Ending::kResignation})
                  .ok());
}

// A drawn position still has legal moves; a stored game that plays on
// past one is not a game.
TEST(GameStateTest, RestoreRefusesAMovePastTheEnd) {
  const std::vector<std::string> repeated = {"e1d1", "e5d5", "d1e1", "d5e5",
                                             "e1d1", "e5d5", "d1e1", "d5e5"};
  const GameState drawn = Play(Start(), repeated);
  ASSERT_TRUE(drawn.isOver());
  const auto restore = [&](std::vector<std::string> moves) {
    return GameState::restore(drawn.players(), "kpk", kKpk, 0, std::move(moves),
                              drawn.timeControl(), drawn.clock(), std::nullopt);
  };
  std::vector<std::string> past = repeated;
  past.emplace_back("e2e3");  // a position never seen, so only the draw refuses it
  EXPECT_FALSE(restore(past).ok());
  // The control: the moves up to the draw, stored with the draw, restore.
  EXPECT_TRUE(GameState::restore(drawn.players(), "kpk", kKpk, 0, repeated, drawn.timeControl(),
                                 drawn.clock(), drawn.result())
                  .ok());
}

// A resignation that comes after the flag fell is too late: the game had
// already ended on time, whichever seat resigns.
TEST(GameStateTest, AResignationAfterTheFlagIsTheFlag) {
  for (const int seat : {0, 1}) {
    const auto late = Start().resign(seat, kT0 + 300'000);
    ASSERT_TRUE(late.ok());
    EXPECT_EQ(*late->result(), (Result{std::nullopt, Ending::kTimeout})) << seat;
  }
  // The control: a moment earlier, it is a resignation.
  EXPECT_EQ(*Start().resign(1, kT0 + 299'999)->result(),
            (Result{Color::kWhite, Ending::kResignation}));
}

// The wire and the stored row spell every ending this way.
TEST(GameStateTest, EveryEndingIsSpelledAsTheWireSaysIt) {
  const std::vector<std::pair<Ending, std::string>> spellings = {
      {Ending::kCheckmate, "checkmate"},
      {Ending::kStalemate, "stalemate"},
      {Ending::kInsufficientMaterial, "insufficientMaterial"},
      {Ending::kFiftyMoves, "fiftyMoves"},
      {Ending::kRepetition, "repetition"},
      {Ending::kResignation, "resignation"},
      {Ending::kTimeout, "timeout"},
      {Ending::kAbandoned, "abandoned"},
  };
  for (const auto& [ending, name] : spellings) {
    EXPECT_EQ(EndingName(ending), name);
    EXPECT_EQ(ParseEnding(name), ending) << name;
  }
  EXPECT_EQ(ParseEnding("resigned"), std::nullopt);
}

// A timeout in a stored row is the one its clock shows: the side to move
// out of time, and the result that flag would have given.
TEST(GameStateTest, RestoreRefusesATimeoutTheClockContradicts) {
  const auto flagged = Start().flag(kT0 + 300'000);
  ASSERT_TRUE(flagged.ok());
  const auto restore = [&](Clock clock, Result result) {
    return GameState::restore(flagged->players(), "kpk", kKpk, 0, {}, flagged->timeControl(), clock,
                              result);
  };
  ASSERT_TRUE(restore(flagged->clock(), *flagged->result()).ok());  // the control
  // A win on time for the side against a bare king, or for the side that flagged.
  EXPECT_FALSE(restore(flagged->clock(), Result{Color::kWhite, Ending::kTimeout}).ok());
  EXPECT_FALSE(restore(flagged->clock(), Result{Color::kBlack, Ending::kTimeout}).ok());
  // A timeout with time still on the clock.
  Clock time_left = flagged->clock();
  time_left.remaining_ms[0] = 5'000;
  EXPECT_FALSE(restore(time_left, *flagged->result()).ok());
}

// A stored clock the arithmetic cannot hold is not a stored game.
TEST(GameStateTest, RestoreRefusesAClockOutOfRange) {
  const GameState open = Start();
  const auto restore = [&](TimeControl tc, Clock clock) {
    return GameState::restore(open.players(), "kpk", kKpk, 0, {}, tc, clock, std::nullopt);
  };
  ASSERT_TRUE(restore(open.timeControl(), open.clock()).ok());  // the control
  Clock negative_start = open.clock();
  negative_start.turn_started_ms = -1;
  EXPECT_FALSE(restore(open.timeControl(), negative_start).ok());
  Clock far_future = open.clock();
  far_future.turn_started_ms = GameState::kMaxEpochMs + 1;
  EXPECT_FALSE(restore(open.timeControl(), far_future).ok());
  Clock too_long = open.clock();
  too_long.remaining_ms[1] = GameState::kMaxClockMs + 1;
  EXPECT_FALSE(restore(open.timeControl(), too_long).ok());
  EXPECT_FALSE(restore({GameState::kMaxClockMs + 1, 0}, open.clock()).ok());
  EXPECT_FALSE(restore({300'000, GameState::kMaxClockMs + 1}, open.clock()).ok());
}

TEST(ChessSetupTest, EveryNamedSetupIsPlayableAndCarriesItsStableIdentity) {
  struct Expected {
    const char* id;
    const char* name;
    const char* variant;
    const char* fen;
  };
  const std::vector<Expected> fixed = {
      {"kpk-opposition", "K+P vs K — Opposition", "kpk",
       "8/8/4k3/4P3/4K3/8/8/8 w - - 0 1"},
      {"rpr-lucena", "R+P vs R — Lucena position", "rpr",
       "1K1R4/1P6/1k6/8/8/8/r7/8 w - - 0 1"},
      {"qvr-basic", "Q vs R — Basic conversion", "qvr",
       "4k3/8/8/8/8/8/1r6/3QK3 w - - 0 1"},
  };
  std::mt19937_64 gen(1234);
  for (const Expected& expected : fixed) {
    const auto setup = SelectChessSetup(expected.id, gen);
    ASSERT_TRUE(setup.ok()) << expected.id << ": " << setup.status();
    EXPECT_EQ(setup->id, expected.id);
    EXPECT_EQ(setup->name, expected.name);
    EXPECT_EQ(setup->variant, expected.variant);
    EXPECT_EQ(setup->opening.fen, expected.fen);
    const auto state =
        GameState::start({"a", "b"}, setup->variant, setup->opening, kFiveAndTwo, kT0, setup->id);
    ASSERT_TRUE(state.ok()) << expected.id << ": " << state.status();
    EXPECT_EQ(state->setupId(), expected.id);
    EXPECT_FALSE(state->isOver());
  }

  const auto random = SelectChessSetup(kRandomKpkSetup, gen);
  ASSERT_TRUE(random.ok()) << random.status();
  EXPECT_EQ(random->id, kRandomKpkSetup);
  EXPECT_EQ(random->name, "Random K+P vs K");
  EXPECT_EQ(random->variant, "kpk");
  EXPECT_FALSE(SelectChessSetup("not-a-setup", gen).ok());
}

TEST(RandomKpkOpeningTest, EveryOpeningIsAPlayableKpkWithWhiteToMove) {
  std::mt19937_64 gen(1234);
  std::set<int> white_seats;
  std::set<char> pawn_ranks;
  for (int i = 0; i < 500; ++i) {
    const Opening opening = RandomKpkOpening(gen);
    white_seats.insert(opening.white_seat);
    const std::string placement = opening.fen.substr(0, opening.fen.find(' '));
    EXPECT_EQ(std::count(placement.begin(), placement.end(), 'K'), 1) << opening.fen;
    EXPECT_EQ(std::count(placement.begin(), placement.end(), 'k'), 1) << opening.fen;
    EXPECT_EQ(std::count(placement.begin(), placement.end(), 'P'), 1) << opening.fen;
    EXPECT_EQ(std::count_if(placement.begin(), placement.end(),
                            [](char c) { return std::isalpha(static_cast<unsigned char>(c)); }),
              3)
        << opening.fen;
    EXPECT_NE(opening.fen.find(" w "), std::string::npos) << opening.fen;
    // Ranks, top down: the pawn's rank is 8 minus its row.
    int row = 0;
    for (char c : placement) {
      if (c == '/') ++row;
      if (c == 'P') pawn_ranks.insert(static_cast<char>('8' - row));
    }
    const auto state = GameState::start({"a", "b"}, "kpk", opening, kFiveAndTwo, kT0);
    ASSERT_TRUE(state.ok()) << opening.fen << ": " << state.status();
    EXPECT_FALSE(state->isOver());
  }
  EXPECT_EQ(white_seats, (std::set<int>{0, 1}));
  EXPECT_EQ(pawn_ranks, (std::set<char>{'2', '3', '4', '5', '6'}));
}

}  // namespace
}  // namespace chess_play
