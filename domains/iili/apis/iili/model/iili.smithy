$version: "2.0"

namespace moonbase.iili

/// iili (#1359): mint a slug for a long URL, redirect a slug back to it.
/// longUrl is trait-validated here; the clock-dependent expiry rules answer
/// as InvalidRequestError from the service.
service Iili {
    version: "2026-08-21"
    operations: [Shorten, Redirect, RedirectHead]
}

/// 201 with the slug alone: the caller builds the short link, so the
/// link's domain is a client constant.
@http(method: "POST", uri: "/iili/v1/shorten", code: 201)
operation Shorten {
    input := {
        @required
        longUrl: LongUrl

        /// Epoch millis; in the future, at most 30 days out.
        @required
        expiresAt: Long
    }

    output := {
        @required
        slug: String
    }

    errors: [InvalidRequestError]
}

/// 302 with Location for a live slug; unknown and expired are the same 404.
///
/// @suppress is load-bearing: Smithy's HttpResponseCodeSemantics validator
/// refuses a non-2xx @http code and a redirect is the legitimate exception —
/// deleting it stops the model assembling. See smithy-cpp docs/server-guide.md,
/// "Redirects (3xx)". The 302 carries a `{}` body (alloy conformance pins it);
/// wire_test pins ours.
@readonly
@suppress(["HttpResponseCodeSemantics"])
@http(method: "GET", uri: "/iili/v1/r/{slug}", code: 302)
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

/// The HEAD form of Redirect, which unfurlers and link checkers lead with.
/// Modeled because the router buckets by exact method: an unmodeled HEAD
/// 405s. The transport frames it — Location, the GET's length, no octets
/// (RFC 9110 §9.3.2) — so nothing here or in the handler drops the body.
@readonly
@suppress(["HttpResponseCodeSemantics"])
@http(method: "HEAD", uri: "/iili/v1/r/{slug}", code: 302)
operation RedirectHead {
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

/// Min is the shortest absolute URL ("http://g.co"); pattern is anchored
/// because @pattern is an unanchored search.
@length(min: 11, max: 1000)
@pattern("^https?://")
string LongUrl

@error("client")
@httpError(400)
structure InvalidRequestError {
    @required
    message: String
}

@error("client")
@httpError(404)
structure NotFoundError {
    @required
    message: String
}
