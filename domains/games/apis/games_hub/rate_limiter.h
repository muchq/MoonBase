#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_RATE_LIMITER_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_RATE_LIMITER_H

#include "domains/platform/libs/futility/rate_limiter/token_bucket.h"

namespace games_hub {

/// The bucket mechanism lives with its siblings in futility (promoted
/// there once the hub became its second consumer); what stays here is
/// the hub's policy.
using TokenBucket = futility::rate_limiter::TokenBucket;

/// The stream's budgets (#1240), injectable so tests pin behavior with
/// tiny or huge values instead of racing wall-clock refills. Defaults
/// are far above human play and far below what it takes to hurt the hub
/// mutex or the database (#1234).
///
/// A lobby frame draws from the lobby bucket, every other frame from the
/// command bucket, and chat frames from the chat bucket too. Game moves
/// are turn-gated and chat is typed; the world's moves are cursor-driven
/// (the site sends up to twenty a second while a player walks), so the
/// lobby's budget is an order of magnitude wider. Chat is tighter because
/// each message is a durable PostgreSQL transaction plus fleet-wide
/// NOTIFY fan-out, and a rate that is fine for gameplay is still a flood
/// in a conversation.
struct RateLimits {
  double command_burst = 10;
  double command_refill_per_sec = 5;
  double lobby_burst = 120;
  double lobby_refill_per_sec = 60;
  double chat_burst = 3;
  double chat_refill_per_sec = 1;
};

}  // namespace games_hub

#endif
