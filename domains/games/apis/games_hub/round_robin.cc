#include "domains/games/apis/games_hub/round_robin.h"

#include <algorithm>
#include <map>
#include <tuple>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace games_hub {
namespace {

// What white scores: 1, ½ or 0.
double WhiteScore(PairingResult result) {
  switch (result) {
    case PairingResult::kWhite:
      return 1;
    case PairingResult::kDraw:
      return 0.5;
    case PairingResult::kBlack:
      return 0;
  }
  return 0;
}

}  // namespace

absl::StatusOr<std::vector<Pairing>> RoundRobinPairings(const std::vector<std::string>& entrants) {
  const std::size_t n = entrants.size();
  if (n < kMinEventEntrants || n > kMaxEventEntrants) {
    return absl::InvalidArgumentError(absl::StrCat("a round robin takes ", kMinEventEntrants,
                                                   " to ", kMaxEventEntrants, " entrants"));
  }
  std::set<std::string_view> seen;
  for (const auto& name : entrants) {
    if (name.empty()) return absl::InvalidArgumentError("an entrant needs a name");
    if (!seen.insert(name).second) {
      return absl::InvalidArgumentError(absl::StrCat(name, " is entered twice"));
    }
  }

  // Berger tables: an odd field pads to even with a bye in the last slot,
  // which plays the fixed board against player r in round r. The other
  // boards pair r+k against r-k round the remaining m-1 slots.
  const std::size_t m = n + n % 2;
  const std::size_t q = m - 1;
  std::vector<Pairing> pairings;
  auto pair = [&](int round, std::size_t white, std::size_t black) {
    if (white < n && black < n) pairings.push_back({round, entrants[white], entrants[black]});
  };
  for (std::size_t r = 0; r < q; ++r) {
    const int round = static_cast<int>(r) + 1;
    const std::size_t fixed = m - 1;
    if (r % 2 == 1) {
      pair(round, fixed, r);
    } else {
      pair(round, r, fixed);
    }
    for (std::size_t k = 1; k < m / 2; ++k) {
      const std::size_t up = (r + k) % q;
      const std::size_t down = (r + q - k) % q;
      if (k % 2 == 1) {
        pair(round, up, down);
      } else {
        pair(round, down, up);
      }
    }
  }
  return pairings;
}

bool Voided(const Pairing& pairing, const std::set<std::string>& withdrawn) {
  return !pairing.result.has_value() &&
         (withdrawn.contains(pairing.white) || withdrawn.contains(pairing.black));
}

std::optional<std::size_t> OpenPairing(const std::vector<Pairing>& pairings,
                                       const std::set<std::string>& withdrawn, std::string_view a,
                                       std::string_view b) {
  for (std::size_t i = 0; i < pairings.size(); ++i) {
    const Pairing& p = pairings[i];
    const bool between = (p.white == a && p.black == b) || (p.white == b && p.black == a);
    if (between && !p.result.has_value() && !Voided(p, withdrawn)) return i;
  }
  return std::nullopt;
}

std::vector<Standing> Standings(const std::vector<std::string>& entrants,
                                const std::vector<Pairing>& pairings,
                                const std::set<std::string>& withdrawn) {
  std::map<std::string, double> points;
  for (const Pairing& p : pairings) {
    if (!p.result.has_value()) continue;
    const double white = WhiteScore(*p.result);
    points[p.white] += white;
    points[p.black] += 1 - white;
  }
  std::map<std::string, double> sb;
  for (const Pairing& p : pairings) {
    if (!p.result.has_value()) continue;
    const double white = WhiteScore(*p.result);
    sb[p.white] += white * points[p.black];
    sb[p.black] += (1 - white) * points[p.white];
  }

  // Points scored against the other players level on points and
  // Sonneborn-Berger: the head-to-head that splits them.
  auto level = [&](const std::string& a, const std::string& b) {
    return points[a] == points[b] && sb[a] == sb[b];
  };
  std::map<std::string, double> among_level;
  for (const Pairing& p : pairings) {
    if (!p.result.has_value() || !level(p.white, p.black)) continue;
    const double white = WhiteScore(*p.result);
    among_level[p.white] += white;
    among_level[p.black] += 1 - white;
  }

  std::vector<Standing> standings;
  for (const auto& name : entrants) {
    standings.push_back({name, points[name], sb[name], 0, withdrawn.contains(name)});
  }
  auto key = [&](const Standing& s) {
    return std::tuple(s.points, s.sonneborn_berger, among_level[s.player]);
  };
  std::stable_sort(standings.begin(), standings.end(),
                   [&](const Standing& a, const Standing& b) { return key(a) > key(b); });
  for (std::size_t i = 0; i < standings.size(); ++i) {
    standings[i].place = i > 0 && key(standings[i]) == key(standings[i - 1])
                             ? standings[i - 1].place
                             : static_cast<int>(i) + 1;
  }
  return standings;
}

}  // namespace games_hub
