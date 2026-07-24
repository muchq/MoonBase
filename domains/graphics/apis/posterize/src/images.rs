use crate::types::ErrorResponse;
use axum::Json;
use axum::http::StatusCode;
use base64::Engine;
use base64::engine::general_purpose;
use image::{DynamicImage, ImageFormat};
use std::io::Cursor;
use std::time::Duration;
use tracing::{Level, event};

fn error_response(status_code: StatusCode, message: String) -> (StatusCode, Json<ErrorResponse>) {
    (status_code, Json(ErrorResponse { error: message }))
}

fn bad_request(message: String) -> (StatusCode, Json<ErrorResponse>) {
    error_response(StatusCode::BAD_REQUEST, message)
}

fn server_error() -> (StatusCode, Json<ErrorResponse>) {
    error_response(
        StatusCode::INTERNAL_SERVER_ERROR,
        "Server Error".to_string(),
    )
}

/// Human-readable label for a format, reported back in API responses.
pub fn format_label(format: ImageFormat) -> String {
    match format {
        ImageFormat::Png => "PNG",
        ImageFormat::Jpeg => "JPEG",
        ImageFormat::Gif => "GIF",
        ImageFormat::WebP => "WEBP",
        ImageFormat::Bmp => "BMP",
        ImageFormat::Tiff => "TIFF",
        ImageFormat::Ico => "ICO",
        _ => "PNG",
    }
    .to_string()
}

/// Pick the format to encode the result in. We preserve the input format when
/// we have a reliable encoder for it; formats we can decode but not re-encode
/// dependably (e.g. WebP, ICO) round-trip out as PNG.
fn output_format(input: ImageFormat) -> ImageFormat {
    match input {
        ImageFormat::Png
        | ImageFormat::Jpeg
        | ImageFormat::Gif
        | ImageFormat::Bmp
        | ImageFormat::Tiff => input,
        _ => ImageFormat::Png,
    }
}

/// Cap on memory a single decode may allocate. Guards against decompression
/// bombs: a small payload (input is already limited to ~3.75 MiB decoded) can
/// still declare enormous dimensions, and enabling TIFF/GIF/BMP widens that
/// surface. 256 MiB comfortably fits any legitimately-sized image.
const MAX_DECODE_ALLOC_BYTES: u64 = 256 * 1024 * 1024;

/// Decode a base64-encoded image, auto-detecting the format from its magic
/// bytes. Returns the decoded image alongside the detected input format so the
/// caller can preserve it on the way out.
pub fn read_image_from_b64_string(
    b64_image: &str,
) -> Result<(DynamicImage, ImageFormat), (StatusCode, Json<ErrorResponse>)> {
    let image_bytes = general_purpose::STANDARD
        .decode(b64_image)
        .map_err(|_| bad_request("Invalid base64 encoding".to_string()))?;

    let format = image::guess_format(&image_bytes)
        .map_err(|_| bad_request("Unsupported or unrecognized image format".to_string()))?;

    let mut reader = image::ImageReader::new(Cursor::new(&image_bytes));
    reader.set_format(format);
    let mut limits = image::Limits::default();
    limits.max_alloc = Some(MAX_DECODE_ALLOC_BYTES);
    reader.limits(limits);

    let image = reader
        .decode()
        .map_err(|e| bad_request(format!("Invalid image data: {}", e)))?;

    Ok((image, format))
}

/// Convert an image to a color type the target format's encoder accepts.
/// `DynamicImage::write_to` does not adapt color types for every encoder, so
/// grayscale (Luma8) buffers produced by the edge/gray-blur pipeline must be
/// widened for formats whose encoders only speak RGB(A).
fn to_encodable(image: &DynamicImage, format: ImageFormat) -> DynamicImage {
    match format {
        // JPEG has no alpha channel; flatten to RGB.
        ImageFormat::Jpeg => DynamicImage::ImageRgb8(image.to_rgb8()),
        // The GIF encoder only accepts Rgb8/Rgba8 and performs no conversion of
        // its own, so a Luma8 buffer would otherwise error out.
        ImageFormat::Gif => DynamicImage::ImageRgba8(image.to_rgba8()),
        // PNG, BMP, and TIFF encoders handle Luma8 and Rgba8 directly.
        _ => image.clone(),
    }
}

/// Encode an image back to a base64 string, preferring `format`. Returns the
/// encoded string and the format actually used, which may differ from `format`
/// when the input format has no reliable encoder.
pub fn write_image_to_b64_string(
    image: &DynamicImage,
    format: ImageFormat,
) -> Result<(String, ImageFormat), (StatusCode, Json<ErrorResponse>)> {
    let out_format = output_format(format);
    let mut image_bytes = Vec::new();
    let mut cursor = Cursor::new(&mut image_bytes);

    to_encodable(image, out_format)
        .write_to(&mut cursor, out_format)
        .map_err(|e| {
            event!(
                Level::INFO,
                "Failed to encode {}: {}",
                format_label(out_format),
                e
            );
            server_error()
        })?;

    Ok((general_purpose::STANDARD.encode(&image_bytes), out_format))
}

