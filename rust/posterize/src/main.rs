use axum::extract::Json as ExtractJson;
use axum::http::StatusCode;
use axum::response::Json;
use axum::routing::post;
use base64::{Engine as _, engine::general_purpose};
use image::ImageFormat;
use imagine::fast_blur;
use serde::{Deserialize, Deserializer, Serialize};
use server_pal::{listen_addr_pal, router_builder};
use std::io::Cursor;
use tracing::{Level, event};

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

#[derive(Deserialize)]
struct BlurRequest {
    #[serde(deserialize_with = "validate_png")]
    b64_png: String,
}

#[derive(Serialize)]
struct BlurResponse {
    width: u32,
    height: u32,
    format: String,
    image_data: String,
    size_bytes: usize,
}

#[derive(Serialize, Debug)]
struct ErrorResponse {
    error: String,
}

async fn blur_post(
    ExtractJson(request): ExtractJson<BlurRequest>,
) -> Result<Json<BlurResponse>, (StatusCode, Json<ErrorResponse>)> {
    let image_bytes = general_purpose::STANDARD
        .decode(&request.b64_png)
        .map_err(|_| {
            (
                StatusCode::BAD_REQUEST,
                Json(ErrorResponse {
                    error: "Invalid base64 encoding".to_string(),
                }),
            )
        })?;

    let input_png = image::load_from_memory(&image_bytes).map_err(|e| {
        (
            StatusCode::BAD_REQUEST,
            Json(ErrorResponse {
                error: format!("Invalid image format: {}", e),
            }),
        )
    })?;

    let blurred = tokio::task::spawn_blocking(move || fast_blur(&input_png, 2.0))
        .await
        .map_err(|_| {
            (
                StatusCode::INTERNAL_SERVER_ERROR,
                Json(ErrorResponse {
                    error: "Image processing failed".to_string(),
                }),
            )
        })?;

    let mut png_bytes = Vec::new();
    let mut cursor = Cursor::new(&mut png_bytes);

    blurred
        .write_to(&mut cursor, ImageFormat::Png)
        .map_err(|e| {
            (
                StatusCode::INTERNAL_SERVER_ERROR,
                Json(ErrorResponse {
                    error: format!("Failed to encode PNG: {}", e),
                }),
            )
        })?;

    let blurred_b64 = general_purpose::STANDARD.encode(&png_bytes);
    let response = BlurResponse {
        width: blurred.width(),
        height: blurred.height(),
        format: "PNG".to_string(),
        size_bytes: blurred_b64.len(),
        image_data: blurred_b64,
    };
    Ok(Json(response))
}

#[tokio::main]
async fn main() {
    tracing_subscriber::fmt::init();

    let listen_address = listen_addr_pal();

    let app = router_builder()
        .route("/v1/imagine/blur", post(blur_post))
        .build();

    let listener = tokio::net::TcpListener::bind(listen_address.clone())
        .await
        .unwrap();
    event!(Level::INFO, "listening on {}", listen_address);
    axum::serve(listener, app).await.unwrap();
}
