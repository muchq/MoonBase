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

}