use axum::{
    extract::{Path, State},
    http::{StatusCode, Method},
    response::{Json, Sse, sse::Event},
    routing::{get, post},
    Router,
};
use tower_http::cors::{CorsLayer, Any};
use futures::stream::{Stream, StreamExt};
use std::convert::Infallible;
use build_pal_core::{
    BuildRequest, BuildResponse, HealthResponse, ErrorResponse,
    BuildStatusResponse, ListProjectsResponse, ProjectHistoryResponse
};
use uuid::Uuid;
use std::sync::Arc;
use tracing::{info, error, warn};


use crate::BuildPalServer;

/// Create the API router with shared state
pub fn create_router() -> Router {
    let server = Arc::new(BuildPalServer::new());

    // Configure CORS to allow web UI access
    let cors = CorsLayer::new()
        .allow_origin(Any)
        .allow_methods([Method::GET, Method::POST, Method::PUT, Method::DELETE])
        .allow_headers(Any);

    Router::new()
        .route("/api/health", get(health_check))
        .route("/api/projects", get(list_projects))
        .route("/api/projects/{id}/history", get(get_project_history))
        .route("/api/builds", post(create_build))
        .route("/api/builds/{id}", get(get_build))
        .route("/api/builds/{id}/logs", get(get_build_logs))
        .layer(cors)
        .with_state(server)
}

/// Health check endpoint
pub async fn health_check(
    State(server): State<Arc<BuildPalServer>>,
) -> Json<HealthResponse> {
    let active_builds = server.get_active_build_count().await;

    Json(HealthResponse {
        status: "ok".to_string(),
        version: "0.1.0".to_string(),
        uptime_seconds: server.get_uptime_seconds(),
        active_builds,
        database_connected: false, // TODO: Implement database connectivity check
    })
}

/// List all projects
pub async fn list_projects(
    State(server): State<Arc<BuildPalServer>>,
) -> Json<ListProjectsResponse> {
    info!("Retrieving projects list");

    let projects = server.list_projects().await;

    Json(ListProjectsResponse {
        projects,
        total: 0, // TODO: Implement actual project count from database
        has_more: false,
    })
}

/// Get project build history
pub async fn get_project_history(
    State(server): State<Arc<BuildPalServer>>,
    Path(id): Path<Uuid>,
) -> Result<Json<ProjectHistoryResponse>, (StatusCode, Json<ErrorResponse>)> {
    info!("Retrieving build history for project: {}", id);

    let builds = server.get_project_builds(id).await;

    Ok(Json(ProjectHistoryResponse {
        builds,
        total: 0, // TODO: Implement pagination
        has_more: false,
    }))
}

/// Create a new build
pub async fn create_build(
    State(server): State<Arc<BuildPalServer>>,
    Json(request): Json<BuildRequest>,
) -> Result<(StatusCode, Json<BuildResponse>), (StatusCode, Json<ErrorResponse>)> {
    info!("Received build request for command: {}", request.command);
    
    // Validate the build request
    if request.command.trim().is_empty() {
        warn!("Build request rejected: empty command");
        return Err((
            StatusCode::BAD_REQUEST,
            Json(ErrorResponse {
                error: "Command cannot be empty".to_string(),
                code: "INVALID_COMMAND".to_string(),
                details: None,
            }),
        ));
    }

    if request.project_path.trim().is_empty() {
        warn!("Build request rejected: empty project path");
        return Err((
            StatusCode::BAD_REQUEST,
            Json(ErrorResponse {
                error: "Project path cannot be empty".to_string(),
                code: "INVALID_PROJECT_PATH".to_string(),
                details: None,
            }),
        ));
    }

    // Submit the build to the server
    match server.submit_build(request).await {
        Ok(build_id) => {
            let web_url = format!("http://localhost:8080/builds/{}", build_id);
            info!("Build {} created successfully", build_id);
            
            let response = BuildResponse::new(build_id, web_url);
            Ok((StatusCode::CREATED, Json(response)))
        }
        Err(e) => {
            error!("Failed to create build: {}", e);
            Err((
                StatusCode::INTERNAL_SERVER_ERROR,
                Json(ErrorResponse {
                    error: "Failed to create build".to_string(),
                    code: "BUILD_CREATION_FAILED".to_string(),
                    details: Some(serde_json::json!({ "error": e.to_string() })),
                }),
            ))
        }
    }
}

