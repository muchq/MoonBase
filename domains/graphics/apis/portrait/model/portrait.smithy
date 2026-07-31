$version: "2.0"

namespace moonbase.portrait

/// The Portrait API: ray-traced scene rendering. Tuples serialize as JSON
/// arrays; the rendered PNG returns as base64.
///
/// Constraint mapping from types.cc validate* rules:
///   - spheres count 1..10            -> @length on SphereList
///   - radius/specular/reflective,
///     intensity, starProbability,
///     width/height ranges            -> @range on the members
///   - Vec3/Color exactly 3 elements  -> @length on the list shapes
///   - color channel 0..255           -> @range on ColorChannel
///   - light type membership          -> LightType enum validation
///   - NaN/Inf checks                 -> unreachable over standard JSON;
///                                       dropped
/// Cross-field rules stay in the handler (InvalidSceneError):
///   - cameraPosition != cameraFocus
///   - aspect ratio within [1/50, 50]
///   - strictly positive radius (@range is inclusive of 0)
///   - backgroundColor default [0, 0, 0] (Smithy list defaults must be empty)
///
/// Server-fault side of the space: a render that the server could not
/// produce for a valid scene is RenderCapacityError when the client can
/// recover by asking for less, and an unmodeled InternalFailure 500
/// otherwise. Overload is not modeled here — the deployment answers 429 in
/// the middleware chain (main.cc), before the operation is reached.
service Portrait {
    version: "2026-07-16"
    operations: [Trace]
}

/// Renders a ray-traced scene and returns it as a PNG. The Blob output
/// member serializes as standard base64 in JSON.
@http(method: "POST", uri: "/portrait/v1/trace", code: 200)
operation Trace {
    input := {
        @required
        scene: Scene

        @required
        perspective: Perspective

        @required
        output: Output
    }

    output := {
        @required
        base64_png: Blob

        @required
        width: Integer

        @required
        height: Integer
    }

    errors: [InvalidSceneError, RenderCapacityError]
}

/// [x, y, z], exactly three elements.
@length(min: 3, max: 3)
list Vec3 {
    member: Double
}

/// [r, g, b], exactly three channels.
@length(min: 3, max: 3)
list Color {
    member: ColorChannel
}

@range(min: 0, max: 255)
integer ColorChannel

structure Sphere {
    @required
    center: Vec3

    @required
    @range(min: 0, max: 10000)
    radius: Double

    @required
    color: Color

    @required
    @range(min: 0, max: 1000)
    specular: Double

    @required
    @range(min: 0, max: 1)
    reflective: Double
}

enum LightType {
    AMBIENT = "ambient"
    POINT = "point"
    DIRECTIONAL = "directional"
}

structure Light {
    @required
    lightType: LightType

    @required
    @range(min: 0, max: 10)
    intensity: Double

    @required
    position: Vec3
}

@length(min: 1, max: 10)
list SphereList {
    member: Sphere
}

list LightList {
    member: Light
}

structure Scene {
    backgroundColor: Color

    @range(min: 0, max: 1)
    backgroundStarProbability: Double = 0.0

    @required
    spheres: SphereList

    lights: LightList = []
}

structure Perspective {
    @required
    cameraPosition: Vec3

    @required
    cameraFocus: Vec3
}

structure Output {
    @required
    @range(min: 20, max: 1200)
    width: Integer

    @required
    @range(min: 20, max: 1200)
    height: Integer
}

/// Scene rejected by a cross-field rule the constraint traits can't express.
@error("client")
@httpError(400)
structure InvalidSceneError {
    @required
    message: String

    /// JSON-pointer path to the offending input member, in the same form
    /// ValidationException uses for the trait-expressible constraints —
    /// "/scene/spheres/0/radius" — so a client can locate the problem the
    /// same way whichever layer rejected it. Absent when the rule that
    /// failed spans members and so has no single field to name.
    field: String
}

/// The scene was valid, but the render could not be produced: it needed more
/// memory than the server would give it. Distinct from the unmodeled
/// InternalFailure 500 because the client has a specific recovery available
/// — ask for a smaller `output` — which a generic 500 cannot tell them.
///
/// 503 rather than 500: the condition is a property of the server's capacity
/// at this moment, not of the request, so the same request may well succeed
/// later. @retryable makes generated clients treat it that way.
@error("server")
@httpError(503)
@retryable
structure RenderCapacityError {
    @required
    message: String
}
