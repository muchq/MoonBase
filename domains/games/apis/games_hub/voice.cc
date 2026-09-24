#include "domains/games/apis/games_hub/voice.h"

#include <utility>

#include "absl/strings/ascii.h"
#include "absl/strings/str_split.h"

namespace games_hub {

namespace {

// One reason for every signal that has no one to reach, so the answer
// never says whether the peer exists.
constexpr char kNoPeer[] = "not in voice with that player";

}  // namespace

void Voice::SetIceServers(std::vector<moonbase::games::IceServer> servers) {
  ice_servers_ = std::move(servers);
}

std::optional<Refusal> Voice::Join(const std::string& player_id, const std::string& room_id,
                                   Deliveries& out) {
  if (in_voice_.contains(player_id)) return Refusal{RejectKind::kState, "already in voice"};
  moonbase::games::VoiceRoster roster;
  for (const auto& [id, member] : in_voice_) {
    if (member.room_id != room_id) continue;
    moonbase::games::VoiceMember listed;
    listed.playerId = id;
    listed.epoch = member.epoch;
    roster.members.push_back(std::move(listed));
  }
  if (roster.members.size() >= kCapacity) return Refusal{RejectKind::kState, "voice is full"};
  roster.iceServers = ice_servers_;
  roster.epoch = next_epoch_++;

  in_voice_.emplace(player_id, Member{room_id, roster.epoch});
  moonbase::games::VoiceJoined joined;
  joined.playerId = player_id;
  joined.epoch = roster.epoch;
  std::vector<std::string> others;
  for (const auto& other : roster.members) others.push_back(other.playerId);
  // The joiner's roster first: whatever reaches it after happened after.
  out.push_back({player_id, moonbase::games::VoiceUpdate::FromRoster(std::move(roster))});
  for (const auto& other : others) {
    out.push_back({other, moonbase::games::VoiceUpdate::FromJoined(joined)});
  }
  return std::nullopt;
}

bool Voice::Leave(const std::string& player_id, Deliveries& out) {
  const auto it = in_voice_.find(player_id);
  if (it == in_voice_.end()) return false;
  const std::string room_id = it->second.room_id;
  in_voice_.erase(it);
  for (const auto& [id, member] : in_voice_) {
    if (member.room_id != room_id) continue;
    moonbase::games::VoiceLeft left;
    left.playerId = player_id;
    out.push_back({id, moonbase::games::VoiceUpdate::FromLeft(std::move(left))});
  }
  return true;
}

std::optional<Refusal> Voice::Signal(const std::string& player_id,
                                     const moonbase::games::SendSignal& signal, Deliveries& out) {
  if (signal.description.has_value() == signal.candidate.has_value()) {
    return Refusal{RejectKind::kInvalid, "a signal carries a description or a candidate"};
  }
  if (signal.description.has_value()) {
    const auto& type = signal.description->type;
    if (type != "offer" && type != "answer") {
      return Refusal{RejectKind::kInvalid, "a description is an offer or an answer"};
    }
  }

  const auto sender = in_voice_.find(player_id);
  const auto peer = in_voice_.find(signal.to);
  if (sender == in_voice_.end() || peer == in_voice_.end() || sender == peer ||
      sender->second.room_id != peer->second.room_id || peer->second.epoch != signal.toEpoch) {
    return Refusal{RejectKind::kState, kNoPeer};
  }

  moonbase::games::ReceivedSignal received;
  received.from = player_id;
  received.description = signal.description;
  received.candidate = signal.candidate;
  out.push_back({signal.to, moonbase::games::VoiceUpdate::FromSignal(std::move(received))});
  return std::nullopt;
}

std::vector<moonbase::games::IceServer> StunServersFromList(std::string_view list) {
  std::vector<moonbase::games::IceServer> servers;
  for (const std::string_view url : absl::StrSplit(list, ',')) {
    const std::string_view trimmed = absl::StripAsciiWhitespace(url);
    if (trimmed.empty()) continue;
    moonbase::games::IceServer server;
    server.urls.emplace_back(trimmed);
    servers.push_back(std::move(server));
  }
  return servers;
}

}  // namespace games_hub
