#include "cpp/cards/card_proto_mapper.h"

#include <gtest/gtest.h>

#include "cpp/cards/card.h"
#include "protos/cards/cards.pb.h"

using namespace cards;

TEST(CardProtoMapper, SuitToProto) {
    auto clubs_proto = SuitToProto(Suit::Clubs);
    EXPECT_EQ(clubs_proto, cards_proto::Suit::Clubs);

    auto diamonds_proto = SuitToProto(Suit::Diamonds);
    EXPECT_EQ(diamonds_proto, cards_proto::Suit::Diamonds);

    auto hearts_proto = SuitToProto(Suit::Hearts);
    EXPECT_EQ(hearts_proto, cards_proto::Suit::Hearts);

    auto spades_proto = SuitToProto(Suit::Spades);
    EXPECT_EQ(spades_proto, cards_proto::Suit::Spades);
}

TEST(CardProtoMapper, RankToProto) {
    EXPECT_EQ(RankToProto(Rank::Ace), cards_proto::Rank::Ace);
    EXPECT_EQ(RankToProto(Rank::Two), cards_proto::Rank::Two);
    EXPECT_EQ(RankToProto(Rank::Three), cards_proto::Rank::Three);
    EXPECT_EQ(RankToProto(Rank::Four), cards_proto::Rank::Four);
    EXPECT_EQ(RankToProto(Rank::Five), cards_proto::Rank::Five);
    EXPECT_EQ(RankToProto(Rank::Six), cards_proto::Rank::Six);
    EXPECT_EQ(RankToProto(Rank::Seven), cards_proto::Rank::Seven);
    EXPECT_EQ(RankToProto(Rank::Eight), cards_proto::Rank::Eight);
    EXPECT_EQ(RankToProto(Rank::Nine), cards_proto::Rank::Nine);
    EXPECT_EQ(RankToProto(Rank::Ten), cards_proto::Rank::Ten);
    EXPECT_EQ(RankToProto(Rank::Jack), cards_proto::Rank::Jack);
    EXPECT_EQ(RankToProto(Rank::Queen), cards_proto::Rank::Queen);
    EXPECT_EQ(RankToProto(Rank::King), cards_proto::Rank::King);
}
