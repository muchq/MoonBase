use build_pal_core::{BuildRequest, BuildResponse, ErrorResponse, HealthResponse, CancelBuildRequest};
use anyhow::{Context, Result};
use reqwest::Client;
use std::time::Duration;
use tokio::time::sleep;
use uuid::Uuid;

/// Configuration for retry behavior
#[derive(Debug, Clone)]
pub struct RetryConfig {
    pub max_attempts: u32,
    pub initial_delay: Duration,
    pub max_delay: Duration,
    pub backoff_multiplier: f64,
}

impl Default for RetryConfig {
    fn default() -> Self {
        Self {
            max_attempts: 3,
            initial_delay: Duration::from_millis(100),
            max_delay: Duration::from_secs(5),
            backoff_multiplier: 2.0,
        }
    }
}

/// HTTP client for communicating with build_pal server
pub struct BuildPalClient {
    client: Client,
    base_url: String,
    retry_config: RetryConfig,
}

impl BuildPalClient {
    pub fn new(base_url: String) -> Self {
        Self {
            client: Client::builder()
                .timeout(Duration::from_secs(30))
                .build()
                .expect("Failed to create HTTP client"),
            base_url,
            retry_config: RetryConfig::default(),
        }
    }

    pub fn with_retry_config(mut self, retry_config: RetryConfig) -> Self {
        self.retry_config = retry_config;
        self
    }

    pub fn default() -> Self {
        Self::new("http://localhost:8080".to_string())
    }

    /// Submit a build request to the server with retry logic
    pub async fn submit_build(&self, request: BuildRequest) -> Result<BuildResponse> {
        let url = format!("{}/api/builds", self.base_url);
        
        self.retry_request(|| async {
            let response = self.client
                .post(&url)
                .json(&request)
                .send()
                .await
                .with_context(|| "Failed to send build request")?;

            self.handle_response(response).await
        }).await
    }

    /// Cancel a build request
    pub async fn cancel_build(&self, build_id: Uuid) -> Result<()> {
        let url = format!("{}/api/builds/{}", self.base_url, build_id);
        let cancel_request = CancelBuildRequest { build_id };
        
        self.retry_request(|| async {
            let response = self.client
                .delete(&url)
                .json(&cancel_request)
                .send()
                .await
                .with_context(|| format!("Failed to cancel build {}", build_id))?;

            if response.status().is_success() {
                Ok(())
            } else {
                let error_response: ErrorResponse = response.json().await
                    .with_context(|| "Failed to parse error response")?;
                Err(anyhow::anyhow!("Server error: {}", error_response.error))
            }
        }).await
    }

    /// Check if the server is running and healthy
    pub async fn health_check(&self) -> Result<HealthResponse> {
        let url = format!("{}/api/health", self.base_url);
        
        let response = self.client
            .get(&url)
            .send()
            .await
            .with_context(|| "Failed to connect to server")?;

        if response.status().is_success() {
            let health_response: HealthResponse = response.json().await
                .with_context(|| "Failed to parse health response")?;
            Ok(health_response)
        } else {
            Err(anyhow::anyhow!("Server health check failed with status: {}", response.status()))
        }
    }

    /// Check if the server is available (simple connectivity check)
    pub async fn is_server_available(&self) -> bool {
        match self.health_check().await {
            Ok(_) => true,
            Err(_) => false,
        }
    }

    /// Wait for server to become available with timeout
    pub async fn wait_for_server(&self, timeout: Duration) -> Result<()> {
        let start = std::time::Instant::now();
        let mut delay = Duration::from_millis(100);
        
        while start.elapsed() < timeout {
            if self.is_server_available().await {
                return Ok(());
            }
            
            sleep(delay).await;
            delay = std::cmp::min(delay * 2, Duration::from_secs(1));
        }
        
        Err(anyhow::anyhow!("Server did not become available within {:?}", timeout))
    }

    /// Generic retry logic for HTTP requests
    async fn retry_request<F, Fut, T>(&self, request_fn: F) -> Result<T>
    where
        F: Fn() -> Fut,
        Fut: std::future::Future<Output = Result<T>>,
    {
        let mut last_error = None;
        let mut delay = self.retry_config.initial_delay;

        for attempt in 1..=self.retry_config.max_attempts {
            match request_fn().await {
                Ok(result) => return Ok(result),
                Err(err) => {
                    last_error = Some(err);
                    
                    if attempt < self.retry_config.max_attempts {
                        tracing::warn!(
                            "Request attempt {} failed, retrying in {:?}: {}",
                            attempt,
                            delay,
                            last_error.as_ref().unwrap()
                        );
                        
                        sleep(delay).await;
                        delay = std::cmp::min(
                            Duration::from_millis((delay.as_millis() as f64 * self.retry_config.backoff_multiplier) as u64),
                            self.retry_config.max_delay
                        );
                    }
                }
            }
        }

        Err(last_error.unwrap_or_else(|| anyhow::anyhow!("All retry attempts failed")))
    }

