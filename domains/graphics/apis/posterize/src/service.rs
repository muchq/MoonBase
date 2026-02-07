use crate::images::{process_image, read_png_from_b64_string, write_png_to_b64_string};
use crate::types::{BlurRequest, BlurResponse, EdgesRequest, EdgesResponse, ErrorResponse};
use axum::Json;
use axum::extract::Json as ExtractJson;
use axum::http::StatusCode;
use imagine::{Radius, fast_blur, gray_gaussian_blur, sobel};

pub async fn blur_post(
    ExtractJson(request): ExtractJson<BlurRequest>,
) -> Result<Json<BlurResponse>, (StatusCode, Json<ErrorResponse>)> {
    let input_png = read_png_from_b64_string(&request.b64_png)?;

    let gray = request.gray;
    let sigma = request.sigma.unwrap_or(5.0);
    let blurred = process_image(input_png, move |p| {
        if gray {
            image::DynamicImage::ImageLuma8(gray_gaussian_blur(&p, Radius::Five, 3))
        } else {
            image::DynamicImage::ImageRgba8(fast_blur(&p, sigma))
        }
    })
    .await?;

    let blurred_b64 = write_png_to_b64_string(&blurred)?;
    let response = BlurResponse {
        width: blurred.width(),
        height: blurred.height(),
        format: "PNG".to_string(),
        size_bytes: blurred_b64.len(),
        image_data: blurred_b64,
    };
    Ok(Json(response))
}

pub async fn edges_post(
    ExtractJson(request): ExtractJson<EdgesRequest>,
) -> Result<Json<EdgesResponse>, (StatusCode, Json<ErrorResponse>)> {
    let input_png = read_png_from_b64_string(&request.b64_png)?;

    let edges_img = process_image(input_png, move |p| {
        image::DynamicImage::ImageLuma8(sobel(&p.to_luma8()))
    })
    .await?;

    let edges_b64 = write_png_to_b64_string(&edges_img)?;
    let response = EdgesResponse {
        width: edges_img.width(),
        height: edges_img.height(),
        format: "PNG".to_string(),
        size_bytes: edges_b64.len(),
        image_data: edges_b64,
    };
    Ok(Json(response))
}
