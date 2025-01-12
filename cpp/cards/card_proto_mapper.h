#ifndef CPP_CARDS_CARD_PROTO_MAPPER_H
#define CPP_CARDS_CARD_PROTO_MAPPER_H

#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "cpp/cards/card.h"
#include "protos/cards/cards.pb.h"

namespace cards {

cards_proto::Suit SuitToProto(const Suit& s) const;
cards_proto::Rank RankToProto(const Rank& r) const;

}  // namespace cards

#endif
