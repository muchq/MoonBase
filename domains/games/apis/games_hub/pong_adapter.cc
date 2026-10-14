#include "domains/games/apis/games_hub/pong_adapter.h"

#include "absl/strings/str_cat.h"
#include "domains/games/libs/pong/rally.h"

namespace games_hub {
namespace {

// Both seats played, the winner won. A leaver played and lost.
StatsDeltas GameDeltas(const pong::Table& table) {
  return PlayedWon(table.players(), table.scoreSheet().back().winner.value_or(""));
}

}  // namespace

// A game in play, one ended with the table waiting on the next, or the
// table closed by a leave.
std::string Adapter<pong::Table>::Phase(const pong::Table& table) {
  if (table.isOver()) return "closed";
  return table.inPlay() ? "playing" : "ended";
}

StatsDeltas Adapter<pong::Table>::ClosedDeltas(const pong::Table& table,
                                               const std::vector<std::string>&) {
  return table.endedByClose() ? GameDeltas(table) : StatsDeltas{};
}

std::optional<StatsDeltas> Adapter<pong::Table>::DealEndDeltas(const pong::Table& before,
                                                               const pong::Table& after) {
  if (!before.inPlay() || after.inPlay() || after.isOver()) return std::nullopt;
  return GameDeltas(after);
}

moonbase::games::GameEvents Adapter<pong::Table>::Wrap(Update update) {
  moonbase::games::PongEvent event;
  event.update = std::move(update);
  return moonbase::games::GameEvents::FromPong(std::move(event));
}

moonbase::games::PongView Adapter<pong::Table>::ViewOf(const TableContext& context,
                                                       const pong::Table* table,
                                                       const std::string&) {
  moonbase::games::PongView view;
  view.gameId = context.game_id;
  if (context.event.has_value()) view.roundRobinId = context.event->event_id;
  if (table == nullptr) {
    view.phase = "waiting";
    for (const std::string& roster_id : context.roster) {
      moonbase::games::PongPlayer player;
      player.playerId = roster_id;
      if (PongBotLevel(roster_id).has_value()) player.bot = true;
      view.players.push_back(std::move(player));
    }
    return view;
  }
  view.phase = Phase(*table);
  view.variant = std::string(pong::VariantName(table->variant()));
  const pong::Court court = pong::CourtOf(table->variant());
  moonbase::games::PongCourt wire;
  wire.width = court.width;
  wire.height = court.height;
  wire.depth = court.depth;
  wire.paddleWidth = court.paddle_width;
  wire.paddleHeight = court.paddle_height;
  wire.ballRadius = court.ball_radius;
  wire.inset = court.inset;
  wire.playerSpeed = court.player_speed;
  wire.ticksPerSecond = pong::kTicksPerSecond;
  view.court = wire;
  for (int seat = 0; seat < pong::kSeats; ++seat) {
    moonbase::games::PongPlayer player;
    player.playerId = table->players()[seat];
    player.lives = table->lives()[seat];
    if (PongBotLevel(player.playerId).has_value()) player.bot = true;
    view.players.push_back(std::move(player));
  }
  for (const pong::GameScore& score : table->scoreSheet()) {
    moonbase::games::PongScoreLine line;
    line.winner = score.winner.value_or("");
    line.ending = std::string(pong::EndingName(score.ending));
    view.scoreSheet.push_back(std::move(line));
  }
  if (!table->inPlay() && !view.scoreSheet.empty()) {
    moonbase::games::PongResult result;
    result.winner = view.scoreSheet.back().winner;
    result.ending = view.scoreSheet.back().ending;
    view.result = std::move(result);
  }
  return view;
}

moonbase::games::GameEvents Adapter<pong::Table>::StateEvent(View view) {
  moonbase::games::PongGameState update;
  update.view = std::move(view);
  return Wrap(Update::FromGamestate(std::move(update)));
}

moonbase::games::GameEvents Adapter<pong::Table>::JoinedEvent(View view) {
  moonbase::games::PongGameJoined joined;
  joined.view = std::move(view);
  return Wrap(Update::FromGamejoined(std::move(joined)));
}

std::vector<moonbase::games::GameEvents> Adapter<pong::Table>::GamesEnded(const pong::Table& table,
                                                                          std::size_t told) {
  std::vector<moonbase::games::GameEvents> events;
  const auto& sheet = table.scoreSheet();
  for (std::size_t i = told; i < sheet.size(); ++i) {
    moonbase::games::PongGameEnded ended;
    ended.result.winner = sheet[i].winner.value_or("");
    ended.result.ending = std::string(pong::EndingName(sheet[i].ending));
    events.push_back(Wrap(Update::FromGameended(ended)));
  }
  return events;
}

namespace {

// A variant named, or smallcat unnamed.
std::optional<pong::Variant> VariantNamed(const std::optional<std::string>& named,
                                          std::string& name) {
  name = named.value_or(std::string(pong::VariantName(pong::Variant::kSmallcat)));
  return pong::ParseVariant(name);
}

}  // namespace

Action<pong::Table, Adapter<pong::Table>::Start> Adapter<pong::Table>::Decode(const Move& move,
                                                                              const std::string&) {
  // startGame is pong's own shape: it names the variant. On a table whose
  // game ended, the next game; otherwise the first.
  if (const auto* start = move.as_startGame_or_null()) {
    std::string name;
    const std::optional<pong::Variant> variant = VariantNamed(start->variant, name);
    if (!variant.has_value()) {
      return Refusal{RejectKind::kRules, absl::StrCat("unknown pong variant: ", name)};
    }
    return StartGame<Start>{Start{*variant}, /*next_game=*/true};
  }
  if (const auto* paddle = move.as_paddle_or_null()) return Steer{*paddle};
  if (const auto* add = move.as_addBot_or_null()) return AddBot{PongBotId(add->level), false};
  if (const auto* watch = move.as_watch_or_null()) return WatchTable{watch->gameId};
  if (const auto* create = move.as_createRoundRobin_or_null()) {
    std::string name;
    const std::optional<pong::Variant> variant = VariantNamed(create->variant, name);
    if (!variant.has_value()) {
      return Refusal{RejectKind::kRules, absl::StrCat("unknown pong variant: ", name)};
    }
    return CreateRoundRobin{create->entrants, ChessTerms{}, *variant};
  }
  if (const auto* withdraw = move.as_withdraw_or_null()) return Withdraw{*withdraw};
  if (const auto* forfeit = move.as_forfeit_or_null()) return Forfeit{*forfeit};
  if (move.as_roundRobins_or_null() != nullptr) return ListRoundRobins{};
  if (const auto* play = move.as_playRoundRobin_or_null()) return PlayRoundRobin{*play};
  if (move.as_createGame_or_null() != nullptr) return CreateTable{};
  if (const auto* join = move.as_joinGame_or_null()) return JoinTable{join->gameId};
  if (move.as_leaveGame_or_null() != nullptr) return LeaveTable{};
  return Refusal{RejectKind::kUnknown, "unknown move"};
}

}  // namespace games_hub
