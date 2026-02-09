fn main() -> Result<(), Box<dyn std::error::Error>> {
    tonic_prost_build::compile_protos("doc_db.proto")?;
    Ok(())
}
