#include "domains/games/apis/games_hub/rummy_adapter.h"

#include "absl/strings/str_cat.h"
#include "domains/games/apis/games_hub/wire_cards.h"
#include "domains/games/libs/cards/rummy/table_serde.h"

namespace games_hub {
namespace {

// A deal's finish: each deal is a game — the seat that went out
// won it and everyone dealt in played it. A leaver is compacted out of the
// engine's seats, as in castle. No score: rummy's points count up for the
// winner where golf's count against every seat, so adding them to the
// room's running total would mix two scales; the points ride the view's
// lastDeal instead.
StatsDeltas DealDeltas(const rummy::Deal& deal) {
  std::vector<std::string> dealt;
  for (const rummy::Player& seat : rummy::dealPlayers(deal)) dealt.push_back(seat.id);
  return PlayedWon(dealt, rummy::dealWinner(deal).value_or(""));
}

std::string RummyMoveName(rummy::MoveKind kind) {
  switch (kind) {
    case rummy::MoveKind::DrawStock:
      return "drawStock";
    case rummy::MoveKind::DrawDiscard:
      return "drawDiscard";
    case rummy::MoveKind::TakeDown:
      return "takeDown";
    case rummy::MoveKind::Meld:
      return "meld";
    case rummy::MoveKind::LayOff:
      return "layOff";
    case rummy::MoveKind::Discard:
      return "discard";
  }
  return "drawStock";
}

std::string GinMoveName(rummy::GinMoveKind kind) {
  switch (kind) {
    case rummy::GinMoveKind::Pass:
      return "pass";
    case rummy::GinMoveKind::DrawStock:
      return "drawStock";
    case rummy::GinMoveKind::DrawDiscard:
      return "drawDiscard";
    case rummy::GinMoveKind::Discard:
      return "discard";
    case rummy::GinMoveKind::Knock:
      return "knock";
  }
  return "pass";
}

std::string GinStageName(rummy::GinStage stage) {
  switch (stage) {
    case rummy::GinStage::Upcard:
      return "upcard";
    case rummy::GinStage::StockOnly:
    case rummy::GinStage::Draw:
      return "draw";
    case rummy::GinStage::Play:
      return "play";
  }
  return "draw";
}

// A seat's running score: what the deals it won scored, off the sheet.
int RummyPointsOf(const rummy::TableState& table, const std::string& id) {
  int points = 0;
  for (const rummy::DealScore& line : table.getScoreSheet()) {
    if (line.winner == id) points += line.points;
  }
  return points;
}

std::string GinEndingName(rummy::GinEnding ending) {
  switch (ending) {
    case rummy::GinEnding::Knock:
      return "knock";
    case rummy::GinEnding::Gin:
      return "gin";
    case rummy::GinEnding::Undercut:
      return "undercut";
    case rummy::GinEnding::Draw:
      return "draw";
  }
  return "draw";
}

std::vector<moonbase::games::Card> WireCards(const std::vector<cards::Card>& cards) {
  std::vector<moonbase::games::Card> wire;
  wire.reserve(cards.size());
  for (const cards::Card& card : cards) wire.push_back(WireCard(card));
  return wire;
}

}  // namespace

std::string Adapter<rummy::TableState>::Phase(const rummy::TableState& table) {
  switch (table.getPhase()) {
    case rummy::TablePhase::Choosing:
      return "choosing";
    case rummy::TablePhase::Playing:
      return "playing";
    case rummy::TablePhase::Closed:
      break;
  }
  return "ended";
}

std::string Adapter<rummy::TableState>::Turn(const rummy::TableState& table) {
  // Between deals nobody has a turn: the dealer's choice is not one.
  if (table.getPhase() != rummy::TablePhase::Playing) return "";
  const int seat = rummy::dealWhoseTurn(*table.getDeal());
  return seat < 0 ? "" : rummy::dealPlayers(*table.getDeal()).at(seat).id;
}

// A table closes by a leave. A deal it broke up was played by those still
// dealt in; one that had already ended was credited as it did.
StatsDeltas Adapter<rummy::TableState>::ClosedDeltas(const rummy::TableState& table,
                                                     const std::vector<std::string>&) {
  const auto& deal = table.getDeal();
  if (deal.has_value() && rummy::dealPhase(*deal) == rummy::Phase::Abandoned) {
    return DealDeltas(*deal);
  }
  return {};
}

// A deal won by play: the table goes back to choosing.
std::optional<StatsDeltas> Adapter<rummy::TableState>::DealEndDeltas(
    const rummy::TableState& before, const rummy::TableState& after) {
  if (before.getPhase() != rummy::TablePhase::Playing ||
      after.getPhase() != rummy::TablePhase::Choosing) {
    return std::nullopt;
  }
  return DealDeltas(*after.getDeal());
}

moonbase::games::GameEvents Adapter<rummy::TableState>::Wrap(Update update) {
  moonbase::games::RummyEvent event;
  event.update = std::move(update);
  return moonbase::games::GameEvents::FromRummy(std::move(event));
}

moonbase::games::RummyView Adapter<rummy::TableState>::ViewOf(const TableContext& context,
                                                              const rummy::TableState* engine,
                                                              const std::string& viewer_id) {
  moonbase::games::RummyView view;
  view.gameId = context.game_id;
  view.dealNumber = 0;
  view.stockCount = 0;
  view.canDrawStock = false;
  view.discardCount = 0;
  if (engine == nullptr) {
    view.phase = "waiting";
    for (const std::string& roster_id : context.roster) {
      moonbase::games::RummyPlayer player;
      player.playerId = roster_id;
      player.handCount = 0;
      view.players.push_back(std::move(player));
      moonbase::games::RummyStanding standing;
      standing.playerId = roster_id;
      standing.handsWon = 0;
      standing.points = 0;
      view.standings.push_back(std::move(standing));
    }
    return view;
  }

  const rummy::TableState& table = *engine;
  view.phase = Phase(table);
  for (const rummy::DealScore& line : table.getScoreSheet()) {
    moonbase::games::RummyScoreLine scored;
    scored.variant = std::string(rummy::variantName(line.variant));
    scored.winner = line.winner;
    scored.points = line.points;
    view.scoreSheet.push_back(std::move(scored));
  }
  view.dealNumber = table.getDealNumber();
  if (table.getDealNumber() > 0) view.variant = std::string(rummy::variantName(table.getVariant()));
  for (std::size_t i = 0; i < table.getSeats().size(); ++i) {
    moonbase::games::RummyStanding standing;
    standing.playerId = table.getSeats()[i];
    standing.handsWon = table.getWins()[i];
    standing.points = RummyPointsOf(table, standing.playerId);
    view.standings.push_back(std::move(standing));
  }
  if (table.getPhase() == rummy::TablePhase::Choosing) {
    moonbase::games::RummyChoosing choosing;
    choosing.dealer = table.getSeats().at(table.getDealer());
    for (const rummy::Variant variant :
         rummy::variantsFor(static_cast<int>(table.getSeats().size()))) {
      choosing.options.push_back(std::string(rummy::variantName(variant)));
    }
    view.choosing = std::move(choosing);
  }

  const std::optional<rummy::Deal>& deal = table.getDeal();
  // A deal that is over shows every hand: between deals the last one is
  // what the table looks at.
  const bool shown = deal.has_value() && rummy::dealPhase(*deal) != rummy::Phase::Playing;
  if (deal.has_value()) {
    if (!shown) {
      view.currentPlayerId = Turn(table);
    } else {
      moonbase::games::RummyDealResult result;
      result.variant = std::string(rummy::variantName(table.getVariant()));
      result.winner = rummy::dealWinner(*deal);
      result.points = rummy::dealWinnerPoints(*deal);
      const auto& seats = rummy::dealPlayers(*deal);
      for (std::size_t i = 0; i < seats.size(); ++i) {
        moonbase::games::RummyScore score;
        score.playerId = seats[i].id;
        score.deadwood = rummy::dealDeadwood(*deal, static_cast<int>(i));
        result.scores.push_back(std::move(score));
      }
      if (const rummy::GinState* gin = table.ginDeal();
          gin != nullptr && gin->getResult().has_value()) {
        const rummy::GinResult& ended = *gin->getResult();
        moonbase::games::RummyGinResult reckoning;
        reckoning.ending = GinEndingName(ended.ending);
        if (ended.knocker >= 0) reckoning.knocker = seats.at(ended.knocker).id;
        for (std::size_t i = 0; i < ended.hands.size(); ++i) {
          moonbase::games::RummyArrangedHand hand;
          hand.playerId = seats.at(i).id;
          for (const auto& meld : ended.hands[i].melds) hand.melds.push_back(WireCards(meld));
          hand.deadwood = WireCards(ended.hands[i].deadwood);
          reckoning.hands.push_back(std::move(hand));
        }
        reckoning.laidOff = WireCards(ended.laidOff);
        result.gin = std::move(reckoning);
      }
      view.lastDeal = std::move(result);
    }
    std::visit(
        [&](const auto& d) {
          view.stockCount = static_cast<int>(d.getStock().size());
          view.discardCount = static_cast<int>(d.getDiscard().size());
          if (!d.getDiscard().empty()) view.discardTop = WireCard(d.getDiscard().back());
          view.discardPile = WireCards(d.getDiscard());
          if (!shown) {
            view.canDrawStock = d.canDrawStock();
            if (d.getTakenDiscard().has_value()) view.takenDiscard = WireCard(*d.getTakenDiscard());
          }
        },
        *deal);
    if (const rummy::GameState* basic = table.rummyDeal(); basic != nullptr) {
      if (!shown) {
        view.stage = basic->getStage() == rummy::Stage::Draw ? "draw" : "play";
        view.canDrawDiscard =
            basic->getStage() == rummy::Stage::Draw && !basic->getDiscard().empty();
      }
      for (const rummy::Meld& meld : basic->getMelds()) {
        moonbase::games::RummyTableMeld table_meld;
        table_meld.owner = meld.owner;
        table_meld.cards = WireCards(meld.cards);
        view.melds.push_back(std::move(table_meld));
      }
      if (const auto& move = basic->getLastMove(); move.has_value()) {
        moonbase::games::RummyLastMove last;
        last.playerId = move->playerId;
        last.move = RummyMoveName(move->kind);
        last.cards = WireCards(move->cards);
        if (move->meld >= 0) last.meldIndex = move->meld;
        view.lastMove = std::move(last);
      }
    } else if (const rummy::GinState* gin = table.ginDeal(); gin != nullptr) {
      if (!shown) {
        view.stage = GinStageName(gin->getStage());
        view.canDrawDiscard = gin->canDrawDiscard();
      }
      if (const auto& move = gin->getLastMove(); move.has_value()) {
        moonbase::games::RummyLastMove last;
        last.playerId = move->playerId;
        last.move = GinMoveName(move->kind);
        last.cards = WireCards(move->cards);
        view.lastMove = std::move(last);
      }
    }
  }
  for (const std::string& seat_id : table.getSeats()) {
    moonbase::games::RummyPlayer player;
    player.playerId = seat_id;
    player.handCount = 0;
    const int at = deal.has_value()
                       ? std::visit([&](const auto& d) { return d.playerIndex(seat_id); }, *deal)
                       : -1;
    if (at >= 0) {
      const std::vector<cards::Card>& hand = rummy::dealPlayers(*deal).at(at).hand;
      player.handCount = static_cast<int>(hand.size());
      // Own hand faces only while a deal is on, everyone's once it ends.
      if (shown || seat_id == viewer_id) player.hand = WireCards(hand);
    }
    view.players.push_back(std::move(player));
  }
  return view;
}

moonbase::games::GameEvents Adapter<rummy::TableState>::StateEvent(View view) {
  moonbase::games::RummyGameState update;
  update.view = std::move(view);
  return Wrap(Update::FromGamestate(std::move(update)));
}

moonbase::games::GameEvents Adapter<rummy::TableState>::JoinedEvent(View view) {
  moonbase::games::RummyGameJoined joined;
  joined.view = std::move(view);
  return Wrap(Update::FromGamejoined(std::move(joined)));
}

// The table broke up: the evening's standings.
std::optional<moonbase::games::GameEvents> Adapter<rummy::TableState>::ClosedEvent(
    const rummy::TableState& table, const std::vector<std::string>&) {
  moonbase::games::RummyGameEnded ended;
  ended.dealsPlayed = table.getDealNumber();
  for (std::size_t i = 0; i < table.getSeats().size(); ++i) {
    moonbase::games::RummyStanding standing;
    standing.playerId = table.getSeats()[i];
    standing.handsWon = table.getWins()[i];
    standing.points = RummyPointsOf(table, standing.playerId);
    ended.standings.push_back(std::move(standing));
  }
  return Wrap(Update::FromGameended(std::move(ended)));
}

namespace {

using Cards = std::vector<cards::Card>;
using RummyAction = Action<rummy::TableState, Adapter<rummy::TableState>::Start>;
// A move in the deal in play, by the mover's seat — the table's and the
// deal's seats are the same while a deal is on — as each game makes it.
// A game without the move has no step: a basic-only move at a gin table
// is refused, and the other way round.
using BasicStep =
    std::function<absl::StatusOr<rummy::GameState>(const rummy::GameState&, int, const Cards&)>;
using GinStep =
    std::function<absl::StatusOr<rummy::GinState>(const rummy::GinState&, int, const Cards&)>;

// The named cards from `from_hand` on are the hand's; a take-down's first
// is the pile's.
RummyAction Step(const std::vector<moonbase::games::Card>& wire, BasicStep basic, GinStep gin,
                 std::size_t from_hand = 0) {
  auto named = CardsFromWire(wire);
  if (!named.ok()) return Refusal{RejectKind::kInvalid, std::string(named.status().message())};
  return Transition<rummy::TableState>{
      [cards = *std::move(named), basic = std::move(basic), gin = std::move(gin), from_hand](
          const rummy::TableState& table, int seat,
          const MoveContext&) -> absl::StatusOr<rummy::TableState> {
        // The engine's turn and stage come first; only a move it would
        // make but for a card the hand lacks is stale, and that one is
        // named in the wire's spelling.
        const auto held = [&](const auto& deal, auto next) -> decltype(next) {
          if (next.status().code() == absl::StatusCode::kNotFound) {
            return HeldInHand(
                deal.getPlayer(seat).hand,
                Cards(cards.begin() + static_cast<std::ptrdiff_t>(from_hand), cards.end()));
          }
          return next;
        };
        if (table.ginDeal() != nullptr ? gin != nullptr : basic == nullptr) {
          return table.inDeal<rummy::GinState>(
              [&](const rummy::GinState& deal) { return held(deal, gin(deal, seat, cards)); });
        }
        return table.inDeal<rummy::GameState>(
            [&](const rummy::GameState& deal) { return held(deal, basic(deal, seat, cards)); });
      }};
}

}  // namespace

// The moves that put cards down name them: a spelling no card has never
// reaches the table, and a card the hand does not hold is the engine's
// NotFound — the same stale-view refusal castle's rows give — named here
// in the wire's spelling.
RummyAction Adapter<rummy::TableState>::Decode(const Move& move, const std::string&) {
  if (move.as_createGame_or_null() != nullptr) return CreateTable{};
  if (const auto* join = move.as_joinGame_or_null()) return JoinTable{join->gameId};
  if (move.as_startGame_or_null() != nullptr) return StartGame<Start>{};
  if (move.as_leaveGame_or_null() != nullptr) return LeaveTable{};
  if (const auto* choose = move.as_chooseVariant_or_null()) {
    const std::optional<rummy::Variant> variant = rummy::parseVariant(choose->variant);
    if (!variant.has_value()) {
      return Refusal{RejectKind::kInvalid, absl::StrCat("no such game: ", choose->variant)};
    }
    return Transition<rummy::TableState>{
        [chosen = *variant](const rummy::TableState& table, int seat,
                            const MoveContext& context) -> absl::StatusOr<rummy::TableState> {
          // Whether the dealer is here to choose, which lets any seat deal
          // when they are not.
          const bool dealer_away = context.away(table.getSeats().at(table.getDealer()));
          return table.chooseVariant(seat, chosen, context.deck(), dealer_away);
        }};
  }
  if (move.as_pass_or_null() != nullptr) {
    return Step({}, nullptr, [](const rummy::GinState& deal, int seat, const Cards&) {
      return deal.pass(seat);
    });
  }
  if (move.as_drawStock_or_null() != nullptr) {
    return Step(
        {},
        [](const rummy::GameState& deal, int seat, const Cards&) { return deal.drawStock(seat); },
        [](const rummy::GinState& deal, int seat, const Cards&) { return deal.drawStock(seat); });
  }
  if (const auto* draw = move.as_drawDiscard_or_null()) {
    // A card named is the top's; a deeper one is a take-down's to take.
    std::vector<moonbase::games::Card> named;
    if (draw->card.has_value()) named.push_back(*draw->card);
    const auto top = [](const std::vector<cards::Card>& pile, const Cards& cards) -> absl::Status {
      if (!cards.empty() && (pile.empty() || pile.back() != cards.front())) {
        return absl::InvalidArgumentError(
            "a draw takes the top card: take the pile down with takeDown");
      }
      return absl::OkStatus();
    };
    return Step(
        named,
        [top](const rummy::GameState& deal, int seat,
              const Cards& cards) -> absl::StatusOr<rummy::GameState> {
          // The engine's turn and stage refusals come first.
          auto drew = deal.drawDiscard(seat);
          if (!drew.ok()) return drew;
          if (auto ok = top(deal.getDiscard(), cards); !ok.ok()) return ok;
          return drew;
        },
        [](const rummy::GinState& deal, int seat,
           const Cards& cards) -> absl::StatusOr<rummy::GinState> {
          if (!cards.empty() &&
              (deal.getDiscard().empty() || deal.getDiscard().back() != cards.front())) {
            return absl::InvalidArgumentError("gin takes only the discard pile's top card");
          }
          return deal.drawDiscard(seat);
        });
  }
  if (const auto* take_down = move.as_takeDown_or_null()) {
    // Exactly one play: a meld with hand cards, or a lay-off.
    const bool melds = take_down->cards.has_value();
    if (melds == take_down->meldIndex.has_value()) {
      return Refusal{RejectKind::kInvalid,
                     "a take-down melds cards from hand or lays off onto a meld"};
    }
    std::vector<moonbase::games::Card> named{take_down->card};
    if (melds) named.insert(named.end(), take_down->cards->begin(), take_down->cards->end());
    return Step(
        named,
        [index = take_down->meldIndex](const rummy::GameState& deal, int seat, const Cards& cards) {
          if (index.has_value()) return deal.takeDownAndLayOff(seat, cards.front(), *index);
          return deal.takeDownAndMeld(seat, cards.front(), Cards(cards.begin() + 1, cards.end()));
        },
        nullptr, /*from_hand=*/1);
  }
  if (const auto* meld = move.as_meld_or_null()) {
    return Step(
        meld->cards,
        [](const rummy::GameState& deal, int seat, const Cards& cards) {
          return deal.meld(seat, cards);
        },
        nullptr);
  }
  if (const auto* lay_off = move.as_layOff_or_null()) {
    return Step(
        {lay_off->card},
        [index = lay_off->meldIndex](const rummy::GameState& deal, int seat, const Cards& cards) {
          return deal.layOff(seat, cards.front(), index);
        },
        nullptr);
  }
  if (const auto* discard = move.as_discard_or_null()) {
    return Step(
        {discard->card},
        [](const rummy::GameState& deal, int seat, const Cards& cards) {
          return deal.discard(seat, cards.front());
        },
        [](const rummy::GinState& deal, int seat, const Cards& cards) {
          return deal.discard(seat, cards.front());
        });
  }
  if (const auto* knock = move.as_knock_or_null()) {
    return Step({knock->card}, nullptr,
                [](const rummy::GinState& deal, int seat, const Cards& cards) {
                  return deal.knock(seat, cards.front());
                });
  }
  return Refusal{RejectKind::kUnknown, "unknown move"};
}

std::string Adapter<rummy::TableState>::Serialize(const rummy::TableState& engine) {
  return rummy::serializeTableState(engine);
}

absl::StatusOr<rummy::TableState> Adapter<rummy::TableState>::Deserialize(
    const std::string& stored) {
  return rummy::deserializeTableState(stored);
}

}  // namespace games_hub