pub async fn process_image(
    input_image: DynamicImage,
    f: impl Fn(&DynamicImage) -> DynamicImage + Send + 'static,
) -> Result<DynamicImage, (StatusCode, Json<ErrorResponse>)> {
    tokio::time::timeout(
        Duration::from_secs(10),
        tokio::task::spawn_blocking(move || f(&input_image)),
    )
    .await
    .map_err(|_| {
        error_response(
            StatusCode::REQUEST_TIMEOUT,
            "Image processing timed out after 10 seconds".to_string(),
        )
    })?
    .map_err(|_| server_error())
}

#[cfg(test)]
mod tests {
    use super::*;
    use image::{ImageFormat, RgbImage, RgbaImage};

    /// Encode a small test image to a base64 string in the given format.
    fn b64_image(format: ImageFormat) -> String {
        let img = DynamicImage::ImageRgba8(RgbaImage::from_pixel(
            8,
            8,
            image::Rgba([120, 80, 200, 255]),
        ));
        let mut bytes = Vec::new();
        let mut cursor = Cursor::new(&mut bytes);
        // JPEG can't hold alpha; feed it RGB.
        if format == ImageFormat::Jpeg {
            DynamicImage::ImageRgb8(img.to_rgb8())
                .write_to(&mut cursor, format)
                .unwrap();
        } else {
            img.write_to(&mut cursor, format).unwrap();
        }
        general_purpose::STANDARD.encode(&bytes)
    }

    #[test]
    fn reads_common_formats_and_reports_their_format() {
        for format in [
            ImageFormat::Png,
            ImageFormat::Jpeg,
            ImageFormat::Gif,
            ImageFormat::Bmp,
            ImageFormat::Tiff,
        ] {
            let (img, detected) = read_image_from_b64_string(&b64_image(format))
                .unwrap_or_else(|_| panic!("failed to read {:?}", format));
            assert_eq!(detected, format, "format detection for {:?}", format);
            assert_eq!(img.width(), 8);
            assert_eq!(img.height(), 8);
        }
    }

    #[test]
    fn round_trips_preserve_input_format() {
        for format in [
            ImageFormat::Png,
            ImageFormat::Jpeg,
            ImageFormat::Gif,
            ImageFormat::Bmp,
            ImageFormat::Tiff,
        ] {
            let (img, detected) = read_image_from_b64_string(&b64_image(format)).unwrap();
            let (encoded, out_format) = write_image_to_b64_string(&img, detected).unwrap();
            assert_eq!(out_format, format, "output format for {:?}", format);
            // The encoded output must be decodable and detected as the same format.
            let (_, re_detected) = read_image_from_b64_string(&encoded).unwrap();
            assert_eq!(re_detected, format, "re-detected format for {:?}", format);
        }
    }

    #[test]
    fn jpeg_encodes_rgba_input_without_error() {
        // The blur pipeline can hand back an RGBA image; encoding as JPEG must
        // flatten the alpha channel rather than fail.
        let rgba =
            DynamicImage::ImageRgba8(RgbaImage::from_pixel(4, 4, image::Rgba([10, 20, 30, 128])));
        let (encoded, out_format) = write_image_to_b64_string(&rgba, ImageFormat::Jpeg).unwrap();
        assert_eq!(out_format, ImageFormat::Jpeg);
        assert!(!encoded.is_empty());
    }

    #[test]
    fn webp_input_falls_back_to_png_output() {
        // WebP has no reliable encoder here, so a WebP round-trip should still
        // succeed by emitting PNG.
        let rgb = DynamicImage::ImageRgb8(RgbImage::from_pixel(4, 4, image::Rgb([1, 2, 3])));
        let (encoded, out_format) = write_image_to_b64_string(&rgb, ImageFormat::WebP).unwrap();
        assert_eq!(out_format, ImageFormat::Png);
        let (_, detected) = read_image_from_b64_string(&encoded).unwrap();
        assert_eq!(detected, ImageFormat::Png);
    }

    #[test]
    fn rejects_invalid_base64() {
        let err = read_image_from_b64_string("not base64!!!").unwrap_err();
        assert_eq!(err.0, StatusCode::BAD_REQUEST);
    }

    #[test]
    fn rejects_non_image_bytes() {
        let junk = general_purpose::STANDARD.encode("this is definitely not an image");
        let err = read_image_from_b64_string(&junk).unwrap_err();
        assert_eq!(err.0, StatusCode::BAD_REQUEST);
    }
}
