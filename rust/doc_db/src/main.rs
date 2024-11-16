mod doc_db;
mod model;

use mongodb::Client;
use tonic::transport::Server;

use crate::doc_db::DocDbService;
use doc_db_proto::doc_db::doc_db_server::DocDbServer;

#[cfg(not(test))]
#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let uri = std::env::var("MONGODB_URI").unwrap_or_else(|_| "mongodb://localhost:27017".into());
    let client = Client::with_uri_str(uri).await.expect("failed to connect");
    let service = DocDbService { client };

    let addr = "[::1]:50051".parse()?;
    let server = Server::builder()
        .add_service(DocDbServer::new(service))
        .serve(addr);

    println!("listening on {}", addr);
    server.await?;
    Ok(())
}
