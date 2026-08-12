// Protocol binding overlay: binds the protocol-agnostic R3dr service to
// alloy#simpleRestJson (plain REST/JSON, matching the current wire format).
// Pass this file alongside model/r3dr.smithy to the generation rules.
$version: "2.0"

namespace moonbase.r3dr

use alloy#simpleRestJson

apply R3dr @simpleRestJson
