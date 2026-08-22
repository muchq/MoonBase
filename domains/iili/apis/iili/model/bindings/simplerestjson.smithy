// Protocol binding overlay: binds the protocol-agnostic Iili service to
// alloy#simpleRestJson (plain REST/JSON). Passed alongside model/iili.smithy
// to the generation rules.
$version: "2.0"

namespace moonbase.iili

use alloy#simpleRestJson

apply Iili @simpleRestJson
