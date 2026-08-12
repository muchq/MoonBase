$version: "2.0"

namespace moonbase.r3dr

/// The r3dr URL shortener: mint a slug for a long URL, and redirect a slug
/// back to it.
///
/// This model describes the wire contract the Go service serves **today**,
/// including two things we intend to change. It is written that way on
/// purpose: the cutover replays a corpus against both binaries and every diff
/// has to be explainable, so behavior changes belong in their own commits with
/// their own tests, not smuggled in as part of the port. `wire_test.go` pins
/// the same contract from the Go side.
///
/// Constraint mapping from dto.go:ValidateShortenRequest:
///   - longUrl non-empty, >= 11 chars, <= 1000  -> @length on LongUrl
///   - longUrl starts with http:// or https://  -> @pattern on LongUrl
///   - expiresAt in the future, within the
///     window                                   -> handler (needs the clock)
///
/// The clock-dependent rules cannot be constraint traits, so they stay in the
/// handler behind InvalidRequestError, the way portrait keeps its cross-field
/// checks. Everything else the framework rejects before the handler runs.
///
/// Error shape changes with the port, unavoidably: mucks.Problem
/// (status/errorCode/message/detail/instance) becomes smithy's
/// ValidationException with a fieldList, modeled errors with X-Error-Type, and
/// non-leaking 500s with x-correlation-id. r3dr.net's page reads
/// `response.ok` and never parses a body, and it is the only known client.
service R3dr {
    version: "2026-08-12"
    operations: [Shorten, Redirect]
}

/// Mints a slug for a long URL. 201 on success, with the slug alone in the
/// body — the caller builds the short link as https://r3dr.net/r/{slug}.
@http(method: "POST", uri: "/shorten", code: 201)
operation Shorten {
    input := {
        @required
        longUrl: LongUrl

        /// Epoch milliseconds. @required matches the service as it behaves
        /// rather than as apis/r3dr/README.md describes it: the README calls
        /// this optional, but Go's zero value decodes as epoch 0, which fails
        /// the "expiration time is in the past" check, so an omitted
        /// expiresAt is a 400 today. (r3dr.net always sends now+7d, which is
        /// why nobody has noticed.) Making it genuinely optional with a
        /// server-side default is a behavior change — worth making, and worth
        /// making on its own.
        @required
        expiresAt: Long
    }

    output := {
        @required
        slug: String
    }

    errors: [InvalidRequestError]
}

/// Resolves a slug to its long URL. 302 with Location, which is what a
/// browser follows; unknown or malformed slugs are 404.
///
/// @suppress is load-bearing: Smithy's HttpResponseCodeSemantics validator
/// fails the model outright on a non-2xx @http code ("Expected an `http` code
/// in the 2xx range, but found 302"), and a redirect is the legitimate
/// exception. Do not delete it — the model stops assembling. See
/// smithy-cpp's docs/server-guide.md, "Redirects (3xx)".
///
/// The 302 carries a `{}` JSON body, because simpleRestJson servers always
/// send one on a status that permits it and alloy's conformance suite pins
/// that. Browsers ignore it; see muchq/smithy-cpp#184 for why suppressing it
/// on 3xx is not available.
@readonly
@suppress(["HttpResponseCodeSemantics"])
@http(method: "GET", uri: "/r/{slug}", code: 302)
operation Redirect {
    input := {
        @required
        @httpLabel
        slug: String
    }

    output := {
        @required
        @httpHeader("Location")
        location: String
    }

    errors: [NotFoundError]
}

/// A URL the service will accept as a redirect target.
///
/// The lower bound is the length of the shortest plausible absolute URL
/// ("http://g.co"), and the upper bound matches the long_url column. The
/// pattern is anchored: Smithy @pattern is an unanchored search.
@length(min: 11, max: 1000)
@pattern("^https?://")
string LongUrl

/// The clock-dependent halves of validation, which no constraint trait can
/// express: an expiry already in the past, or beyond the maximum lifetime.
///
/// Today that maximum is 31 days while the message says 30 (dto.go:24) — a
/// mismatch this model deliberately does not fix, so that the cutover's
/// replay shows no diff here. Pick a number in a follow-up; `wire_test.go`
/// pins both sides of the current boundary.
@error("client")
@httpError(400)
structure InvalidRequestError {
    @required
    message: String
}

/// An unknown slug, or one too short to be one (the Go service rejects
/// anything under two characters before it reaches the store).
///
/// Note the shape change this brings: today a redirect miss answers Go's
/// stdlib text/plain "404 page not found" while every other error is a JSON
/// Problem. The port gives the service one error shape instead of two.
@error("client")
@httpError(404)
structure NotFoundError {
    @required
    message: String
}
