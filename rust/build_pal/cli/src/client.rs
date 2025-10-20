use build_pal_core::{BuildRequest, BuildResponse, ErrorResponse};
use anyhow::{Context, Result};
use reqwest::Client;
// use serde_json;

/// HTTP client for communicating with build_pal server
pub struct BuildPalClient {
    client: Client,
    base_url: String,
}

impl BuildPalClient {
    pub fn new(base_url: String) -> Self {
        Self {
            client: Client::new(),
            base_url,
        }
    }

    pub fn default() -> Self {
        Self::new("http://localhost:8080".to_string())
    }

    /// Submit a build request to the server
    pub async fn submit_build(&self, request: BuildRequest) -> Result<BuildResponse> {
        let url = format!("{}/api/builds", self.base_url);
        
        let response = self.client
            .post(&url)
            .json(&request)
            .send()
            .await
            .with_context(|| "Failed to send build request")?;

        if response.status().is_success() {
            let build_response: BuildResponse = response.json().await
                .with_context(|| "Failed to parse build response")?;
            Ok(build_response)
        } else {
            let error_response: ErrorResponse = response.json().await
                .with_context(|| "Failed to parse error response")?;
            Err(anyhow::anyhow!("Server error: {}", error_response.error))
        }
    }

    /// Check if the server is running
    pub async fn health_check(&self) -> Result<bool> {
        let url = format!("{}/api/health", self.base_url);
        
        match self.client.get(&url).send().await {
            Ok(response) => Ok(response.status().is_success()),
            Err(_) => Ok(false),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_client_creation() {
        let client = BuildPalClient::default();
        assert_eq!(client.base_url, "http://localhost:8080");
    }

    #[test]
    fn test_client_with_custom_url() {
        let client = BuildPalClient::new("http://localhost:9000".to_string());
        assert_eq!(client.base_url, "http://localhost:9000");
    }
}