#include "domains/iili/apis/iili/smithy_handler.h"

#include <optional>
#include <string>
#include <utility>

namespace iili {

namespace gen = moonbase::iili;
namespace {

template <class Detail>
smithy::Error Modeled(const char* code, std::string message) {
  smithy::Error error = smithy::Error::Modeled(code, message);
  error.set_detail(Detail{.message = std::move(message)});
  return error;
}

}  // namespace

smithy::Error ToSmithyError(const absl::Status& status) {
  std::string message(status.message());
  if (status.code() == absl::StatusCode::kInvalidArgument) {
    return Modeled<gen::InvalidRequestError>("InvalidRequestError", std::move(message));
  }
  return smithy::Error::Unknown(std::move(message));
}

smithy::Outcome<gen::ShortenOutput> SmithyShortenerHandler::Shorten(
    const gen::ShortenInput& input,
    [[maybe_unused]] const smithy::server::RequestContext& context) {
  absl::StatusOr<std::string> slug = shortener_->Shorten(input.longUrl, input.expiresAt);
  if (!slug.ok()) {
    return ToSmithyError(slug.status());
  }
  gen::ShortenOutput output;
  output.slug = *std::move(slug);
  return output;
}

smithy::Outcome<gen::RedirectOutput> SmithyShortenerHandler::Redirect(
    const gen::RedirectInput& input,
    [[maybe_unused]] const smithy::server::RequestContext& context) {
  auto location = Location(input.slug);
  if (!location) return std::move(location).error();
  return gen::RedirectOutput{.location = *std::move(location)};
}

smithy::Outcome<gen::RedirectHeadOutput> SmithyShortenerHandler::RedirectHead(
    const gen::RedirectHeadInput& input,
    [[maybe_unused]] const smithy::server::RequestContext& context) {
  auto location = Location(input.slug);
  if (!location) return std::move(location).error();
  return gen::RedirectHeadOutput{.location = *std::move(location)};
}

smithy::Outcome<std::string> SmithyShortenerHandler::Location(const std::string& slug) {
  absl::StatusOr<std::optional<std::string>> resolved = shortener_->Resolve(slug);
  if (!resolved.ok()) {
    return ToSmithyError(resolved.status());
  }
  if (!resolved->has_value()) {
    return Modeled<gen::NotFoundError>("NotFoundError", "no such link");
  }
  return **std::move(resolved);
}

}  // namespace iili
