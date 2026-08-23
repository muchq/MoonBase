#include "domains/iili/apis/iili/smithy_handler.h"

#include <optional>
#include <string>

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
  absl::StatusOr<std::optional<std::string>> resolved = shortener_->Resolve(input.slug);
  if (!resolved.ok()) {
    return ToSmithyError(resolved.status());
  }
  if (!resolved->has_value()) {
    return Modeled<gen::NotFoundError>("NotFoundError", "no such link");
  }
  gen::RedirectOutput output;
  output.location = **std::move(resolved);
  return output;
}

smithy::http::RequestHandler WithHeadAsGet(smithy::http::RequestHandler next) {
  return [next = std::move(next)](const smithy::http::HttpRequest& request) {
    if (request.method != "HEAD") return next(request);
    smithy::http::HttpRequest as_get = request;
    as_get.method = "GET";
    smithy::http::HttpResponse response = next(as_get);
    response.body.clear();
    return response;
  };
}

}  // namespace iili