/// Get build status
pub async fn get_build(
    State(server): State<Arc<BuildPalServer>>,
    Path(id): Path<Uuid>,
) -> Result<Json<BuildStatusResponse>, (StatusCode, Json<ErrorResponse>)> {
    info!("Retrieving build status for ID: {}", id);
    
    match server.get_build(id).await {
        Some(build) => {
            let web_url = format!("http://localhost:8080/builds/{}", id);
            let response = BuildStatusResponse {
                build,
                logs_available: true, // TODO: Check actual log availability
                web_url,
            };
            Ok(Json(response))
        }
        None => {
            warn!("Build not found: {}", id);
            Err((
                StatusCode::NOT_FOUND,
                Json(ErrorResponse {
                    error: "Build not found".to_string(),
                    code: "BUILD_NOT_FOUND".to_string(),
                    details: Some(serde_json::json!({ "build_id": id })),
                }),
            ))
        }
    }
}

/// Get build logs
pub async fn get_build_logs(
    State(server): State<Arc<BuildPalServer>>,
    Path(id): Path<Uuid>,
) -> Result<Json<serde_json::Value>, (StatusCode, Json<ErrorResponse>)> {
    info!("Retrieving logs for build ID: {}", id);

    // First check if the build exists
    if server.get_build(id).await.is_none() {
        warn!("Build not found: {}", id);
        return Err((
            StatusCode::NOT_FOUND,
            Json(ErrorResponse {
                error: "Build not found".to_string(),
                code: "BUILD_NOT_FOUND".to_string(),
                details: Some(serde_json::json!({ "build_id": id })),
            }),
        ));
    }

    // Get structured logs from the log manager
    let log_manager = server.get_log_manager();
    match log_manager.get_build_logs(id).await {
        Some(log_entries) => {
            let response = serde_json::json!({
                "build_id": id,
                "logs": log_entries,
                "log_count": log_entries.len(),
                "retrieved_at": chrono::Utc::now()
            });
            Ok(Json(response))
        }
        None => {
            warn!("No logs found for build: {}", id);
            Err((
                StatusCode::NOT_FOUND,
                Json(ErrorResponse {
                    error: "No logs found for build".to_string(),
                    code: "LOGS_NOT_FOUND".to_string(),
                    details: Some(serde_json::json!({ "build_id": id })),
                }),
            ))
        }
    }
}

/// Stream build logs in real-time using Server-Sent Events
pub async fn stream_build_logs(
    State(server): State<Arc<BuildPalServer>>,
    Path(id): Path<Uuid>,
) -> Result<Sse<impl Stream<Item = Result<Event, Infallible>>>, (StatusCode, Json<ErrorResponse>)> {
    info!("Starting log stream for build ID: {}", id);
    
    // First check if the build exists
    if server.get_build(id).await.is_none() {
        warn!("Build not found: {}", id);
        return Err((
            StatusCode::NOT_FOUND,
            Json(ErrorResponse {
                error: "Build not found".to_string(),
                code: "BUILD_NOT_FOUND".to_string(),
                details: Some(serde_json::json!({ "build_id": id })),
            }),
        ));
    }

    // Get the log manager from the execution engine and move it into the stream
    let log_manager = server.get_log_manager();
    
    // Initialize logs for the build if they don't exist yet (this is idempotent)
    let _ = log_manager.initialize_build_logs(id).await;
    
    // Create the stream that owns the log_manager
    let stream = async_stream::stream! {
        // Create a log stream inside the async stream
        match log_manager.create_log_stream(id).await {
            Ok(mut log_stream) => {
                while let Some(log_entry) = log_stream.next().await {
                    let event_data = serde_json::json!({
                        "line_number": log_entry.line_number,
                        "timestamp": log_entry.timestamp,
                        "content": log_entry.content,
                        "stream_type": match log_entry.stream_type {
                            crate::logs::LogStreamType::Stdout => "stdout",
                            crate::logs::LogStreamType::Stderr => "stderr",
                            crate::logs::LogStreamType::System => "system",
                        }
                    });
                    
                    yield Ok(Event::default()
                        .event("log")
                        .data(event_data.to_string()));
                }
                
                // Send end-of-stream event
                yield Ok(Event::default()
                    .event("end")
                    .data("{}"));
            }
            Err(e) => {
                // Send error event if stream creation fails
                let error_data = serde_json::json!({
                    "error": e.to_string()
                });
                yield Ok(Event::default()
                    .event("error")
                    .data(error_data.to_string()));
            }
        }
    };
    
    Ok(Sse::new(stream))
}