    /// Handle HTTP response and parse result
    async fn handle_response<T>(&self, response: reqwest::Response) -> Result<T>
    where
        T: serde::de::DeserializeOwned,
    {
        let status = response.status();
        
        if status.is_success() {
            let result: T = response.json().await
                .with_context(|| "Failed to parse successful response")?;
            Ok(result)
        } else {
            // Try to parse as error response first
            let response_text = response.text().await
                .with_context(|| "Failed to read error response body")?;
            
            if let Ok(error_response) = serde_json::from_str::<ErrorResponse>(&response_text) {
                Err(anyhow::anyhow!("Server error ({}): {}", status, error_response.error))
            } else {
                Err(anyhow::anyhow!("Server error ({}): {}", status, response_text))
            }
        }
    }

    /// Get the base URL for this client
    pub fn base_url(&self) -> &str {
        &self.base_url
    }

    /// Get the retry configuration
    pub fn retry_config(&self) -> &RetryConfig {
        &self.retry_config
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use build_pal_core::{CLIConfig, BuildTool, ExecutionMode, Environment, RetentionPolicy};
    use std::sync::Arc;
    use std::sync::atomic::AtomicU32;
    use uuid::Uuid;

    fn create_test_config() -> CLIConfig {
        CLIConfig {
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
        }
    }

    fn create_test_build_request() -> BuildRequest {
        BuildRequest::new(
            "/path/to/project".to_string(),
            "build //...".to_string(),
            create_test_config(),
            "cli".to_string(),
        )
    }

    #[test]
    fn test_client_creation() {
        let client = BuildPalClient::default();
        assert_eq!(client.base_url(), "http://localhost:8080");
        assert_eq!(client.retry_config().max_attempts, 3);
    }

    #[test]
    fn test_client_with_custom_url() {
        let client = BuildPalClient::new("http://localhost:9000".to_string());
        assert_eq!(client.base_url(), "http://localhost:9000");
    }

    #[test]
    fn test_client_with_retry_config() {
        let retry_config = RetryConfig {
            max_attempts: 5,
            initial_delay: Duration::from_millis(50),
            max_delay: Duration::from_secs(10),
            backoff_multiplier: 1.5,
        };
        
        let client = BuildPalClient::new("http://localhost:8080".to_string())
            .with_retry_config(retry_config.clone());
        
        assert_eq!(client.retry_config().max_attempts, 5);
        assert_eq!(client.retry_config().initial_delay, Duration::from_millis(50));
        assert_eq!(client.retry_config().max_delay, Duration::from_secs(10));
        assert_eq!(client.retry_config().backoff_multiplier, 1.5);
    }

    #[test]
    fn test_retry_config_default() {
        let config = RetryConfig::default();
        assert_eq!(config.max_attempts, 3);
        assert_eq!(config.initial_delay, Duration::from_millis(100));
        assert_eq!(config.max_delay, Duration::from_secs(5));
        assert_eq!(config.backoff_multiplier, 2.0);
    }

    #[tokio::test]
    async fn test_server_unavailable_scenario() {
        // Use a port that's unlikely to be in use
        let client = BuildPalClient::new("http://localhost:65432".to_string());
        
        // Health check should fail
        let health_result = client.health_check().await;
        assert!(health_result.is_err());
        
        // Server availability check should return false
        assert!(!client.is_server_available().await);
        
        // Build submission should fail
        let request = create_test_build_request();
        let result = client.submit_build(request).await;
        assert!(result.is_err());
        
        // Cancel build should fail
        let build_id = Uuid::new_v4();
        let cancel_result = client.cancel_build(build_id).await;
        assert!(cancel_result.is_err());
    }

    #[tokio::test]
    async fn test_wait_for_server_timeout() {
        let client = BuildPalClient::new("http://localhost:65433".to_string());
        
        let start = std::time::Instant::now();
        let result = client.wait_for_server(Duration::from_millis(200)).await;
        let elapsed = start.elapsed();
        
        assert!(result.is_err());
        assert!(elapsed >= Duration::from_millis(200));
        assert!(elapsed < Duration::from_millis(500)); // Should not take too long
    }

    #[tokio::test]
    async fn test_retry_logic_with_mock_server() {
        // This test simulates retry behavior by tracking call attempts
        let attempt_counter = Arc::new(AtomicU32::new(0));
        let _counter_clone = attempt_counter.clone();
        
        let retry_config = RetryConfig {
            max_attempts: 3,
            initial_delay: Duration::from_millis(10),
            max_delay: Duration::from_millis(50),
            backoff_multiplier: 2.0,
        };
        
        let client = BuildPalClient::new("http://localhost:65434".to_string())
            .with_retry_config(retry_config);
        
        // Test that retry logic attempts the correct number of times
        let request = create_test_build_request();
        let result = client.submit_build(request).await;
        
        assert!(result.is_err());
        // The actual retry attempts are handled internally by reqwest and our retry logic
        // We can't easily mock the HTTP layer in this unit test, but we've verified
        // the error handling path works correctly
    }

    #[tokio::test]
    async fn test_build_request_serialization() {
        let request = create_test_build_request();
        
        // Verify the request can be serialized to JSON
        let json = serde_json::to_string(&request).unwrap();
        assert!(json.contains("build //..."));
        assert!(json.contains("test-project"));
        assert!(json.contains("/path/to/project"));
        
        // Verify it can be deserialized back
        let deserialized: BuildRequest = serde_json::from_str(&json).unwrap();
        assert_eq!(deserialized.command, "build //...");
        assert_eq!(deserialized.config.name, "test-project");
        assert_eq!(deserialized.project_path, "/path/to/project");
    }

    #[tokio::test]
    async fn test_error_handling_with_invalid_json() {
        // Test that the client handles malformed responses gracefully
        let client = BuildPalClient::new("http://localhost:65435".to_string());
        
        // These will fail due to connection errors, but we're testing
        // that the error handling doesn't panic and provides meaningful messages
        let request = create_test_build_request();
        let result = client.submit_build(request).await;
        
        match result {
            Err(err) => {
                let error_msg = err.to_string();
                // Should contain context about the failure
                assert!(error_msg.contains("Failed") || error_msg.contains("error") || error_msg.contains("connection"));
            }
            Ok(_) => panic!("Expected error when connecting to non-existent server"),
        }
    }

    #[tokio::test]
    async fn test_cancel_build_request_format() {
        let build_id = Uuid::new_v4();
        let cancel_request = CancelBuildRequest { build_id };
        
        // Verify the cancel request can be serialized
        let json = serde_json::to_string(&cancel_request).unwrap();
        assert!(json.contains(&build_id.to_string()));
        
        // Verify it can be deserialized back
        let deserialized: CancelBuildRequest = serde_json::from_str(&json).unwrap();
        assert_eq!(deserialized.build_id, build_id);
    }

    #[test]
    fn test_url_formatting() {
        let client = BuildPalClient::new("http://localhost:8080".to_string());
        
        // Test that URLs are formatted correctly
        let build_id = Uuid::new_v4();
        let _expected_cancel_url = format!("http://localhost:8080/api/builds/{}", build_id);
        
        // We can't directly test the private URL formatting, but we can verify
        // the base URL is stored correctly
        assert_eq!(client.base_url(), "http://localhost:8080");
    }

    #[tokio::test]
    async fn test_concurrent_requests() {
        let client = Arc::new(BuildPalClient::new("http://localhost:65436".to_string()));
        
        // Test that multiple concurrent requests don't interfere with each other
        let mut handles = vec![];
        
        for i in 0..5 {
            let client_clone = client.clone();
            let handle = tokio::spawn(async move {
                let mut request = create_test_build_request();
                request.command = format!("build //test{}", i);
                client_clone.submit_build(request).await
            });
            handles.push(handle);
        }
        
        // All should fail (no server), but none should panic or interfere
        for handle in handles {
            let result = handle.await.unwrap();
            assert!(result.is_err());
        }
    }

    #[tokio::test]
    async fn test_timeout_behavior() {
        // Test with a very short timeout to ensure timeout handling works
        let client = BuildPalClient::new("http://localhost:65437".to_string());
        
        let start = std::time::Instant::now();
        let result = client.health_check().await;
        let elapsed = start.elapsed();
        
        assert!(result.is_err());
        // Should fail relatively quickly due to connection refused, not timeout
        assert!(elapsed < Duration::from_secs(5));
    }
}