use std::env;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    match env::var("PROTO_PATH") {
        Ok(p) =>  tonic_build::compile_protos(p)?,
        Err(e) => println!("Oops: {}", e.to_string())
    };
    Ok(())
}
