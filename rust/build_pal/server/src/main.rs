use build_pal_server::{create_router, BuildPalServer};
use tokio::net::TcpListener;
use tracing::{info, error};
use anyhow::Result;

#[tokio::main]
async fn main() -> Result<()> {
    tracing_subscriber::fmt::init();

    info!("Starting Build Pal Server...");

    // Create server instance
    let _server = BuildPalServer::new();

    // Create router
    let app = create_router();

    // Bind to localhost:8080
    let listener = TcpListener::bind("127.0.0.1:8080").await?;
    info!("Server listening on http://127.0.0.1:8080");

    // Start server
    if let Err(e) = axum::serve(listener, app).await {
        error!("Server error: {}", e);
        return Err(e.into());
    }

    Ok(())
}