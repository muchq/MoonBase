#include "cpp/golf_service/api.h"

#include <string>
#include <vector>

#include "absl/status/statusor.h"

namespace golf_service {
using std::string;
using std::vector;

absl::StatusOr<GolfServiceRequest> readRegisterUserRequest(const vector<string>& args) {
  if (args.size() != 2) {
    return absl::InvalidArgumentError("registerUser|<username>");
  }

  return RegisterUserRequest{args.at(1)};
}
}  // namespace golf_service