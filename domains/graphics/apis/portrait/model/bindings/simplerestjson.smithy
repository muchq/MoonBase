// Protocol binding overlay: binds the protocol-agnostic Portrait service to
// alloy#simpleRestJson (plain REST/JSON, matching the current wire format).
// Pass this file alongside model/portrait.smithy to the generation rules.
$version: "2.0"

namespace moonbase.portrait

use alloy#simpleRestJson

apply Portrait @simpleRestJson
