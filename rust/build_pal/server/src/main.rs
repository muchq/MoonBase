use build_pal_server::{create_router, BuildPalServer};
use build_pal_core::{LoggingConfig, LogLevel, BuildPalError, log_build_pal_error, log_success};
use tokio::net::TcpListener;
use tracing::{info, error};

#[tokio::main]
async fn main() -> Result<(), BuildPalError> {
    // Initialize structured logging
    let logging_config = LoggingConfig::for_component("build_pal_server")
        .with_level(LogLevel::Info)
        .with_targets();
    
    logging_config.init()
        .map_err(|e| {
            eprintln!("Failed to initialize logging: {}", e);
            e
        })?;

    info!("Starting Build Pal Server...");

    // Create server instance
    let _server = BuildPalServer::new();

    // Create router
    let app = create_router();

    // Bind to localhost:8080
    let listener = TcpListener::bind("127.0.0.1:8080").await
        .map_err(|e| {
            let error = BuildPalError::server(format!("Failed to bind to port 8080: {}", e));
            log_build_pal_error(&error, Some("server_bind"));
            error
        })?;
    
    info!("Server listening on http://127.0.0.1:8080");
    log_success("server", "startup", None);

    // Start server
    if let Err(e) = axum::serve(listener, app).await {
        let error = BuildPalError::server(format!("Server error: {}", e));
        log_build_pal_error(&error, Some("server_runtime"));
        return Err(error);
    }

    Ok(())
}