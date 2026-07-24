use crate::images::{
    format_label, process_image, read_image_from_b64_string, write_image_to_b64_string,
};
use crate::types::{BlurRequest, BlurResponse, EdgesRequest, EdgesResponse, ErrorResponse};
use axum::Json;
use axum::extract::Json as ExtractJson;
use axum::http::StatusCode;
use imagine::{Radius, fast_blur, gray_gaussian_blur, sobel};

pub async fn blur_post(
    ExtractJson(request): ExtractJson<BlurRequest>,
) -> Result<Json<BlurResponse>, (StatusCode, Json<ErrorResponse>)> {
    let (input_image, input_format) = read_image_from_b64_string(&request.b64_png)?;

    let gray = request.gray;
    let sigma = request.sigma.unwrap_or(5.0);
    let blurred = process_image(input_image, move |p| {
        if gray {
            image::DynamicImage::ImageLuma8(gray_gaussian_blur(&p, Radius::Five, 3))
        } else {
            image::DynamicImage::ImageRgba8(fast_blur(&p, sigma))
        }
    })
    .await?;

    let (blurred_b64, output_format) = write_image_to_b64_string(&blurred, input_format)?;
    let response = BlurResponse {
        width: blurred.width(),
        height: blurred.height(),
        format: format_label(output_format),
        size_bytes: blurred_b64.len(),
        image_data: blurred_b64,
    };
    Ok(Json(response))
}

pub async fn edges_post(
    ExtractJson(request): ExtractJson<EdgesRequest>,
) -> Result<Json<EdgesResponse>, (StatusCode, Json<ErrorResponse>)> {
    let (input_image, input_format) = read_image_from_b64_string(&request.b64_png)?;

    let edges_img = process_image(input_image, move |p| {
        image::DynamicImage::ImageLuma8(sobel(&p.to_luma8()))
    })
    .await?;

    let (edges_b64, output_format) = write_image_to_b64_string(&edges_img, input_format)?;
    let response = EdgesResponse {
        width: edges_img.width(),
        height: edges_img.height(),
        format: format_label(output_format),
        size_bytes: edges_b64.len(),
        image_data: edges_b64,
    };
    Ok(Json(response))
}

#[cfg(test)]
mod tests {
    use super::*;
    use axum::Router;
    use axum::body::Body;
    use axum::http::Request;
    use axum::routing::post;
    use base64::Engine;
    use base64::engine::general_purpose;
    use http_body_util::BodyExt;
    use image::{DynamicImage, ImageFormat, RgbaImage};
    use std::io::Cursor;
    use tower::ServiceExt;

    fn app() -> Router {
        Router::new()
            .route("/blur", post(blur_post))
            .route("/edges", post(edges_post))
    }

    /// A small base64-encoded test image in the requested format.
    fn b64_image(format: ImageFormat) -> String {
        let img = DynamicImage::ImageRgba8(RgbaImage::from_pixel(
            8,
            8,
            image::Rgba([120, 80, 200, 255]),
        ));
        let mut bytes = Vec::new();
        let mut cursor = Cursor::new(&mut bytes);
        if format == ImageFormat::Jpeg {
            DynamicImage::ImageRgb8(img.to_rgb8())
                .write_to(&mut cursor, format)
                .unwrap();
        } else {
            img.write_to(&mut cursor, format).unwrap();
        }
        general_purpose::STANDARD.encode(&bytes)
    }

    fn json_request(uri: &str, body: String) -> Request<Body> {
        Request::builder()
            .uri(uri)
            .method("POST")
            .header("content-type", "application/json")
            .body(Body::from(body))
            .unwrap()
    }

    async fn response_json(resp: axum::response::Response) -> serde_json::Value {
        let bytes = resp.into_body().collect().await.unwrap().to_bytes();
        serde_json::from_slice(&bytes).unwrap()
    }

    const FORMAT_CASES: [(ImageFormat, &str); 5] = [
        (ImageFormat::Png, "PNG"),
        (ImageFormat::Jpeg, "JPEG"),
        (ImageFormat::Gif, "GIF"),
        (ImageFormat::Bmp, "BMP"),
        (ImageFormat::Tiff, "TIFF"),
    ];

