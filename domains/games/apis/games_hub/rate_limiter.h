#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_RATE_LIMITER_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_RATE_LIMITER_H

#include <chrono>

#include "domains/platform/libs/futility/rate_limiter/sliding_window_rate_limiter.h"
#include "domains/platform/libs/futility/rate_limiter/token_bucket.h"

namespace games_hub {

/// The bucket mechanism is futility's; this file holds the hub's policy.
using TokenBucket = futility::rate_limiter::TokenBucket;

/// The stream's budgets, injectable so tests pin behavior with
/// tiny or huge values instead of racing wall-clock refills. Defaults
/// are far above human play and far below what it takes to hurt the hub
/// mutex or the database (#1234).
///
/// A lobby frame draws from the lobby bucket, a voice frame from the voice
/// bucket, every other frame from the command bucket, and chat frames
/// from the chat bucket too. A voice join is a burst of signals — an
/// offer, a dozen or so candidates and their end to each of up to five
/// peers, about 75 — so its bucket holds a whole join and refills slowly.
/// Game moves
/// are turn-gated and chat is typed; the world's moves are cursor-driven
/// (the site sends up to twenty a second while a player walks), so the
/// lobby's budget is an order of magnitude wider. Chat is tighter because
/// each message is a durable PostgreSQL transaction plus fleet-wide
/// NOTIFY fan-out, and a rate that is fine for gameplay is still a flood
/// in a conversation. Pong's paddle has a bucket of its own: it moves
/// with the pointer, a few dozen times a second, and must not starve the
/// table's other commands.
struct RateLimits {
  double command_burst = 10;
  double command_refill_per_sec = 5;
  double lobby_burst = 120;
  double lobby_refill_per_sec = 60;
  double chat_burst = 3;
  double chat_refill_per_sec = 1;
  double voice_burst = 100;
  double voice_refill_per_sec = 10;
  double paddle_burst = 120;
  double paddle_refill_per_sec = 60;
};

/// The unary budget on `POST /games/v2/session`, keyed by the ADR-0012
/// client address: every mint writes a ticket row, which a reconnect loop or
/// a script would otherwise run for free. 20/min absorbs a client's whole
/// reconnect cycle (ten attempts) with headroom.
///
/// A full key table refuses every client it has not seen, so the table is
/// sized for arrivals, not for abuse: a key is dropped once two idle windows
/// have emptied it, which forgets no budget, and the cap admits ~800 new
/// clients a second indefinitely in a few tens of MB. Only an address spray
/// reaches it.
inline futility::rate_limiter::SlidingWindowRateLimiterConfig SessionMintLimits() {
  return {.max_requests_per_key = 20,
          .window_size = std::chrono::seconds(60),
          .ttl = std::chrono::minutes(2),
          .cleanup_interval = std::chrono::seconds(30),
          .max_keys = 100'000};
}

}  // namespace games_hub

#endif
