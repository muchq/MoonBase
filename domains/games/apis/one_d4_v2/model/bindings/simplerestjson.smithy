// Protocol binding overlay: binds the protocol-agnostic OneD4V2 service to
// alloy#simpleRestJson (plain REST/JSON, matching /v1/analyze's wire format).
$version: "2.0"

namespace moonbase.one_d4

use alloy#simpleRestJson

apply OneD4V2 @simpleRestJson
