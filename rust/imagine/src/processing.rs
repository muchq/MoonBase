use crate::radius::Radius;
use image::{DynamicImage, GrayImage, ImageBuffer, Rgba};

pub fn gray_scale(img: &DynamicImage) -> GrayImage {
    return img.clone().into_luma8();
}

fn compute_index(row: i32, col: i32, width: i32) -> usize {
    return (row * width + col) as usize;
}

fn convolve<F>(input_pixels: &Vec<u8>, width: u32, height: u32, kernel: &[i32], scaler: F) -> Vec<i32>
where
    F: Fn(i32) -> i32,
{
    // TODO: assert that kernel.length is an odd square (3x3, 5x5, 7x7)
    // TODO: what is a GPU?
    let edge_offset = ((kernel.len() as f32).sqrt() / 2.0) as u32;
    let mut output_pixels = vec![0i32; input_pixels.len()];

    for row in edge_offset..(height - edge_offset) {
        for col in edge_offset..(width - edge_offset) {
            let mut i = 0;
            let mut dot_product = 0i32;
            for r in (-(edge_offset as i32))..=(edge_offset as i32) {
                for c in (-(edge_offset as i32))..=(edge_offset as i32) {
                    let neighbor_pixel = input_pixels
                        [compute_index(row as i32 + r, col as i32 + c, width as i32)]
                        & 0xff;
                    dot_product = dot_product + (kernel[i]) * neighbor_pixel as i32;
                    i += 1;
                }
            }

            output_pixels[compute_index(row as i32, col as i32, width as i32)] =
                scaler(dot_product);
        }
    }

    return output_pixels;
}

fn convolve_and_scale_by_kernel_sum(input: &GrayImage, kernel: &[i32]) -> GrayImage {
    let kernel_sum: i32 = kernel.iter().sum();
    let input_pixels = input.as_raw();
    let conv_output = convolve(
        input_pixels,
        input.width(),
        input.height(),
        kernel,
        |i: i32| i / kernel_sum,
    );
    let byte_output: Vec<u8> = conv_output.iter().map(|&x| x as u8).collect();
    return ImageBuffer::from_vec(input.width(), input.height(), byte_output)
        .expect("Failed to create image from vector");
}

pub fn sobel(input: &GrayImage) -> GrayImage {
    const SOBEL_X_KERNEL: &[i32] = &[1, 0, -1, 2, 0, -2, 1, 0, -1];
    const SOBEL_Y_KERNEL: &[i32] = &[1, 2, 1, 0, 0, 0, -1, -2, -1];

    let input_pixels = input.as_raw();

    let sobel_x = convolve(input_pixels, input.width(), input.height(), SOBEL_X_KERNEL, |x| x);
    let sobel_y = convolve(input_pixels, input.width(), input.height(), SOBEL_Y_KERNEL, |x| x);

    let mut output_pixels = vec![0u8; input_pixels.len()];
    for i in 0..input_pixels.len() {
        let xi = sobel_x[i];
        let yi = sobel_y[i];
        output_pixels[i] = ((255.0 * ((xi * xi + yi * yi) as f64).sqrt()) / 360.0) as u8
    }

    return ImageBuffer::from_vec(input.width(), input.height(), output_pixels)
        .expect("Failed to create image from vector");
}

pub fn gray_gaussian_blur(input: &DynamicImage, radius: Radius, depth: u8) -> GrayImage {
    // TODO: validateDepth(depth);
    let kernel = radius.gaussian_kernel();
    let mut gray_copy = gray_scale(input);

    // Don't try to use a kernel that's bigger than half the image width or height
    if gray_copy.width() / 2 < kernel.len() as u32 || gray_copy.height() / 2 < kernel.len() as u32 {
        return gray_copy;
    }

    for _i in 0..depth {
        gray_copy = convolve_and_scale_by_kernel_sum(&gray_copy, kernel);
    }
    return gray_copy;
}

pub fn fast_blur(input: &DynamicImage, sigma: f32) -> ImageBuffer<Rgba<u8>, Vec<u8>> {
    image::imageops::fast_blur(&input.clone().into_rgba8(), sigma)
}