#[cfg(test)]
mod tests {
    use super::*;
    use axum::{
        body::Body,
        http::{Request, StatusCode},
    };
    use tower::ServiceExt;
    use build_pal_core::{CLIConfig, BuildTool, RetentionPolicy, ExecutionMode, Environment, BuildStatus};

    #[tokio::test]
    async fn test_health_check_endpoint() {
        let app = create_router();

        let response = app
            .oneshot(
                Request::builder()
                    .uri("/api/health")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::OK);

        let body = axum::body::to_bytes(response.into_body(), usize::MAX).await.unwrap();
        let health_response: HealthResponse = serde_json::from_slice(&body).unwrap();
        
        assert_eq!(health_response.status, "ok");
        assert_eq!(health_response.version, "0.1.0");
        assert!(!health_response.database_connected); // Should be false for now
    }

    #[tokio::test]
    async fn test_create_build_success() {
        let app = create_router();

        let config = CLIConfig {
            tool: BuildTool::Bazel,
            name: "test-project".to_string(),
            description: None,
            mode: ExecutionMode::Async,
            retention: RetentionPolicy::All,
            retention_duration_days: Some(7),
            environment: Environment::Native,
            parsing: None,
            docker: None,
            ai: None,
        };

        let build_request = BuildRequest::new(
            "/path/to/project".to_string(),
            "bazel build //...".to_string(),
            config,
            "test".to_string(),
        );

        let request_body = serde_json::to_string(&build_request).unwrap();

        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/builds")
                    .header("content-type", "application/json")
                    .body(Body::from(request_body))
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::CREATED);

        let body = axum::body::to_bytes(response.into_body(), usize::MAX).await.unwrap();
        let build_response: BuildResponse = serde_json::from_slice(&body).unwrap();
        
