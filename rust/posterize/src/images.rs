use crate::types::ErrorResponse;
use axum::Json;
use axum::http::StatusCode;
use base64::Engine;
use base64::engine::general_purpose;
use image::{DynamicImage, ImageFormat};
use std::io::Cursor;
use std::time::Duration;
use tracing::{event, Level};

fn error_response(status_code: StatusCode, message: String) -> (StatusCode, Json<ErrorResponse>) {
    (status_code, Json(ErrorResponse { error: message }))
}

fn server_error() -> (StatusCode, Json<ErrorResponse>) {
    error_response(StatusCode::INTERNAL_SERVER_ERROR, "Server Error".to_string())
}

pub fn read_png_from_b64_string(
    b64_png: &String,
) -> Result<DynamicImage, (StatusCode, Json<ErrorResponse>)> {
    general_purpose::STANDARD
        .decode(b64_png)
        .map_err(|_| {
            error_response(
                StatusCode::BAD_REQUEST,
                "Invalid base64 encoding".to_string(),
            )
        })
        .and_then(|image_bytes| {
            image::load_from_memory(&image_bytes).map_err(|e| {
                error_response(
                    StatusCode::BAD_REQUEST,
                    format!("Invalid image format: {}", e),
                )
            })
        })
}

pub fn write_png_to_b64_string(
    png: &DynamicImage,
) -> Result<String, (StatusCode, Json<ErrorResponse>)> {
    let mut png_bytes = Vec::new();
    let mut cursor = Cursor::new(&mut png_bytes);

    png.write_to(&mut cursor, ImageFormat::Png)
        .map(|()| {
            return png_bytes;
        })
        .map_err(|e| {
            event!(Level::INFO, "Failed to encode PNG: {}", e);
            server_error()
        })
        .map(|bytes| {
            return general_purpose::STANDARD.encode(bytes);
        })
}

pub async fn process_image(
    input_png: DynamicImage,
    f: impl Fn(&DynamicImage) -> DynamicImage + Send + 'static,
) -> Result<DynamicImage, (StatusCode, Json<ErrorResponse>)> {
    tokio::time::timeout(
        Duration::from_secs(10),
        tokio::task::spawn_blocking(move || f(&input_png)),
    )
    .await
    .map_err(|_| {
        error_response(
            StatusCode::REQUEST_TIMEOUT,
            "Image processing timed out after 10 seconds".to_string(),
        )
    })?
    .map_err(|_| {
        server_error()
    })
}
