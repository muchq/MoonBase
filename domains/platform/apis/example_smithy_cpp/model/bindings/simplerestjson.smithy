// Protocol binding overlay: binds the protocol-agnostic Greeter service to
// alloy#simpleRestJson (plain REST/JSON — the protocol portrait would use).
// Pass this file alongside model/greeter.smithy to the generation rules.
$version: "2.0"

namespace moonbase.greeter

use alloy#simpleRestJson

apply Greeter @simpleRestJson
