mod doc_db;
mod model;

use mongodb::Client;
use mongodb::options::ClientOptions;
use std::time::Duration;
use tonic::transport::Server;

use crate::doc_db::DocDbService;
use doc_db_proto::doc_db::doc_db_server::DocDbServer;

#[cfg(not(test))]
#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let uri = std::env::var("MONGODB_URI").unwrap_or_else(|_| "mongodb://localhost:27017".into());
    let mut client_options = ClientOptions::parse(uri).await?;
    client_options.connect_timeout = Some(Duration::from_secs(5));

    let client = Client::with_options(client_options).expect("failed to connect");
    let service = DocDbService { client };

    let addr = "[::1]:50051".parse()?;
    let server = Server::builder()
        .add_service(DocDbServer::new(service))
        .serve(addr);

    println!("listening on {}", addr);
    server.await?;
    Ok(())
}
