use base64ct::{Base64, Encoding};
use sha2::{Digest, Sha256};

pub fn compute_sha(sorted_word_list: &[String]) -> String {
    let mut hasher = Sha256::new();
    for w in sorted_word_list.iter() {
        Digest::update(&mut hasher, w);
    }

    let sha256 = hasher.finalize();
    let sha_string = Base64::encode_string(&sha256);
    sha_string.replace("/", "_").replace("=", "_")
}
