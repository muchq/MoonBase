mod escapist;
mod model;

use mongodb::Client;
use tonic::transport::Server;

use crate::escapist::EscapistService;
use escapist_proto::escapist::escapist_server::EscapistServer;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let uri = std::env::var("MONGODB_URI").unwrap_or_else(|_| "mongodb://localhost:27017".into());
    let client = Client::with_uri_str(uri).await.expect("failed to connect");
    let service = EscapistService { client };

    let addr = "[::1]:50051".parse()?;
    let server = Server::builder()
        .add_service(EscapistServer::new(service))
        .serve(addr);

    println!("listening on {}", addr);
    server.await?;
    Ok(())
}