        assert!(!build_response.build_id.is_nil());
        assert!(build_response.web_url.contains(&build_response.build_id.to_string()));
        assert_eq!(build_response.status, "created");
    }

    #[tokio::test]
    async fn test_create_build_empty_command() {
        let app = create_router();

        let config = CLIConfig {
            tool: BuildTool::Bazel,
            name: "test-project".to_string(),
            description: None,
            mode: ExecutionMode::Async,
            retention: RetentionPolicy::All,
            retention_duration_days: Some(7),
            environment: Environment::Native,
            parsing: None,
            docker: None,
            ai: None,
        };

        let build_request = BuildRequest::new(
            "/path/to/project".to_string(),
            "".to_string(), // Empty command
            config,
            "test".to_string(),
        );

        let request_body = serde_json::to_string(&build_request).unwrap();

        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/builds")
                    .header("content-type", "application/json")
                    .body(Body::from(request_body))
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::BAD_REQUEST);

        let body = axum::body::to_bytes(response.into_body(), usize::MAX).await.unwrap();
        let error_response: ErrorResponse = serde_json::from_slice(&body).unwrap();
        
        assert_eq!(error_response.code, "INVALID_COMMAND");
        assert!(error_response.error.contains("Command cannot be empty"));
    }

    #[tokio::test]
    async fn test_create_build_empty_project_path() {
        let app = create_router();

        let config = CLIConfig {
            tool: BuildTool::Maven,
            name: "test-project".to_string(),
            description: None,
            mode: ExecutionMode::Sync,
            retention: RetentionPolicy::Error,
            retention_duration_days: Some(14),
            environment: Environment::Native,
            parsing: None,
            docker: None,
            ai: None,
        };

        let build_request = BuildRequest::new(
            "".to_string(), // Empty project path
            "mvn compile".to_string(),
            config,
            "test".to_string(),
        );

        let request_body = serde_json::to_string(&build_request).unwrap();

        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/builds")
                    .header("content-type", "application/json")
                    .body(Body::from(request_body))
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::BAD_REQUEST);

        let body = axum::body::to_bytes(response.into_body(), usize::MAX).await.unwrap();
        let error_response: ErrorResponse = serde_json::from_slice(&body).unwrap();
        
        assert_eq!(error_response.code, "INVALID_PROJECT_PATH");
        assert!(error_response.error.contains("Project path cannot be empty"));
    }

    #[tokio::test]
    async fn test_get_build_success() {
        let app = create_router();

        // First create a build
        let config = CLIConfig {
            tool: BuildTool::Gradle,
            name: "test-project".to_string(),
            description: None,
            mode: ExecutionMode::Async,
            retention: RetentionPolicy::All,
            retention_duration_days: Some(7),
            environment: Environment::Native,
            parsing: None,
            docker: None,
            ai: None,
        };

        let build_request = BuildRequest::new(
            "/path/to/project".to_string(),
            "gradle build".to_string(),
            config,
            "test".to_string(),
        );

        let request_body = serde_json::to_string(&build_request).unwrap();

        let create_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/builds")
                    .header("content-type", "application/json")
                    .body(Body::from(request_body))
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(create_response.status(), StatusCode::CREATED);

        let body = axum::body::to_bytes(create_response.into_body(), usize::MAX).await.unwrap();
        let build_response: BuildResponse = serde_json::from_slice(&body).unwrap();
        let build_id = build_response.build_id;

        // Now get the build
        let get_response = app
            .oneshot(
                Request::builder()
                    .uri(&format!("/api/builds/{}", build_id))
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(get_response.status(), StatusCode::OK);

        let body = axum::body::to_bytes(get_response.into_body(), usize::MAX).await.unwrap();
        let status_response: BuildStatusResponse = serde_json::from_slice(&body).unwrap();
        
        assert_eq!(status_response.build.id, build_id);
        assert_eq!(status_response.build.command, "gradle build");
        assert_eq!(status_response.build.status, BuildStatus::Queued);
        assert!(status_response.logs_available);
        assert!(status_response.web_url.contains(&build_id.to_string()));
    }

    #[tokio::test]
    async fn test_get_build_not_found() {
        let app = create_router();
        let non_existent_id = Uuid::new_v4();

        let response = app
            .oneshot(
                Request::builder()
                    .uri(&format!("/api/builds/{}", non_existent_id))
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::NOT_FOUND);

        let body = axum::body::to_bytes(response.into_body(), usize::MAX).await.unwrap();
        let error_response: ErrorResponse = serde_json::from_slice(&body).unwrap();
        
        assert_eq!(error_response.code, "BUILD_NOT_FOUND");
        assert!(error_response.error.contains("Build not found"));
    }

    #[tokio::test]
    async fn test_get_build_invalid_uuid() {
        let app = create_router();

        let response = app
            .oneshot(
                Request::builder()
                    .uri("/api/builds/invalid-uuid")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();

        // Axum should return 400 for invalid UUID in path parameter
        assert_eq!(response.status(), StatusCode::BAD_REQUEST);
    }

    #[tokio::test]
    async fn test_get_build_logs_no_logs_yet() {
        let app = create_router();

        // First create a build
        let config = CLIConfig {
            tool: BuildTool::Bazel,
            name: "test-project".to_string(),
            description: None,
            mode: ExecutionMode::Async,
            retention: RetentionPolicy::All,
            retention_duration_days: Some(7),
            environment: Environment::Native,
            parsing: None,
            docker: None,
            ai: None,
        };

        let build_request = BuildRequest::new(
            "/path/to/project".to_string(),
            "echo test logs".to_string(),
            config,
            "test".to_string(),
        );

        let request_body = serde_json::to_string(&build_request).unwrap();

        let create_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/builds")
                    .header("content-type", "application/json")
                    .body(Body::from(request_body))
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(create_response.status(), StatusCode::CREATED);

        let body = axum::body::to_bytes(create_response.into_body(), usize::MAX).await.unwrap();
        let build_response: BuildResponse = serde_json::from_slice(&body).unwrap();
        let build_id = build_response.build_id;

        // Now get the logs - should return 404 since build hasn't been executed yet
        let logs_response = app
            .oneshot(
                Request::builder()
                    .uri(&format!("/api/builds/{}/logs", build_id))
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(logs_response.status(), StatusCode::NOT_FOUND);

        let body = axum::body::to_bytes(logs_response.into_body(), usize::MAX).await.unwrap();
        let error_response: ErrorResponse = serde_json::from_slice(&body).unwrap();
        
        assert_eq!(error_response.code, "LOGS_NOT_FOUND");
        assert!(error_response.error.contains("No logs found for build"));
    }

    #[tokio::test]
    async fn test_get_build_logs_not_found() {
        let app = create_router();
        let non_existent_id = Uuid::new_v4();

        let response = app
            .oneshot(
                Request::builder()
                    .uri(&format!("/api/builds/{}/logs", non_existent_id))
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::NOT_FOUND);

        let body = axum::body::to_bytes(response.into_body(), usize::MAX).await.unwrap();
        let error_response: ErrorResponse = serde_json::from_slice(&body).unwrap();
        
        assert_eq!(error_response.code, "BUILD_NOT_FOUND");
        assert!(error_response.error.contains("Build not found"));
    }

    #[tokio::test]
    async fn test_create_build_malformed_json() {
        let app = create_router();

        let malformed_json = r#"{"tool": "bazel", "name": "test""#; // Missing closing brace

        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/builds")
                    .header("content-type", "application/json")
                    .body(Body::from(malformed_json))
                    .unwrap(),
            )
            .await
            .unwrap();

        // Axum should return 400 for malformed JSON
        assert_eq!(response.status(), StatusCode::BAD_REQUEST);
    }

    #[tokio::test]
    async fn test_stream_build_logs_not_found() {
        let app = create_router();
        let non_existent_id = Uuid::new_v4();

        let response = app
            .oneshot(
                Request::builder()
                    .uri(&format!("/api/builds/{}/logs/stream", non_existent_id))
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();

        let status = response.status();
        assert_eq!(status, StatusCode::NOT_FOUND);

        let body = axum::body::to_bytes(response.into_body(), usize::MAX).await.unwrap();
        
        // Should have JSON error response for 404
        if !body.is_empty() {
            let error_response: ErrorResponse = serde_json::from_slice(&body).unwrap();
            assert_eq!(error_response.code, "BUILD_NOT_FOUND");
            assert!(error_response.error.contains("Build not found"));
        }
    }

    #[tokio::test]
    async fn test_stream_build_logs_success() {
        // Create a shared server instance
        let server = Arc::new(BuildPalServer::new());
        
        let app = Router::new()
            .route("/api/health", get(health_check))
            .route("/api/builds", post(create_build))
            .route("/api/builds/{id}", get(get_build))
            .route("/api/builds/{id}/logs", get(get_build_logs))
            .route("/api/builds/{id}/logs/stream", get(stream_build_logs))
            .with_state(server.clone());

        // First create a build
        let config = CLIConfig {
            tool: BuildTool::Bazel,
            name: "test-project".to_string(),
            description: None,
            mode: ExecutionMode::Async,
            retention: RetentionPolicy::All,
            retention_duration_days: Some(7),
            environment: Environment::Native,
            parsing: None,
            docker: None,
            ai: None,
        };

        let build_request = BuildRequest::new(
            "/path/to/project".to_string(),
            "echo test".to_string(),
            config,
            "test".to_string(),
        );

        let request_body = serde_json::to_string(&build_request).unwrap();

        let create_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/builds")
                    .header("content-type", "application/json")
                    .body(Body::from(request_body))
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(create_response.status(), StatusCode::CREATED);

        let body = axum::body::to_bytes(create_response.into_body(), usize::MAX).await.unwrap();
        let build_response: BuildResponse = serde_json::from_slice(&body).unwrap();
        let build_id = build_response.build_id;

        // Now test the streaming endpoint
        let stream_response = app
            .oneshot(
                Request::builder()
                    .uri(&format!("/api/builds/{}/logs/stream", build_id))
                    .header("accept", "text/event-stream")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();

        // Should return 200 OK for streaming
        assert_eq!(stream_response.status(), StatusCode::OK);
        
        // Should have the correct content-type for SSE
        let content_type = stream_response.headers().get("content-type");
        assert!(content_type.is_some());
        let content_type_str = content_type.unwrap().to_str().unwrap();
        assert!(content_type_str.contains("text/event-stream"));
    }
}