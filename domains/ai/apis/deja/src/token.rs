//! One request, one token: the bounded factors caddylog reads, spelled as
//! one string, and the vocabulary that gives each distinct string an id.

use std::collections::HashMap;

use caddylog::{CaddyLine, agent_of, bounded_method, probe_of, route_of, site_of};

/// The id of every token the vocabulary had no room for.
pub const UNK: u16 = 0;
/// The token before a client's first request.
pub const BOS: u16 = 1;
/// How many distinct tokens the vocabulary holds before the rest read as
/// `<unk>`. The factors are bounded, but their product is not small, and a
/// client that walks every route with every method can fill the table; a
/// vocabulary at its cap is itself the signal, and `<unk>` is what the
/// model says about everything after.
pub const DEFAULT_CAP: usize = 2048;

/// `site method route status agent_class[ probe]`, every factor bounded
/// upstream, so a scanner's path or a spoofed User-Agent cannot mint a
/// token of its own.
pub fn token_text(line: &CaddyLine) -> String {
    let (class, _) = agent_of(line.user_agent());
    let mut text = format!(
        "{} {} {} {} {}",
        site_of(&line.request.host),
        bounded_method(&line.request.method),
        route_of(&line.request.host, &line.request.uri),
        line.status,
        class.as_str(),
    );
    if let Some(probe) = probe_of(&line.request.uri) {
        text.push(' ');
        text.push_str(probe);
    }
    text
}

/// Token ids by first sight, capped. `names[id]` is the token's text; the
/// two reserved ids come first.
#[derive(Clone, Debug)]
pub struct Vocab {
    names: Vec<String>,
    cap: usize,
    ids: HashMap<String, u16>,
}

impl Vocab {
    pub fn new(cap: usize) -> Self {
        Self::from_names(vec!["<unk>".into(), "<bos>".into()], cap)
    }

    /// The names in id order, as a checkpoint carries them; the index is
    /// rebuilt from them.
    pub fn from_names(names: Vec<String>, cap: usize) -> Self {
        let ids = names
            .iter()
            .enumerate()
            .map(|(id, name)| (name.clone(), id as u16))
            .collect();
        Self { names, cap, ids }
    }

    /// The token's id and whether this is its first sight. A new token past
    /// the cap is `UNK`, never novel: the vocabulary is full and says so.
    pub fn intern(&mut self, text: &str) -> (u16, bool) {
        if let Some(&id) = self.ids.get(text) {
            return (id, false);
        }
        if self.names.len() >= self.cap {
            return (UNK, false);
        }
        let id = self.names.len() as u16;
        self.names.push(text.to_string());
        self.ids.insert(text.to_string(), id);
        (id, true)
    }

    pub fn name(&self, id: u16) -> &str {
        &self.names[usize::from(id)]
    }

    pub fn len(&self) -> usize {
        self.names.len()
    }

    pub fn cap(&self) -> usize {
        self.cap
    }

    pub fn names(&self) -> &[String] {
        &self.names
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn line(json: &str) -> CaddyLine {
        CaddyLine::parse(json.as_bytes()).unwrap()
    }

    #[test]
    fn a_token_is_the_bounded_factors_in_order() {
        let l = line(
            r#"{"status":302,"request":{"host":"api.muchq.com","method":"GET","uri":"/iili/v1/r/abc?x=1","headers":{"User-Agent":["Mozilla/5.0 (X11) Chrome/120"]}}}"#,
        );
        assert_eq!(token_text(&l), "api.muchq.com GET /iili/v1/r/* 302 browser");
    }

    #[test]
    fn a_probe_is_a_sixth_factor_and_the_unbounded_parts_never_appear() {
        let l = line(
            r#"{"status":404,"request":{"host":"evil.example:443","method":"PROPFIND","uri":"/wp-login.php?user=<script>","headers":{"User-Agent":["k7xq3zbot/1.0"]}}}"#,
        );
        let text = token_text(&l);
        assert_eq!(text, "other CUSTOM other 404 bot wordpress");
        assert!(!text.contains("k7xq3z") && !text.contains("script"));
    }

    #[test]
    fn ids_are_assigned_by_first_sight_and_the_reserved_two_come_first() {
        let mut vocab = Vocab::new(DEFAULT_CAP);
        assert_eq!(vocab.name(UNK), "<unk>");
        assert_eq!(vocab.name(BOS), "<bos>");
        assert_eq!(vocab.intern("a"), (2, true));
        assert_eq!(vocab.intern("b"), (3, true));
        assert_eq!(vocab.intern("a"), (2, false));
        assert_eq!(vocab.len(), 4);
        assert_eq!(vocab.name(3), "b");
    }

    #[test]
    fn a_full_vocabulary_reads_new_tokens_as_unk_and_not_as_novel() {
        let mut vocab = Vocab::new(3);
        assert_eq!(vocab.intern("a"), (2, true));
        assert_eq!(vocab.intern("b"), (UNK, false));
        assert_eq!(vocab.intern("b"), (UNK, false));
        assert_eq!(vocab.intern("a"), (2, false));
        assert_eq!(vocab.len(), 3);
    }

    #[test]
    fn from_names_resumes_the_ids_without_minting_them_again() {
        let mut vocab = Vocab::new(DEFAULT_CAP);
        vocab.intern("a");
        vocab.intern("b");
        let mut restored = Vocab::from_names(vocab.names().to_vec(), DEFAULT_CAP);
        assert_eq!(restored.intern("b"), (3, false));
        assert_eq!(restored.intern("c"), (4, true));
        assert_eq!(restored.cap(), DEFAULT_CAP);
    }
}