    #[tokio::test]
    async fn blur_preserves_format_across_types() {
        // Both the color (Rgba8) and grayscale (Luma8) blur paths must encode
        // cleanly for every format; the Luma8 path in particular exercises the
        // color-type widening in `to_encodable`.
        for gray in [false, true] {
            for (format, label) in FORMAT_CASES {
                let body = format!(
                    r#"{{"b64_png":"{}","gray":{},"sigma":3.0}}"#,
                    b64_image(format),
                    gray
                );
                let resp = app().oneshot(json_request("/blur", body)).await.unwrap();
                assert_eq!(
                    resp.status(),
                    StatusCode::OK,
                    "status for {:?} gray={}",
                    format,
                    gray
                );
                let json = response_json(resp).await;
                assert_eq!(
                    json["format"], label,
                    "format for {:?} gray={}",
                    format, gray
                );
                assert!(!json["image_data"].as_str().unwrap().is_empty());
            }
        }
    }

    #[tokio::test]
    async fn edges_preserves_format_across_types() {
        // Edge detection always yields a Luma8 image, so re-encoding it as GIF
        // (which the encoder can't do without widening) is the regression this
        // guards against.
        for (format, label) in FORMAT_CASES {
            let body = format!(r#"{{"b64_png":"{}"}}"#, b64_image(format));
            let resp = app().oneshot(json_request("/edges", body)).await.unwrap();
            assert_eq!(resp.status(), StatusCode::OK, "status for {:?}", format);
            let json = response_json(resp).await;
            assert_eq!(json["format"], label, "format for {:?}", format);
        }
    }

    #[tokio::test]
    async fn blur_rejects_non_image_payload() {
        let junk = general_purpose::STANDARD.encode("not an image at all");
        let body = format!(r#"{{"b64_png":"{}","gray":false,"sigma":3.0}}"#, junk);
        let resp = app().oneshot(json_request("/blur", body)).await.unwrap();
        assert_eq!(resp.status(), StatusCode::BAD_REQUEST);
    }

    #[tokio::test]
    async fn blur_rejects_empty_payload() {
        let body = r#"{"b64_png":"","gray":false,"sigma":3.0}"#.to_string();
        let resp = app().oneshot(json_request("/blur", body)).await.unwrap();
        // The `validate_image` deserializer rejects empty input before the
        // handler runs, which axum surfaces as 422 Unprocessable Entity.
        assert_eq!(resp.status(), StatusCode::UNPROCESSABLE_ENTITY);
    }

    #[tokio::test]
    async fn blur_rejects_non_positive_sigma() {
        for sigma in ["0.0", "-1.5"] {
            let body = format!(
                r#"{{"b64_png":"{}","gray":false,"sigma":{}}}"#,
                b64_image(ImageFormat::Png),
                sigma
            );
            let resp = app().oneshot(json_request("/blur", body)).await.unwrap();
            assert_eq!(
                resp.status(),
                StatusCode::UNPROCESSABLE_ENTITY,
                "sigma={}",
                sigma
            );
        }
    }

    #[tokio::test]
    async fn blur_rejects_missing_image_field() {
        let body = r#"{"gray":false,"sigma":3.0}"#.to_string();
        let resp = app().oneshot(json_request("/blur", body)).await.unwrap();
        assert_eq!(resp.status(), StatusCode::UNPROCESSABLE_ENTITY);
    }

    #[tokio::test]
    async fn blur_rejects_malformed_json() {
        let body = r#"{"b64_png": "#.to_string();
        let resp = app().oneshot(json_request("/blur", body)).await.unwrap();
        assert_eq!(resp.status(), StatusCode::BAD_REQUEST);
    }

    #[tokio::test]
    async fn edges_rejects_invalid_base64() {
        let body = r#"{"b64_png":"not valid base64 !!!"}"#.to_string();
        let resp = app().oneshot(json_request("/edges", body)).await.unwrap();
        assert_eq!(resp.status(), StatusCode::BAD_REQUEST);
    }

    #[tokio::test]
    async fn edges_rejects_non_image_payload() {
        let junk = general_purpose::STANDARD.encode("still not an image");
        let body = format!(r#"{{"b64_png":"{}"}}"#, junk);
        let resp = app().oneshot(json_request("/edges", body)).await.unwrap();
        assert_eq!(resp.status(), StatusCode::BAD_REQUEST);
    }
}
