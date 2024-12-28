#include "cpp/cards/golf/doc_db_game_store.h"

#include <unordered_map>

#include "protos/golf/golf_model.pb.h"

namespace golf {
using doc_db::DocEgg;
using doc_db::DocIdAndVersion;
using golf_proto::BackendGameState;
using std::unordered_map;

Status DocDbGameStore::AddUser(const string& user_id) {
  DocEgg doc_egg;
  doc_egg.bytes = user_id;
  doc_egg.tags = {{"user", user_id}};
  auto status = client_->InsertDoc("users", doc_egg);
  if (status.ok()) {
    return absl::OkStatus();
  }
  return status.status();
}

StatusOr<bool> DocDbGameStore::UserExists(const string& user_id) const {
  auto status = client_->FindDocByTags("users", {{"user", user_id}});
  if (status.ok()) {
    return true;
  }
  if (status.status().code() == absl::StatusCode::kNotFound) {
    return false;
  }
  return status.status();
}

Status DocDbGameStore::RemoveUser(const string& user_id) {
  return absl::UnimplementedError("todo");
}

StatusOr<std::unordered_set<string>> DocDbGameStore::GetUsers() const {
  return absl::UnimplementedError("todo");
}

static const unordered_map<const Rank, const golf_proto::Rank> PROTO_RANK_BY_RANK{
    {Rank::Two, golf_proto::Rank::Two},     {Rank::Three, golf_proto::Rank::Three},
    {Rank::Four, golf_proto::Rank::Four},   {Rank::Five, golf_proto::Rank::Five},
    {Rank::Six, golf_proto::Rank::Six},     {Rank::Seven, golf_proto::Rank::Seven},
    {Rank::Eight, golf_proto::Rank::Eight}, {Rank::Nine, golf_proto::Rank::Nine},
    {Rank::Ten, golf_proto::Rank::Ten},     {Rank::Jack, golf_proto::Rank::Jack},
    {Rank::Queen, golf_proto::Rank::Queen}, {Rank::King, golf_proto::Rank::King},
    {Rank::Ace, golf_proto::Rank::Ace},
};

static const unordered_map<golf_proto::Rank, Rank> RANK_BY_PROTO_RANK{
    {golf_proto::Rank::Two, Rank::Two},     {golf_proto::Rank::Three, Rank::Three},
    {golf_proto::Rank::Four, Rank::Four},   {golf_proto::Rank::Five, Rank::Five},
    {golf_proto::Rank::Six, Rank::Six},     {golf_proto::Rank::Seven, Rank::Seven},
    {golf_proto::Rank::Eight, Rank::Eight}, {golf_proto::Rank::Nine, Rank::Nine},
    {golf_proto::Rank::Ten, Rank::Ten},     {golf_proto::Rank::Jack, Rank::Jack},
    {golf_proto::Rank::Queen, Rank::Queen}, {golf_proto::Rank::King, Rank::King},
    {golf_proto::Rank::Ace, Rank::Ace},
};

static const unordered_map<Suit, golf_proto::Suit> PROTO_SUIT_BY_SUIT{
    {Suit::Clubs, golf_proto::Suit::Clubs},
    {Suit::Diamonds, golf_proto::Suit::Diamonds},
    {Suit::Hearts, golf_proto::Suit::Hearts},
    {Suit::Spades, golf_proto::Suit::Spades}};

static const unordered_map<golf_proto::Suit, Suit> SUIT_BY_PROTO_SUIT{
    {golf_proto::Suit::Clubs, Suit::Clubs},
    {golf_proto::Suit::Diamonds, Suit::Diamonds},
    {golf_proto::Suit::Hearts, Suit::Hearts},
    {golf_proto::Suit::Spades, Suit::Spades}};

auto card_to_proto(const Card& card) -> golf_proto::Card* {
  golf_proto::Card* card_proto = new golf_proto::Card();
  card_proto->set_rank(PROTO_RANK_BY_RANK.at(card.getRank()));
  card_proto->set_suit(PROTO_SUIT_BY_SUIT.at(card.getSuit()));
  return card_proto;
}

auto proto_to_card(const golf_proto::Card& proto) -> Card {
  return Card{SUIT_BY_PROTO_SUIT.at(proto.suit()), RANK_BY_PROTO_RANK.at(proto.rank())};
}

auto proto_to_player(const golf_proto::Player& proto) -> Player {
  if (proto.has_name()) {
    return Player{proto.name(), proto_to_card(proto.hand().top_left()),
                  proto_to_card(proto.hand().top_right()),
                  proto_to_card(proto.hand().bottom_left()),
                  proto_to_card(proto.hand().bottom_right())};
  }

  return Player{proto_to_card(proto.hand().top_left()), proto_to_card(proto.hand().top_right()),
                proto_to_card(proto.hand().bottom_left()),
                proto_to_card(proto.hand().bottom_right())};
}

auto game_to_proto(const GameStatePtr game_state) -> BackendGameState {
  BackendGameState game_proto;
  game_proto.set_peeked_at_draw_pile(false);
  game_proto.set_who_knocked(-1);
  game_proto.set_whose_turn(0);
  for (auto& c : game_state->getDiscardPile()) {
    golf_proto::Card* card_proto = game_proto.add_discard_pile();
    card_proto->set_rank(PROTO_RANK_BY_RANK.at(c.getRank()));
    card_proto->set_suit(PROTO_SUIT_BY_SUIT.at(c.getSuit()));
  }
  for (auto& c : game_state->getDrawPile()) {
    golf_proto::Card* card_proto = game_proto.add_draw_pile();
    card_proto->set_rank(PROTO_RANK_BY_RANK.at(c.getRank()));
    card_proto->set_suit(PROTO_SUIT_BY_SUIT.at(c.getSuit()));
  }
  for (auto& p : game_state->getPlayers()) {
    golf_proto::Player* player_proto = game_proto.add_players();
    if (p.getName().has_value()) {
      player_proto->set_name(p.getName().value());
    }
    golf_proto::Hand* hand = new golf_proto::Hand();
    hand->set_allocated_bottom_left(card_to_proto(p.cardAt(Position::BottomLeft)));
    hand->set_allocated_bottom_right(card_to_proto(p.cardAt(Position::BottomRight)));
    hand->set_allocated_top_left(card_to_proto(p.cardAt(Position::TopLeft)));
    hand->set_allocated_top_right(card_to_proto(p.cardAt(Position::TopRight)));
    player_proto->set_allocated_hand(hand);
  }

  return game_proto;
}

StatusOr<GameStatePtr> DocDbGameStore::NewGame(const GameStatePtr game_state) {
  auto new_game_proto = game_to_proto(game_state);
  DocEgg doc_egg;
  doc_egg.bytes = new_game_proto.SerializeAsString();
  auto status = client_->InsertDoc("games", doc_egg);
  if (!status.ok()) {
    return status.status();
  }
  auto& doc_id_and_version = status.value();
  return std::make_shared<GameState>(
      game_state->withIdAndVersion(doc_id_and_version.id, doc_id_and_version.version));
}

auto proto_to_game_state(const BackendGameState& proto, const string& game_id,
                         const string& version_id) -> GameState {
  std::deque<Card> mutableDrawPile{};
  for (auto& c : proto.draw_pile()) {
    mutableDrawPile.push_back(proto_to_card(c));
  }
  const std::deque<Card> drawPile = std::move(mutableDrawPile);
  std::deque<Card> mutableDiscardPile{};
  for (auto& c : proto.discard_pile()) {
    mutableDiscardPile.push_back(proto_to_card(c));
  }
  const std::deque<Card> discardPile = std::move(mutableDiscardPile);
  std::vector<Player> mutablePlayers{};
  for (auto& p : proto.players()) {
    mutablePlayers.push_back(proto_to_player(p));
  }
  const std::vector<Player> players = std::move(mutablePlayers);

  return GameState{drawPile,           discardPile,         players, proto.peeked_at_draw_pile(),
                   proto.whose_turn(), proto.who_knocked(), game_id, version_id};
}

StatusOr<GameStatePtr> DocDbGameStore::ReadGame(const string& game_id) const {
  auto status = client_->FindDocById("games", game_id);
  if (!status.ok()) {
    return status.status();
  }
  auto& doc = status.value();
  auto& version_id = doc.version;
  BackendGameState game_state_proto;
  if (!game_state_proto.ParseFromString(doc.bytes)) {
    return absl::InternalError("internal error");
  }

  return std::make_shared<GameState>(proto_to_game_state(game_state_proto, game_id, version_id));
}

StatusOr<GameStatePtr> DocDbGameStore::ReadGameByUserId(const string& user_id) const {
  return absl::UnimplementedError("todo");
}

StatusOr<unordered_set<GameStatePtr>> DocDbGameStore::ReadAllGames() const {
  return absl::UnimplementedError("todo");
}

StatusOr<GameStatePtr> DocDbGameStore::UpdateGame(const GameStatePtr game_state) {
  auto game_proto = game_to_proto(game_state);
  DocEgg doc_egg;
  doc_egg.bytes = game_proto.SerializeAsString();

  DocIdAndVersion old_id_and_version;
  old_id_and_version.id = game_state->getGameId();
  old_id_and_version.version = game_state->getVersionId();

  auto status = client_->UpdateDoc("games", old_id_and_version, doc_egg);
  if (!status.ok()) {
    return status.status();
  }
  auto& new_doc_id_and_version = status.value();
  return std::make_shared<GameState>(
      game_state->withIdAndVersion(new_doc_id_and_version.id, new_doc_id_and_version.version));
}

}  // namespace golf
