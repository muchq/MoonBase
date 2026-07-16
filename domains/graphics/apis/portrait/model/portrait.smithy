$version: "2.0"

namespace moonbase.portrait

/// Phase 1 of the portrait-on-smithy-cpp rewrite
/// (https://github.com/muchq/MoonBase/issues/1168): the Portrait API modeled
/// in Smithy, wire-compatible with the running meerkat service — same URI,
/// same field names, tuples as JSON arrays, base64 PNG payload.
///
/// Constraint mapping from types.cc validate* rules:
///   - spheres count 1..10            -> @length on SphereList
///   - radius/specular/reflective,
///     intensity, starProbability,
///     width/height ranges            -> @range on the members
///   - Vec3/Color exactly 3 elements  -> @length on the list shapes
///   - color channel 0..255           -> @range on ColorChannel (improvement:
///                                       the current service silently wraps
///                                       values > 255 through unsigned char)
///   - light type membership          -> LightType enum validation
///   - NaN/Inf checks                 -> unreachable over standard JSON;
///                                       dropped
/// Cross-field rules stay in the Phase 2 handler (InvalidSceneError):
///   - cameraPosition != cameraFocus
///   - aspect ratio within [1/50, 50]
///   - strictly positive radius (@range is inclusive of 0)
///   - backgroundColor default [0, 0, 0] (Smithy list defaults must be empty)
service Portrait {
    version: "2026-07-16"
    operations: [Trace]
}

/// Renders a ray-traced scene and returns it as a PNG. The Blob output
/// member serializes as standard base64 in JSON — the same bytes the current
/// service produces by hand via futility's (abseil's) Base64.
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

    errors: [InvalidSceneError]
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
}
