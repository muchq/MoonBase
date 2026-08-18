#include "domains/games/libs/chess_cpp/side.h"

#include <gtest/gtest.h>

#include "chess.hpp"

namespace chess_cpp {
namespace {

TEST(Side, SpellsItselfTheWayTheDatabaseColumnDoes) {
  // These two strings are written to motif_occurrences.side, and ChessQL
  // compiles predicates against them. They are not cosmetic.
  EXPECT_EQ(ToString(Side::kWhite), "white");
  EXPECT_EQ(ToString(Side::kBlack), "black");
}

TEST(Side, RoundTripsThroughTheLibrarysColour) {
  EXPECT_EQ(FromColor(ToColor(Side::kWhite)), Side::kWhite);
  EXPECT_EQ(FromColor(ToColor(Side::kBlack)), Side::kBlack);
  EXPECT_EQ(ToColor(Side::kWhite), chess::Color::WHITE);
  EXPECT_EQ(FromColor(chess::Color::BLACK), Side::kBlack);
}

TEST(Side, OpponentIsTheOtherOneAndIsItsOwnInverse) {
  EXPECT_EQ(Opponent(Side::kWhite), Side::kBlack);
  EXPECT_EQ(Opponent(Opponent(Side::kBlack)), Side::kBlack);
}

}  // namespace
}  // namespace chess_cpp
