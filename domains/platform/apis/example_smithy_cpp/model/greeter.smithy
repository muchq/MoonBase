$version: "2.0"

namespace moonbase.greeter

/// Phase 0 spike for the portrait-on-smithy-cpp evaluation
/// (https://github.com/muchq/MoonBase/issues/1168): the smallest end-to-end
/// service proving smithy-cpp codegen and runtime integrate with this repo.
/// The shapes deliberately exercise the traits portrait will need in Phase 1:
/// a POST operation with a JSON body, @required/@length/@range constraint
/// validation, an @default member, and a modeled error.
service Greeter {
    version: "2026-07-16"
    operations: [Greet]
}

@http(method: "POST", uri: "/greeter/v1/greet")
operation Greet {
    input := {
        @required
        @length(min: 1, max: 64)
        name: String

        @range(min: 1, max: 10)
        enthusiasm: Integer = 1
    }

    output := {
        @required
        message: String
    }

    errors: [UnwelcomeGuest]
}

@error("client")
@httpError(403)
structure UnwelcomeGuest {
    @required
    message: String
}
