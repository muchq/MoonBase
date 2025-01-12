#include "card_proto_mapper.h"

#include "card.h"
#include "protos/cards/cards.pb.h"
namespace cards {

cards_proto::Suit SuitToProto(const Suit& s) {
    switch (s) {
        case Suit::Clubs: return cards_proto::Clubs;
        case Suit::Diamonds: return cards_proto::Diamonds;
        case Suit::Hearts: return cards_proto::Hearts;
        case Suit::Spades: return cards_proto::Spades;
    }
}

cards_proto::Rank RankToProto(const Rank& r) {
    switch (r) {
        case Rank::Ace: return cards_proto::Rank::Ace;
        case Rank::Two: return cards_proto::Rank::Two;
        case Rank::Three: return cards_proto::Rank::Three;
        case Rank::Four: return cards_proto::Rank::Four;
        case Rank::Five: return cards_proto::Rank::Five;
        case Rank::Six: return cards_proto::Rank::Six;
        case Rank::Seven: return cards_proto::Rank::Seven;
        case Rank::Eight: return cards_proto::Rank::Eight;
        case Rank::Nine: return cards_proto::Rank::Nine;
        case Rank::Ten: return cards_proto::Rank::Ten;
        case Rank::Jack: return cards_proto::Rank::Jack;
        case Rank::Queen: return cards_proto::Rank::Queen;
        case Rank::King: return cards_proto::Rank::King;
    }
}

}