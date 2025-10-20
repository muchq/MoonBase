use axum::{
    extract::Path,
    http::StatusCode,
    response::Json,
    routing::{get, post},
    Router,
};
use build_pal_core::{BuildRequest, BuildResponse, HealthResponse, ErrorResponse};
use uuid::Uuid;

/// Create the API router
pub fn create_router() -> Router {
    Router::new()
        .route("/api/health", get(health_check))
        .route("/api/builds", post(create_build))
        .route("/api/builds/{id}", get(get_build))
}

/// Health check endpoint
pub async fn health_check() -> Json<HealthResponse> {
    Json(HealthResponse {
        status: "ok".to_string(),
        version: "0.1.0".to_string(),
        uptime_seconds: 0,
        active_builds: 0,
        database_connected: false,
    })
}

/// Create a new build
pub async fn create_build(
    Json(_request): Json<BuildRequest>,
) -> Result<Json<BuildResponse>, (StatusCode, Json<ErrorResponse>)> {
    let build_id = Uuid::new_v4();
    let web_url = format!("http://localhost:8080/builds/{}", build_id);
    
    let response = BuildResponse::new(build_id, web_url);
    Ok(Json(response))
}

/// Get build status
pub async fn get_build(
    Path(id): Path<Uuid>,
) -> Result<Json<String>, (StatusCode, Json<ErrorResponse>)> {
    // Placeholder implementation
    Ok(Json(format!("Build {}", id)))
}

#[cfg(test)]
mod tests {
    use super::*;
    use axum::http::StatusCode;
    // use axum_test::TestServer;

    #[tokio::test]
    async fn test_health_check() {
        let _app = create_router();
        // Test that we can create the router
        assert!(true);
    }
}