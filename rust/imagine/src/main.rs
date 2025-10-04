use imagine::{Radius, gray_gaussian_blur, gray_scale, read_png, sobel, write_gray_png};
use std::env;

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() != 2 {
        println!("usage: imagine <path to input png>");
        return;
    }
    let dyn_img = read_png(args[1].as_str());
    let gray = gray_scale(&dyn_img);
    write_gray_png(&gray, "gray.png");
    println!("wrote output to: gray.png");

    let gray_blur = gray_gaussian_blur(&dyn_img, Radius::Five, 5);
    write_gray_png(&gray_blur, "gray_blur.png");
    println!("wrote output to: gray_blur.png");

    let edges = sobel(&gray_blur);
    write_gray_png(&edges, "edges.png");
    println!("wrote output to: edges.png");
}
