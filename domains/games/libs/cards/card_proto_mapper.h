#ifndef CPP_CARDS_CARD_PROTO_MAPPER_H
#define CPP_CARDS_CARD_PROTO_MAPPER_H

#include "domains/games/libs/cards/card.h"
#include "domains/games/protos/cards/cards.pb.h"

namespace cards {

cards_proto::Suit SuitToProto(const Suit& s);
cards_proto::Rank RankToProto(const Rank& r);

}  // namespace cards

#endif
