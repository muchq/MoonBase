// Protocol binding overlay: binds the protocol-agnostic R3drV2 service to
// alloy#simpleRestJson (plain REST/JSON). Passed alongside model/r3dr_v2.smithy
// to the generation rules.
$version: "2.0"

namespace moonbase.r3dr

use alloy#simpleRestJson

apply R3drV2 @simpleRestJson
