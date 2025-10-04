use serde::{Deserialize, Deserializer, Serialize};

fn validate_png<'de, D>(deserializer: D) -> Result<String, D::Error>
where
    D: Deserializer<'de>,
{
    let b64_png = String::deserialize(deserializer)?;

    if b64_png.is_empty() {
        return Err(serde::de::Error::custom("png cannot be empty"));
    }

    if b64_png.len() > 5_000_000 {
        return Err(serde::de::Error::custom("b64 png must be at most 5MiB"));
    }
    Ok(b64_png)
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
    #[serde(deserialize_with = "validate_png")]
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
    #[serde(deserialize_with = "validate_png")]
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
