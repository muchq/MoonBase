// Handler tests: the generated client drives SmithyTracerHandler — real ray
// tracing — over loopback. Covers the happy path (a decodable PNG comes
// back), cache reuse, each cross-field rule surfacing as the modeled
// InvalidSceneError, and a concurrency hammer that regression-tests the
// tracy::Tracer RNG fix (run it under TSan to prove the data race is gone:
//   bazel test --copt=-fsanitize=thread --linkopt=-fsanitize=thread \
//     //domains/graphics/apis/portrait:smithy_handler_test).

#include "domains/graphics/apis/portrait/smithy_handler.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <iterator>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "domains/graphics/apis/portrait/test_support.h"
#include "domains/graphics/apis/portrait/tracer_service.h"
#include "domains/graphics/libs/image_core/image_core.h"
#include "domains/graphics/libs/png_plusplus/png_plusplus.h"
#include "moonbase/portrait/client.h"
#include "smithy/core/error.h"

namespace {

using moonbase::portrait::InvalidSceneError;
using moonbase::portrait::PortraitClient;
using moonbase::portrait::RenderCapacityError;
using moonbase::portrait::TraceInput;
using portrait::SmithyTracerHandler;
using portrait::TracerService;
using portrait::test_support::LoopbackHarness;
using portrait::test_support::ValidTraceInput;
using portrait::test_support::ValidTraceJson;

bool LooksLikePng(const smithy::Blob& blob) { return pngpp::isPng(blob.data(), blob.size()); }

class SmithyHandlerTest : public ::testing::Test {
 protected:
  LoopbackHarness harness_{std::make_shared<SmithyTracerHandler>()};
};

TEST_F(SmithyHandlerTest, TracesSceneToPng) {
  PortraitClient client = harness_.MakeClient();
  const auto traced = client.Trace(ValidTraceInput());
  ASSERT_TRUE(traced.ok()) << traced.error().message();
  EXPECT_EQ(traced->width, 20);
  EXPECT_EQ(traced->height, 20);
  EXPECT_TRUE(LooksLikePng(traced->base64_png)) << "blob size: " << traced->base64_png.size();
}

TEST_F(SmithyHandlerTest, IdenticalRequestsServeTheCachedRender) {
  PortraitClient client = harness_.MakeClient();
  const auto first = client.Trace(ValidTraceInput());
  ASSERT_TRUE(first.ok()) << first.error().message();
  const auto second = client.Trace(ValidTraceInput());
  ASSERT_TRUE(second.ok()) << second.error().message();
  // The second call is served from TracerService's LRU cache; either way the
  // bytes must be identical for identical scenes.
  EXPECT_EQ(first->base64_png, second->base64_png);
}

// The cross-field rules that constraint traits can't express: each must
// surface as the modeled InvalidSceneError with types.cc's message, and with
// the offending member named when the rule blames exactly one.
struct CrossFieldCase {
  const char* name;
  void (*mutate)(TraceInput&);
  const char* expected_message;
  // nullptr where the rule spans members and so names none.
  const char* expected_field;
};

class SmithyHandlerCrossFieldTest : public SmithyHandlerTest,
                                    public ::testing::WithParamInterface<CrossFieldCase> {};

TEST_P(SmithyHandlerCrossFieldTest, RejectsWithInvalidSceneError) {
  TraceInput input = ValidTraceInput();
  GetParam().mutate(input);

  PortraitClient client = harness_.MakeClient();
  const auto denied = client.Trace(input);
  ASSERT_FALSE(denied.ok());
  EXPECT_EQ(denied.error().code(), "InvalidSceneError");
  ASSERT_NE(denied.error().detail<InvalidSceneError>(), nullptr);
  const InvalidSceneError& detail = *denied.error().detail<InvalidSceneError>();
  EXPECT_EQ(detail.message, GetParam().expected_message);
  if (GetParam().expected_field == nullptr) {
    EXPECT_FALSE(detail.field.has_value()) << "unexpected field: " << detail.field.value_or("");
  } else {
    ASSERT_TRUE(detail.field.has_value());
    EXPECT_EQ(*detail.field, GetParam().expected_field);
  }
}

INSTANTIATE_TEST_SUITE_P(
    CrossFieldRules, SmithyHandlerCrossFieldTest,
    ::testing::Values(
        CrossFieldCase{"CameraAtFocus",
                       [](TraceInput& in) { in.perspective.cameraFocus = {0.0, 0.0, -1.0}; },
                       "Camera position and focus cannot be the same", nullptr},
        CrossFieldCase{"ExtremeAspectRatio",
                       [](TraceInput& in) {
                         in.output.width = 1200;
                         in.output.height = 20;
                       },
                       "Aspect ratio too extreme", nullptr},
        CrossFieldCase{"ZeroRadius", [](TraceInput& in) { in.scene.spheres[0].radius = 0.0; },
                       "Sphere radius must be positive", "/scene/spheres/0/radius"},
        // The index is the sphere's own, not a hardcoded 0: the second
        // sphere is the bad one here and the first is untouched.
        CrossFieldCase{"ZeroRadiusOnSecondSphere",
                       [](TraceInput& in) {
                         in.scene.spheres.push_back(in.scene.spheres[0]);
                         in.scene.spheres[1].radius = 0.0;
                       },
                       "Sphere radius must be positive", "/scene/spheres/1/radius"}),
    [](const auto& info) { return info.param.name; });

// ---------------------------------------------------------------------------
// The status -> error mapping (#1267 finding 3).
//
// absl::StatusCode is an open enum, so ToSmithyError needs a default whatever
// we do and the compiler cannot flag a newly returned status. This table is
// the substitute contract: every canonical code has a row, so adding one to
// TracerService means editing something visible rather than discovering a
// silent 500 in production. EveryStatusCodeHasARow below is what keeps the
// table honest.
struct MappingCase {
  absl::StatusCode code;
  const char* expected_error_code;
  smithy::ErrorKind expected_kind;
};

// The 16 canonical (non-Ok) codes. The set is fixed by the gRPC canonical
// code list that absl::StatusCode mirrors; it has not changed since absl
// adopted it and is not versioned to grow.
constexpr MappingCase kMappingCases[] = {
    {absl::StatusCode::kCancelled, "UnknownError", smithy::ErrorKind::kUnknown},
    {absl::StatusCode::kUnknown, "UnknownError", smithy::ErrorKind::kUnknown},
    {absl::StatusCode::kInvalidArgument, "InvalidSceneError", smithy::ErrorKind::kModeled},
    {absl::StatusCode::kDeadlineExceeded, "UnknownError", smithy::ErrorKind::kUnknown},
    {absl::StatusCode::kNotFound, "UnknownError", smithy::ErrorKind::kUnknown},
    {absl::StatusCode::kAlreadyExists, "UnknownError", smithy::ErrorKind::kUnknown},
    {absl::StatusCode::kPermissionDenied, "UnknownError", smithy::ErrorKind::kUnknown},
    {absl::StatusCode::kResourceExhausted, "RenderCapacityError", smithy::ErrorKind::kModeled},
    {absl::StatusCode::kFailedPrecondition, "UnknownError", smithy::ErrorKind::kUnknown},
    {absl::StatusCode::kAborted, "UnknownError", smithy::ErrorKind::kUnknown},
    {absl::StatusCode::kOutOfRange, "UnknownError", smithy::ErrorKind::kUnknown},
    {absl::StatusCode::kUnimplemented, "UnknownError", smithy::ErrorKind::kUnknown},
    {absl::StatusCode::kInternal, "UnknownError", smithy::ErrorKind::kUnknown},
    {absl::StatusCode::kUnavailable, "UnknownError", smithy::ErrorKind::kUnknown},
    {absl::StatusCode::kDataLoss, "UnknownError", smithy::ErrorKind::kUnknown},
    {absl::StatusCode::kUnauthenticated, "UnknownError", smithy::ErrorKind::kUnknown},
};

class StatusMappingTest : public ::testing::TestWithParam<MappingCase> {};

TEST_P(StatusMappingTest, MapsToTheDeclaredErrorSpace) {
  const absl::Status status(GetParam().code, "some service message");

  const smithy::Error error = portrait::ToSmithyError(status);

  EXPECT_EQ(error.code(), GetParam().expected_error_code);
  EXPECT_EQ(error.kind(), GetParam().expected_kind);
}

INSTANTIATE_TEST_SUITE_P(AllStatusCodes, StatusMappingTest, ::testing::ValuesIn(kMappingCases),
                         [](const auto& info) {
                           return std::string(absl::StatusCodeToString(info.param.code));
                         });

TEST(StatusMappingTest, EveryStatusCodeHasARow) {
  // Walks the canonical range rather than the table, so a code missing from
  // kMappingCases fails here instead of quietly going untested.
  for (int raw = 1; raw <= 16; ++raw) {
    const auto code = static_cast<absl::StatusCode>(raw);
    const int rows = static_cast<int>(
        std::count_if(std::begin(kMappingCases), std::end(kMappingCases),
                      [code](const MappingCase& mapping) { return mapping.code == code; }));
    EXPECT_EQ(rows, 1) << "no unique row for " << absl::StatusCodeToString(code);
  }
  // And nothing beyond the canonical range crept in.
  EXPECT_EQ(std::size(kMappingCases), 16u);
}

TEST(StatusMappingTest, ResourceExhaustedIsRetryableAndCarriesItsTypedDetail) {
  const smithy::Error error =
      portrait::ToSmithyError(absl::ResourceExhaustedError("render exceeded available memory"));

  EXPECT_TRUE(error.retryable());
  ASSERT_NE(error.detail<RenderCapacityError>(), nullptr);
  EXPECT_EQ(error.detail<RenderCapacityError>()->message, "render exceeded available memory");
}

// The kind() matters as much as the code: the generated server only consults
// the code string inside the kModeled branch, and its fall-through for an
// unrecognised modeled code is a 400 carrying error.message() verbatim. An
// unmodeled failure that arrived as kModeled would therefore be both the
// wrong status and a leak.
TEST(StatusMappingTest, UnmappedStatusesAreNotModeled) {
  const smithy::Error error =
      portrait::ToSmithyError(absl::UnavailableError("upstream 10.0.0.5 refused the connection"));

  EXPECT_EQ(error.kind(), smithy::ErrorKind::kUnknown);
  EXPECT_NE(error.kind(), smithy::ErrorKind::kModeled);
}

// ---------------------------------------------------------------------------
// Server-fault paths end to end: a render that fails, through the real
// handler, the generated server, and the loopback transport.
//
// Loopback runs the handler through the same smithy::http::InvokeHandlerGuarded
// that the Beast transport uses, so what these observe is what a deployed
// server produces — no boost needed to check it.

template <typename Thrower>
class ThrowingTracerService : public TracerService {
 protected:
  image_core::Image<image_core::RGB_Double> do_trace(portrait::Scene&, portrait::Perspective&,
                                                     const portrait::Output&) override {
    Thrower::Throw();
    return image_core::Image<image_core::RGB_Double>(1, 1);  // unreachable
  }
};

struct ThrowBadAlloc {
  static void Throw() { throw std::bad_alloc(); }
};

// The secret is what a real PngException carries: libpng's error text, which
// in this repo's writer path is prefixed straight onto whatever libpng said.
constexpr char kSecret[] = "/srv/portrait/internal-scratch-42";
struct ThrowPngExceptionWithSecret {
  static void Throw() { throw pngpp::PngException(std::string("PNG Error: ") + kSecret); }
};

template <typename Thrower>
LoopbackHarness MakeFailingHarness() {
  return LoopbackHarness(
      std::make_shared<SmithyTracerHandler>(std::make_unique<ThrowingTracerService<Thrower>>()));
}

TEST(SmithyHandlerFailureTest, OutOfMemoryIsARetryable503WithTypedDetail) {
  LoopbackHarness harness = MakeFailingHarness<ThrowBadAlloc>();

  const auto response = harness.PostTrace(ValidTraceJson());
  EXPECT_EQ(response.status, 503) << response.body;
  EXPECT_EQ(response.headers.Get("x-error-type").value_or("<missing>"), "RenderCapacityError");

  // Recovering it as a *typed* detail is the part that matters: the generated
  // server's fall-through for a modeled code it does not recognise is a 400
  // carrying the message, so a misspelled code string would still produce a
  // plausible-looking error response. Only the typed detail proves the code
  // matches a declared shape.
  PortraitClient client = harness.MakeClient();
  const auto denied = client.Trace(ValidTraceInput());
  ASSERT_FALSE(denied.ok());
  EXPECT_EQ(denied.error().code(), "RenderCapacityError");
  EXPECT_TRUE(denied.error().retryable());
  ASSERT_NE(denied.error().detail<RenderCapacityError>(), nullptr);
  EXPECT_EQ(denied.error().detail<RenderCapacityError>()->message,
            "render exceeded available memory; try a smaller output");
}

TEST(SmithyHandlerFailureTest, AnyOtherRenderFailureIsA500ThatLeaksNothing) {
  LoopbackHarness harness = MakeFailingHarness<ThrowPngExceptionWithSecret>();

  const auto response = harness.PostTrace(ValidTraceJson());

  EXPECT_EQ(response.status, 500) << response.body;
  EXPECT_EQ(response.body.find(kSecret), std::string::npos) << response.body;
  EXPECT_EQ(response.body.find("PNG Error"), std::string::npos) << response.body;
  // The whole body, pinned: asserting only the absence of today's secret
  // would still pass if some other internal text took its place.
  EXPECT_EQ(response.body, R"({"__type":"InternalFailure","message":"internal failure"})");
}

// The control. Both tests above assert on failures, so a harness that failed
// for some unrelated reason — a malformed body, a route that stopped
// matching — would satisfy them without the render ever being reached.
TEST(SmithyHandlerFailureTest, TheSameHarnessSucceedsWithAWorkingRenderer) {
  LoopbackHarness harness(std::make_shared<SmithyTracerHandler>());

  const auto response = harness.PostTrace(ValidTraceJson());

  EXPECT_EQ(response.status, 200) << response.body;
}

TEST_F(SmithyHandlerTest, ConcurrentTracesAreSafe) {
  // Regression test for the shared tracy::Tracer RNG data race: one handler
  // instance, many threads, a mix of fresh renders and cache hits.
  constexpr int kThreads = 8;
  constexpr int kRequestsPerThread = 4;
  std::vector<std::thread> threads;
  std::vector<int> failures(kThreads, 0);

  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([this, t, &failures] {
      PortraitClient client = harness_.MakeClient();
      for (int i = 0; i < kRequestsPerThread; ++i) {
        // Half the requests collide across threads (cache contention), half
        // are unique to the thread (concurrent fresh renders).
        const double x = (i % 2 == 0) ? 0.5 * i : 0.1 * (t + 1);
        const auto traced = client.Trace(ValidTraceInput(x));
        if (!traced.ok() || !LooksLikePng(traced->base64_png)) ++failures[t];
      }
    });
  }
  for (auto& thread : threads) thread.join();

  for (int t = 0; t < kThreads; ++t) {
    EXPECT_EQ(failures[t], 0) << "thread " << t << " had failing traces";
  }
}

}  // namespace
