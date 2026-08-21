#include "domains/r3dr/apis/r3dr_v2/smithy_handler.h"

#include <optional>
#include <string>

namespace r3dr_v2 {

namespace gen = moonbase::r3dr;

smithy::Error ToSmithyError(const absl::Status& status) {
  const std::string message(status.message());
  if (status.code() == absl::StatusCode::kInvalidArgument) {
    smithy::Error error = smithy::Error::Modeled("InvalidRequestError", message);
    error.set_detail(gen::InvalidRequestError{.message = message});
    return error;
  }
  return smithy::Error::Unknown(message);
}

smithy::Outcome<gen::ShortenOutput> SmithyShortenerHandler::Shorten(
    const gen::ShortenInput& input, const smithy::server::RequestContext& context) {
  absl::StatusOr<std::string> slug = shortener_->Shorten(input.longUrl, input.expiresAt);
  if (!slug.ok()) {
    return ToSmithyError(slug.status());
  }
  gen::ShortenOutput output;
  output.slug = *std::move(slug);
  return output;
}

smithy::Outcome<gen::RedirectOutput> SmithyShortenerHandler::Redirect(
    const gen::RedirectInput& input, const smithy::server::RequestContext& context) {
  absl::StatusOr<std::optional<std::string>> resolved = shortener_->Resolve(input.slug);
  if (!resolved.ok()) {
    return ToSmithyError(resolved.status());
  }
  if (!resolved->has_value()) {
    smithy::Error error = smithy::Error::Modeled("NotFoundError", "no such link");
    error.set_detail(gen::NotFoundError{.message = "no such link"});
    return error;
  }
  gen::RedirectOutput output;
  output.location = **std::move(resolved);
  return output;
}

}  // namespace r3dr_v2
