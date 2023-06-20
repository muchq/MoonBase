extern crate example_lib;

use example_lib::format_greeting;

fn main() {
    let greeting = format_greeting("world");
    println!("{}", greeting);
}
