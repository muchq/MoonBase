use rmcp::model::{CallToolRequestParam, Content, RawContent};
use rmcp::service::RunningService;
use rmcp::transport::TokioChildProcess;
use rmcp::{RoleClient, ServiceExt};
use tokio::process::Command;

/// A lightweight MCP client that spawns an MCP server as a child process
/// and communicates over stdio.
///
/// Used by integration connectors (e.g. Google Docs) to call tools
/// exposed by external MCP servers.
pub struct McpClient {
    service: RunningService<RoleClient, ()>,
}

impl McpClient {
    /// Spawn an MCP server and connect to it.
    ///
    /// `command` is the program to run (e.g. "node"), `args` are its
    /// arguments (e.g. the path to the MCP server's entry point).
    pub async fn spawn(command: &str, args: &[&str]) -> Result<Self, McpError> {
        let mut cmd = Command::new(command);
        for arg in args {
            cmd.arg(arg);
        }

        let transport =
            TokioChildProcess::new(cmd).map_err(|e| McpError::Spawn(e.to_string()))?;
        let service = ().serve(transport)
            .await
            .map_err(|e| McpError::Connect(e.to_string()))?;

        Ok(Self { service })
    }

    /// List the tools available on the connected MCP server.
    pub async fn list_tools(&self) -> Result<Vec<String>, McpError> {
        let result = self
            .service
            .list_tools(Default::default())
            .await
            .map_err(|e| McpError::Call(e.to_string()))?;

        Ok(result.tools.iter().map(|t| t.name.to_string()).collect())
    }

    /// Call a tool by name with JSON arguments.
    ///
    /// Returns the text content from the tool's response.
    pub async fn call_tool(
        &self,
        name: &str,
        arguments: serde_json::Value,
    ) -> Result<String, McpError> {
        let params = CallToolRequestParam {
            name: name.to_string().into(),
            arguments: arguments.as_object().cloned(),
            task: None,
        };

        let result = self
            .service
            .call_tool(params)
            .await
            .map_err(|e| McpError::Call(e.to_string()))?;

        // Extract text content from the response
        let mut text_parts = Vec::new();
        for content in &result.content {
            if let Content { raw: RawContent::Text(text), .. } = content {
                text_parts.push(text.text.clone());
            }
        }

        if text_parts.is_empty() {
            Ok(String::new())
        } else {
            Ok(text_parts.join("\n"))
        }
    }

    /// Shut down the MCP server gracefully.
    pub async fn shutdown(self) -> Result<(), McpError> {
        let _ = self.service.cancel().await;
        Ok(())
    }
}

#[derive(Debug)]
pub enum McpError {
    Spawn(String),
    Connect(String),
    Call(String),
    Parse(String),
    Shutdown(String),
}

impl std::fmt::Display for McpError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Spawn(msg) => write!(f, "MCP spawn error: {msg}"),
            Self::Connect(msg) => write!(f, "MCP connect error: {msg}"),
            Self::Call(msg) => write!(f, "MCP call error: {msg}"),
            Self::Parse(msg) => write!(f, "MCP parse error: {msg}"),
            Self::Shutdown(msg) => write!(f, "MCP shutdown error: {msg}"),
        }
    }
}

impl std::error::Error for McpError {}
