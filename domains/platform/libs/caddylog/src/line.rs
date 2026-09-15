use std::collections::HashMap;

use serde::{Deserialize, Deserializer};

/// The slice of Caddy's JSON access log this crate reads; everything else
/// in the line is ignored on decode, and a field that is missing or `null`
/// reads as empty rather than as an error, as it does through Go's decoder
/// in the stats aggregator, because older lines carry fewer of them.
#[derive(Clone, Debug, Default, Deserialize, PartialEq)]
pub struct CaddyLine {
    /// Epoch seconds, Caddy's default log timestamp.
    #[serde(default, deserialize_with = "or_default")]
    pub ts: f64,
    /// Any integer, as Go reads it; a real line carries an HTTP status.
    #[serde(default, deserialize_with = "or_default")]
    pub status: i64,
    #[serde(default, deserialize_with = "or_default")]
    pub request: Request,
}

#[derive(Clone, Debug, Default, Deserialize, PartialEq)]
pub struct Request {
    #[serde(default, deserialize_with = "or_default")]
    pub host: String,
    #[serde(default, deserialize_with = "or_default")]
    pub method: String,
    #[serde(default, deserialize_with = "or_default")]
    pub uri: String,
    // Read through the accessors below, which know which of the two
    // address fields a line of a given age carries.
    #[serde(default, deserialize_with = "or_default")]
    client_ip: String,
    #[serde(default, deserialize_with = "or_default")]
    remote_ip: String,
    #[serde(default, deserialize_with = "or_default")]
    headers: HashMap<String, Vec<String>>,
}

fn or_default<'de, D: Deserializer<'de>, T: Default + Deserialize<'de>>(
    deserializer: D,
) -> Result<T, D::Error> {
    Option::<T>::deserialize(deserializer).map(Option::unwrap_or_default)
}

impl CaddyLine {
    /// One line of the log, without its newline. The caller bounds the
    /// line: a reader that never finds a newline hands this the rest of
    /// the file, and the stats pipeline caps a line at 1 MiB.
    pub fn parse(line: &[u8]) -> Result<Self, serde_json::Error> {
        serde_json::from_slice(line)
    }

    /// Caddy is the edge, so `client_ip` and `remote_ip` agree; older lines
    /// carry only `remote_ip`. An owned copy of what the line says: the
    /// caller decides where it goes, and nothing here keeps it.
    pub fn client_ip(&self) -> &str {
        if self.request.client_ip.is_empty() {
            &self.request.remote_ip
        } else {
            &self.request.client_ip
        }
    }

    /// The first User-Agent header, or empty.
    pub fn user_agent(&self) -> &str {
        self.request
            .headers
            .get("User-Agent")
            .and_then(|values| values.first())
            .map_or("", String::as_str)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const LINE: &str = r#"{"level":"info","ts":1789500000.25,"logger":"http.log.access","msg":"handled request","request":{"remote_ip":"1.0.0.7","remote_port":"51234","client_ip":"1.0.0.7","proto":"HTTP/2.0","method":"GET","host":"api.muchq.com","uri":"/iili/v1/r/abc?utm=x","headers":{"User-Agent":["curl/8.6.0"],"Accept":["*/*"]}},"bytes_read":0,"user_id":"","duration":0.0012,"size":0,"status":302,"resp_headers":{"Location":["https://example.com/"]}}"#;

    #[test]
    fn reads_the_fields_the_vocabulary_is_built_from() {
        let line = CaddyLine::parse(LINE.as_bytes()).unwrap();
        assert_eq!(line.ts, 1789500000.25);
        assert_eq!(line.status, 302);
        assert_eq!(line.request.host, "api.muchq.com");
        assert_eq!(line.request.method, "GET");
        assert_eq!(line.request.uri, "/iili/v1/r/abc?utm=x");
        assert_eq!(line.client_ip(), "1.0.0.7");
        assert_eq!(line.user_agent(), "curl/8.6.0");
    }

    #[test]
    fn older_lines_carry_only_remote_ip() {
        let line =
            CaddyLine::parse(br#"{"status":200,"request":{"remote_ip":"9.9.9.9","headers":{}}}"#)
                .unwrap();
        assert_eq!(line.client_ip(), "9.9.9.9");
        assert_eq!(line.user_agent(), "");
        assert_eq!(line.ts, 0.0);
    }

    #[test]
    fn client_ip_wins_over_remote_ip_when_both_are_present() {
        let line =
            CaddyLine::parse(br#"{"request":{"remote_ip":"10.0.0.1","client_ip":"1.0.0.7"}}"#)
                .unwrap();
        assert_eq!(line.client_ip(), "1.0.0.7");
    }

    // A client that sends the header twice is named by the first value,
    // the one Go's Header.Get and the stats aggregator read.
    #[test]
    fn a_repeated_user_agent_header_reads_as_its_first_value() {
        let line = CaddyLine::parse(
            br#"{"request":{"headers":{"User-Agent":["curl/8.6.0","Mozilla/5.0"]}}}"#,
        )
        .unwrap();
        assert_eq!(line.user_agent(), "curl/8.6.0");
    }

    #[test]
    fn a_line_with_no_request_is_empty_not_an_error() {
        let line = CaddyLine::parse(br#"{"status":404}"#).unwrap();
        assert_eq!(line.status, 404);
        assert_eq!(line.request, Request::default());
    }

    // Go's decoder treats null as "leave the zero value"; a line that says
    // so explicitly is counted, not skipped as corrupt.
    #[test]
    fn null_fields_read_as_empty_like_missing_ones() {
        let line = CaddyLine::parse(
            br#"{"ts":null,"status":null,"request":{"host":null,"method":null,"uri":null,"client_ip":null,"remote_ip":null,"headers":null}}"#,
        )
        .unwrap();
        assert_eq!(line, CaddyLine::default());
        let line = CaddyLine::parse(br#"{"request":null,"status":204}"#).unwrap();
        assert_eq!(line.status, 204);
        assert_eq!(line.request, Request::default());
    }

    // Any integer Go's int takes, so a line with a nonsense status is still
    // a line and not the corrupt-object alarm.
    #[test]
    fn status_is_any_integer() {
        assert_eq!(CaddyLine::parse(br#"{"status":-1}"#).unwrap().status, -1);
        assert_eq!(
            CaddyLine::parse(br#"{"status":70000}"#).unwrap().status,
            70000
        );
        assert_eq!(
            CaddyLine::parse(br#"{"ts":1789500000}"#).unwrap().ts,
            1789500000.0
        );
    }

    #[test]
    fn a_corrupt_line_is_an_error() {
        assert!(CaddyLine::parse(b"{not json").is_err());
        assert!(CaddyLine::parse(b"").is_err());
        assert!(CaddyLine::parse(br#"{"status":"200"}"#).is_err());
    }
}
