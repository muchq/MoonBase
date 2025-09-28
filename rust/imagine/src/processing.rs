use crate::radius::Radius;
use image::{DynamicImage, GrayImage, ImageBuffer};

pub fn gray_scale(img: &DynamicImage) -> GrayImage {
    return img.clone().into_luma8();
}

fn compute_index(row: i32, col: i32, width: i32) -> usize {
    return (row * width + col) as usize;
}

fn convolve<F>(input_pixels: &Vec<u8>, width: u32, height: u32, kernel: &[u8], scaler: F) -> Vec<u8>
where
    F: Fn(i32) -> u8,
{
    // TODO: assert that kernel.length is an odd square (3x3, 5x5, 7x7)
    // TODO: what is a GPU?
    let edge_offset = ((kernel.len() as f32).sqrt() / 2.0) as u32;
    let mut output_pixels = vec![0u8; input_pixels.len()];

    for row in edge_offset..(height - edge_offset) {
        for col in edge_offset..(width - edge_offset) {
            let mut i = 0;
            let mut dot_product = 0i32;
            for r in (-(edge_offset as i32))..=(edge_offset as i32) {
                for c in (-(edge_offset as i32))..=(edge_offset as i32) {
                    let neighbor_pixel = input_pixels
                        [compute_index(row as i32 + r, col as i32 + c, width as i32)]
                        & 0xff;
                    dot_product = dot_product + (kernel[i] as i32) * neighbor_pixel as i32;
                    i += 1;
                }
            }

            output_pixels[compute_index(row as i32, col as i32, width as i32)] =
                scaler(dot_product);
        }
    }

    return output_pixels;
}

fn convolve_and_scale_by_kernel_sum(input: &GrayImage, kernel: &[u8]) -> GrayImage {
    let kernel_sum: i32 = kernel.iter().map(|&x| x as i32).sum();
    let input_pixels = input.as_raw();
    let conv_output = convolve(
        input_pixels,
        input.width(),
        input.height(),
        kernel,
        |i: i32| (i / kernel_sum).try_into().unwrap(),
    );
    return ImageBuffer::from_vec(input.width(), input.height(), conv_output)
        .expect("Failed to create image from vector");
}

pub fn gray_gaussian_blur(input: &DynamicImage, radius: Radius, depth: u8) -> GrayImage {
    // TODO: validateDepth(depth);
    let kernel = radius.gaussian_kernel();
    let gray_copy = gray_scale(input);
    let mut blurred = gray_copy;

    // Don't try to use a kernel that's bigger than half the image width or height
    if blurred.width() / 2 < kernel.len() as u32 || blurred.height() / 2 < kernel.len() as u32 {
        return blurred;
    }

    for _i in 0..depth {
        blurred = convolve_and_scale_by_kernel_sum(&blurred, kernel);
    }
    return blurred;
}
