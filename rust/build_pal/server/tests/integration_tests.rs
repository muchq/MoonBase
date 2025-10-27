use build_pal_core::{
    BuildRequest, BuildResponse, BuildStatusResponse, CLIConfig, BuildTool, 
    ExecutionMode, Environment, RetentionPolicy, BuildStatus
};
use build_pal_server::{create_router, BuildPalServer, ExecutionEngine, LogManager};
use axum::{
    body::Body,
    http::{Request, StatusCode},
};
use serde_json;
use std::sync::Arc;
use std::time::Duration;
use tokio::time::sleep;
use tower::ServiceExt;
use uuid::Uuid;

/// Integration test for complete API workflow with real build execution
#[tokio::test]
async fn test_complete_build_workflow() {
    let app = create_router();

    // Create a build request with a simple command
    let config = CLIConfig {
        tool: BuildTool::Bazel,
        name: "integration-test-project".to_string(),
        description: Some("Integration test project".to_string()),
        mode: ExecutionMode::Async,
        retention: RetentionPolicy::All,
        retention_duration_days: Some(7),
        environment: Environment::Native,
        parsing: None,
        docker: None,
        ai: None,
    };

    let build_request = BuildRequest::new(
        std::env::temp_dir().to_string_lossy().to_string(),
        "echo 'Integration test build output'".to_string(),
        config,
        "integration_test".to_string(),
    );

    let request_body = serde_json::to_string(&build_request).unwrap();

    // Step 1: Create the build
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

    assert!(!build_id.is_nil());
    assert!(build_response.web_url.contains(&build_id.to_string()));

    // Step 2: Get build status immediately (should be queued)
    let status_response = app
        .clone()
        .oneshot(
            Request::builder()
                .uri(&format!("/api/builds/{}", build_id))
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(status_response.status(), StatusCode::OK);

    let body = axum::body::to_bytes(status_response.into_body(), usize::MAX).await.unwrap();
    let status_response: BuildStatusResponse = serde_json::from_slice(&body).unwrap();
    
    assert_eq!(status_response.build.id, build_id);
    assert_eq!(status_response.build.command, "echo 'Integration test build output'");
    assert_eq!(status_response.build.status, BuildStatus::Queued);

    // Step 3: Check if logs are available
    // Logs should be available (even if empty) since builds execute in the background
    sleep(Duration::from_millis(100)).await;

    let logs_response = app
        .oneshot(
            Request::builder()
                .uri(&format!("/api/builds/{}/logs", build_id))
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();

    // Should return 200 OK with logs (may be empty or contain execution output)
    assert_eq!(logs_response.status(), StatusCode::OK);

    let body = axum::body::to_bytes(logs_response.into_body(), usize::MAX).await.unwrap();
    let logs_json: serde_json::Value = serde_json::from_slice(&body).unwrap();

    // Verify response structure
    assert_eq!(logs_json["build_id"], build_id.to_string());
    assert!(logs_json["logs"].is_array());
    assert!(logs_json["log_count"].is_number());
}

/// Test concurrent build handling
#[tokio::test]
async fn test_concurrent_build_handling() {
    let app = create_router();
    let mut build_ids = Vec::new();

    // Create multiple builds concurrently
    let mut handles = Vec::new();
    
    for i in 0..5 {
        let app_clone = app.clone();
        let handle = tokio::spawn(async move {
            let config = CLIConfig {
                tool: BuildTool::Maven,
                name: format!("concurrent-test-{}", i),
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
                std::env::temp_dir().to_string_lossy().to_string(),
                format!("echo 'Concurrent build {}'", i),
                config,
                "concurrent_test".to_string(),
            );

            let request_body = serde_json::to_string(&build_request).unwrap();

            let response = app_clone
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
            build_response.build_id
        });
        
        handles.push(handle);
    }

    // Wait for all builds to be created
    for handle in handles {
        let build_id = handle.await.unwrap();
        build_ids.push(build_id);
    }

    assert_eq!(build_ids.len(), 5);

    // Verify all builds are unique
    let mut unique_ids = build_ids.clone();
    unique_ids.sort();
    unique_ids.dedup();
    assert_eq!(unique_ids.len(), 5);

    // Verify we can retrieve all builds
    for build_id in build_ids {
        let response = app
            .clone()
            .oneshot(
                Request::builder()
                    .uri(&format!("/api/builds/{}", build_id))
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::OK);

        let body = axum::body::to_bytes(response.into_body(), usize::MAX).await.unwrap();
        let status_response: BuildStatusResponse = serde_json::from_slice(&body).unwrap();
        assert_eq!(status_response.build.id, build_id);
    }
}

/// Test real build execution with log capture
#[tokio::test]
async fn test_real_build_execution_with_logs() {
    // Create a server with shared execution engine for direct testing
    let log_manager = Arc::new(LogManager::new());
    let execution_engine = Arc::new(ExecutionEngine::with_log_manager(log_manager.clone()));
    let server = Arc::new(BuildPalServer::with_execution_engine(execution_engine.clone()));

    // Create a test build
    let config = CLIConfig {
        tool: BuildTool::Bazel,
        name: "log-test-project".to_string(),
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
        std::env::temp_dir().to_string_lossy().to_string(),
        "echo 'Test log output'".to_string(), // Simplified command for more reliable testing
        config,
        "log_test".to_string(),
    );

    // Submit the build
    let build_id = server.submit_build(build_request).await.unwrap();
    
    // Get the build
    let build = server.get_build(build_id).await.unwrap();
    assert_eq!(build.status, BuildStatus::Queued);

    // Execute the build directly using the execution engine
    let execution_result = execution_engine.execute_build(&build).await;
    assert!(execution_result.is_ok());

    let result = execution_result.unwrap();
    assert_eq!(result.status, BuildStatus::Completed);
    assert_eq!(result.exit_code, 0);
    assert!(result.duration_ms > 0);

    // Verify logs were captured
    let logs = server.get_build_logs(build_id).await;
    assert!(logs.is_some());

    let log_lines = logs.unwrap();
    assert!(!log_lines.is_empty());

    // Check that stdout was captured
    let log_content = log_lines.join("\n");
    assert!(log_content.contains("Test log output"));
    assert!(log_content.contains("[STDOUT]"));
    
    // Should also have system logs
    assert!(log_content.contains("[SYSTEM]"));
}

/// Test stderr capture specifically
#[tokio::test]
async fn test_stderr_capture() {
    let log_manager = Arc::new(LogManager::new());
    let execution_engine = Arc::new(ExecutionEngine::with_log_manager(log_manager.clone()));
    let server = Arc::new(BuildPalServer::with_execution_engine(execution_engine.clone()));

    // Create a build that outputs to stderr using a more reliable method
    let config = CLIConfig {
        tool: BuildTool::Bazel,
        name: "stderr-test-project".to_string(),
        description: None,
        mode: ExecutionMode::Async,
        retention: RetentionPolicy::All,
        retention_duration_days: Some(7),
        environment: Environment::Native,
        parsing: None,
        docker: None,
        ai: None,
    };

    // Use a command that writes to stderr - use a failing command that produces stderr
    let build_request = BuildRequest::new(
        std::env::temp_dir().to_string_lossy().to_string(),
        "ls /nonexistent_directory_12345".to_string(), // This will fail and write to stderr
        config,
        "stderr_test".to_string(),
    );

    let build_id = server.submit_build(build_request).await.unwrap();
    let build = server.get_build(build_id).await.unwrap();

    // Execute the build
    let execution_result = execution_engine.execute_build(&build).await;
    assert!(execution_result.is_ok());

    let result = execution_result.unwrap();
    assert_eq!(result.status, BuildStatus::Failed); // Command should fail
    assert_ne!(result.exit_code, 0);

    // Verify stderr was captured from the failing command
    let logs = server.get_build_logs(build_id).await;
    assert!(logs.is_some());

    let log_lines = logs.unwrap();
    let log_content = log_lines.join("\n");
    
    // Should contain stderr output from the failing ls command
    assert!(log_content.contains("[STDERR]"));
    // Should also have system logs
    assert!(log_content.contains("[SYSTEM]"));
}

/// Test build execution failure handling
#[tokio::test]
async fn test_build_execution_failure() {
    let log_manager = Arc::new(LogManager::new());
    let execution_engine = Arc::new(ExecutionEngine::with_log_manager(log_manager.clone()));
    let server = Arc::new(BuildPalServer::with_execution_engine(execution_engine.clone()));

    // Create a build that will fail
    let config = CLIConfig {
        tool: BuildTool::Gradle,
        name: "failure-test-project".to_string(),
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
        std::env::temp_dir().to_string_lossy().to_string(),
        "false".to_string(), // Command that always fails
        config,
        "failure_test".to_string(),
    );

    let build_id = server.submit_build(build_request).await.unwrap();
    let build = server.get_build(build_id).await.unwrap();

    // Execute the failing build
    let execution_result = execution_engine.execute_build(&build).await;
    assert!(execution_result.is_ok());

    let result = execution_result.unwrap();
    assert_eq!(result.status, BuildStatus::Failed);
    assert_eq!(result.exit_code, 1);

    // Verify logs were still captured for failed build
    let logs = server.get_build_logs(build_id).await;
    assert!(logs.is_some());
}

/// Test log streaming functionality
#[tokio::test]
async fn test_log_streaming() {
    let log_manager = Arc::new(LogManager::new());
    let build_id = Uuid::new_v4();

    // Initialize logs for the build
    log_manager.initialize_build_logs(build_id).await.unwrap();

    // Create a log stream
    let mut stream = log_manager.create_log_stream(build_id).await.unwrap();

    // Add logs in a separate task
    let log_manager_clone = log_manager.clone();
    let build_id_clone = build_id;
    tokio::spawn(async move {
        use build_pal_server::LogStreamType;
        
        sleep(Duration::from_millis(10)).await;
        log_manager_clone.append_log(
            build_id_clone, 
            "First log line".to_string(), 
            LogStreamType::Stdout
        ).await.unwrap();
        
        sleep(Duration::from_millis(10)).await;
        log_manager_clone.append_log(
            build_id_clone, 
            "Second log line".to_string(), 
            LogStreamType::Stderr
        ).await.unwrap();
        
        sleep(Duration::from_millis(10)).await;
        log_manager_clone.append_log(
            build_id_clone, 
            "Build completed".to_string(), 
            LogStreamType::System
        ).await.unwrap();
    });

    // Collect streamed logs with timeout
    use tokio_stream::StreamExt;
    
    let mut collected_logs = Vec::new();
    let mut count = 0;
    
    while count < 3 {
        match tokio::time::timeout(Duration::from_millis(500), stream.next()).await {
            Ok(Some(log_entry)) => {
                collected_logs.push(log_entry);
                count += 1;
            }
            Ok(None) => break,
            Err(_) => {
                panic!("Timeout waiting for log entry {}", count + 1);
            }
        }
    }

    assert_eq!(collected_logs.len(), 3);
    assert_eq!(collected_logs[0].content, "First log line");
    assert_eq!(collected_logs[1].content, "Second log line");
    assert_eq!(collected_logs[2].content, "Build completed");
}

/// Test health check endpoint
#[tokio::test]
async fn test_health_check_integration() {
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
    let health_response: serde_json::Value = serde_json::from_slice(&body).unwrap();
    
    assert_eq!(health_response["status"], "ok");
    assert_eq!(health_response["version"], "0.1.0");
    assert!(health_response["uptime_seconds"].is_number());
    assert!(health_response["active_builds"].is_number());
    assert_eq!(health_response["database_connected"], false);
}

/// Test error handling for invalid requests
#[tokio::test]
async fn test_error_handling_integration() {
    let app = create_router();

    // Test invalid JSON
    let response = app
        .clone()
        .oneshot(
            Request::builder()
                .method("POST")
                .uri("/api/builds")
                .header("content-type", "application/json")
                .body(Body::from("invalid json"))
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(response.status(), StatusCode::BAD_REQUEST);

    // Test missing content-type
    let config = CLIConfig::default();
    let build_request = BuildRequest::new(
        "/path/to/project".to_string(),
        "echo test".to_string(),
        config,
        "test".to_string(),
    );
    let request_body = serde_json::to_string(&build_request).unwrap();

    let response = app
        .clone()
        .oneshot(
            Request::builder()
                .method("POST")
                .uri("/api/builds")
                // No content-type header
                .body(Body::from(request_body))
                .unwrap(),
        )
        .await
        .unwrap();

    // Should still work without explicit content-type or return an error
    // Axum typically requires content-type for JSON parsing
    assert!(
        response.status() == StatusCode::CREATED || 
        response.status() == StatusCode::BAD_REQUEST ||
        response.status() == StatusCode::UNSUPPORTED_MEDIA_TYPE,
        "Unexpected status: {}", response.status()
    );

    // Test invalid UUID in path
    let response = app
        .oneshot(
            Request::builder()
                .uri("/api/builds/not-a-uuid")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(response.status(), StatusCode::BAD_REQUEST);
}

/// Test server state consistency across multiple operations
#[tokio::test]
async fn test_server_state_consistency() {
    let app = create_router();
    let mut build_ids = Vec::new();

    // Create several builds
    for i in 0..3 {
        let config = CLIConfig {
            tool: BuildTool::Bazel,
            name: format!("consistency-test-{}", i),
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
            std::env::temp_dir().to_string_lossy().to_string(),
            format!("echo 'Build {}'", i),
            config,
            "consistency_test".to_string(),
        );

        let request_body = serde_json::to_string(&build_request).unwrap();

        let response = app
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

        assert_eq!(response.status(), StatusCode::CREATED);

        let body = axum::body::to_bytes(response.into_body(), usize::MAX).await.unwrap();
        let build_response: BuildResponse = serde_json::from_slice(&body).unwrap();
        build_ids.push(build_response.build_id);
    }

    // Verify all builds can be retrieved and have consistent state
    for (i, build_id) in build_ids.iter().enumerate() {
        let response = app
            .clone()
            .oneshot(
                Request::builder()
                    .uri(&format!("/api/builds/{}", build_id))
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::OK);

        let body = axum::body::to_bytes(response.into_body(), usize::MAX).await.unwrap();
        let status_response: BuildStatusResponse = serde_json::from_slice(&body).unwrap();
        
        assert_eq!(status_response.build.id, *build_id);
        assert_eq!(status_response.build.command, format!("echo 'Build {}'", i));
        assert_eq!(status_response.build.status, BuildStatus::Queued);
        assert!(status_response.logs_available);
    }

    // Check health endpoint shows correct active build count
    let health_response = app
        .oneshot(
            Request::builder()
                .uri("/api/health")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(health_response.status(), StatusCode::OK);

    let body = axum::body::to_bytes(health_response.into_body(), usize::MAX).await.unwrap();
    let health_data: serde_json::Value = serde_json::from_slice(&body).unwrap();
    
    // Should show 3 active builds (all in queued state)
    assert_eq!(health_data["active_builds"], 3);
}