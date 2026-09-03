#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_HUB_METRICS_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_HUB_METRICS_H

#include <map>
#include <string>

namespace games_hub {

/// One counter series: the name and the exact attributes it is emitted
/// under. A series is identified by both, which is the whole point of
/// spelling the attributes out — see HubHandler::DeclaredCounterSeries().
struct CounterSeries {
  std::string name;
  std::map<std::string, std::string> attributes;
};

/// The bounded label on every hub's rejections counter, closed so each
/// series can be declared at zero (#1327, #1384). The free-text reason goes
/// to the client alone; a rejection's dashboard identity is which of these
/// it was.
///
/// The partition is by who has to act on a spike: kRateLimited — the client
/// is flooding; kInvalid — the client sent malformed input; kState — a
/// well-formed command the current state refuses (out of sync, full,
/// already started, lost a commit race); kRules — the game engine refused
/// the move; kUnavailable — this hub could not do it (storage down,
/// allocation exhausted); kUnknown — a union case with no handler branch,
/// which the wire decoder makes unreachable today, so anything here means a
/// model case shipped without one.
enum class RejectKind { kRateLimited, kInvalid, kState, kRules, kUnavailable, kUnknown };

inline const char* RejectKindName(RejectKind kind) {
  switch (kind) {
    case RejectKind::kRateLimited:
      return "rate_limited";
    case RejectKind::kInvalid:
      return "invalid";
    case RejectKind::kState:
      return "state";
    case RejectKind::kRules:
      return "rules";
    case RejectKind::kUnavailable:
      return "unavailable";
    case RejectKind::kUnknown:
      return "unknown";
  }
  return "unknown";
}

}  // namespace games_hub

#endif
