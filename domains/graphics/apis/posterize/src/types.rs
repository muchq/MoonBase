use serde::{Deserialize, Deserializer, Serialize};

fn validate_image<'de, D>(deserializer: D) -> Result<String, D::Error>
where
    D: Deserializer<'de>,
{
    let b64_image = String::deserialize(deserializer)?;

    if b64_image.is_empty() {
        return Err(serde::de::Error::custom("image cannot be empty"));
    }

    if b64_image.len() > 5_000_000 {
        return Err(serde::de::Error::custom("b64 image must be at most 5MiB"));
    }
    Ok(b64_image)
}

fn validate_sigma<'de, D>(deserializer: D) -> Result<Option<f32>, D::Error>
where
    D: Deserializer<'de>,
{
    let sigma_maybe = Option::<f32>::deserialize(deserializer)?;

    if let Some(sigma) = sigma_maybe {
        if sigma <= 0.0 {
            return Err(serde::de::Error::custom("sigma must be positive"));
        }
    }

    Ok(sigma_maybe)
}

#[derive(Deserialize)]
pub struct BlurRequest {
    #[serde(deserialize_with = "validate_image")]
    pub(crate) b64_png: String,
    #[serde(deserialize_with = "validate_sigma")]
    pub(crate) sigma: Option<f32>,
    pub(crate) gray: bool,
}

#[derive(Serialize)]
pub struct BlurResponse {
    pub(crate) width: u32,
    pub(crate) height: u32,
    pub(crate) format: String,
    pub(crate) image_data: String,
    pub(crate) size_bytes: usize,
}

#[derive(Deserialize)]
pub struct EdgesRequest {
    #[serde(deserialize_with = "validate_image")]
    pub(crate) b64_png: String,
}

#[derive(Serialize)]
pub struct EdgesResponse {
    pub(crate) width: u32,
    pub(crate) height: u32,
    pub(crate) format: String,
    pub(crate) image_data: String,
    pub(crate) size_bytes: usize,
}

#[derive(Serialize, Debug)]
pub struct ErrorResponse {
    pub(crate) error: String,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn blur_request_accepts_valid_payload() {
        let json = r#"{"b64_png":"aGVsbG8=","gray":true,"sigma":4.0}"#;
        let req: BlurRequest = serde_json::from_str(json).unwrap();
        assert_eq!(req.b64_png, "aGVsbG8=");
        assert_eq!(req.sigma, Some(4.0));
        assert!(req.gray);
    }

    #[test]
    fn blur_request_allows_null_sigma() {
        let json = r#"{"b64_png":"aGVsbG8=","gray":false,"sigma":null}"#;
        let req: BlurRequest = serde_json::from_str(json).unwrap();
        assert_eq!(req.sigma, None);
    }

    #[test]
    fn blur_request_rejects_empty_image() {
        let json = r#"{"b64_png":"","gray":false,"sigma":1.0}"#;
        assert!(serde_json::from_str::<BlurRequest>(json).is_err());
    }

    #[test]
    fn blur_request_rejects_oversized_image() {
        // One byte past the 5 MB base64 cap enforced by `validate_image`.
        let big = "A".repeat(5_000_001);
        let json = format!(r#"{{"b64_png":"{}","gray":false,"sigma":1.0}}"#, big);
        assert!(serde_json::from_str::<BlurRequest>(&json).is_err());
    }

    #[test]
    fn blur_request_rejects_non_positive_sigma() {
        for sigma in ["0.0", "-2.5"] {
            let json = format!(r#"{{"b64_png":"aGVsbG8=","gray":false,"sigma":{}}}"#, sigma);
            assert!(
                serde_json::from_str::<BlurRequest>(&json).is_err(),
                "sigma={}",
                sigma
            );
        }
    }

    #[test]
    fn blur_request_requires_image_field() {
        let json = r#"{"gray":false,"sigma":1.0}"#;
        assert!(serde_json::from_str::<BlurRequest>(json).is_err());
    }

    #[test]
    fn edges_request_rejects_empty_image() {
        let json = r#"{"b64_png":""}"#;
        assert!(serde_json::from_str::<EdgesRequest>(json).is_err());
    }
}
