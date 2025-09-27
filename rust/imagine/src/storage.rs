use image::{DynamicImage, GrayImage, open};

pub fn read_png(path: &str) -> DynamicImage {
    // TODO: handle read errors
    return open(path).unwrap();
}

pub fn write_png(dynamic_image: &DynamicImage, path: &str) {
    // TODO: handle write errors
    dynamic_image.save(path).unwrap();
}
pub fn write_gray_png(gray_image: &GrayImage, path: &str) {
    // TODO: handle write errors
    gray_image.save(path).unwrap();
}
