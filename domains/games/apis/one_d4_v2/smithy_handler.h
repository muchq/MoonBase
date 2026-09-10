#ifndef DOMAINS_GAMES_APIS_ONE_D4_V2_SMITHY_HANDLER_H
#define DOMAINS_GAMES_APIS_ONE_D4_V2_SMITHY_HANDLER_H

#include "absl/status/status.h"
#include "moonbase/one_d4/server.h"
#include "opal/core/error.h"
#include "opal/core/outcome.h"
#include "opal/server/router.h"

namespace one_d4_v2 {

/// Maps an Analyze failure onto the operation's declared error space:
/// kInvalidArgument -> InvalidPgnError (400); everything else -> unmodeled,
/// which the generated server answers as a 500 with a fixed body. Analyze
/// only refuses input, so today the second arm is unreachable — the mapping
/// exists so the day that changes, the consequence is already written down
/// and tested rather than decided by accident.
opal::Error ToSmithyError(const absl::Status& status);

/// Serves the generated Smithy OneD4V2 API over Analyze(). Stateless, which
/// is the whole thread-safety story: transports dispatch one handler
/// instance across a thread pool, and this one holds nothing.
class SmithyAnalyzeHandler final : public moonbase::one_d4::OneD4V2Handler {
 public:
  opal::Outcome<moonbase::one_d4::AnalyzeOutput> Analyze(
      const moonbase::one_d4::AnalyzeInput& input,
      const opal::server::RequestContext& context) override;
};

}  // namespace one_d4_v2

#endif  // DOMAINS_GAMES_APIS_ONE_D4_V2_SMITHY_HANDLER_H
