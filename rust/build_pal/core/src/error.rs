use serde::{Deserialize, Serialize};
use thiserror::Error;

/// Comprehensive error types for Build Pal
#[derive(Error, Debug, Clone, Serialize, Deserialize)]
pub enum BuildPalError {
    /// Configuration-related errors
    #[error("Configuration error: {message}")]
    Configuration { message: String },

    /// Git-related errors
    #[error("Git error: {message}")]
    Git { message: String },

    /// Server communication errors
    #[error("Server error: {message}")]
    Server { message: String },

    /// Build execution errors
    #[error("Build execution error: {message}")]
    Execution { message: String },

    /// File system errors
    #[error("File system error: {message}")]
    FileSystem { message: String },

    /// Network/HTTP errors
    #[error("Network error: {message}")]
    Network { message: String },

    /// Validation errors
    #[error("Validation error: {message}")]
    Validation { message: String },

    /// Plugin-related errors
    #[error("Plugin error: {message}")]
    Plugin { message: String },

    /// Database/storage errors
    #[error("Storage error: {message}")]
    Storage { message: String },

    /// Authentication/authorization errors
    #[error("Authentication error: {message}")]
    Auth { message: String },

    /// Timeout errors
    #[error("Timeout error: {message}")]
    Timeout { message: String },

    /// Generic internal errors
    #[error("Internal error: {message}")]
    Internal { message: String },
}

impl BuildPalError {
    /// Create a configuration error
    pub fn configuration<S: Into<String>>(message: S) -> Self {
        Self::Configuration {
            message: message.into(),
        }
    }

    /// Create a git error
    pub fn git<S: Into<String>>(message: S) -> Self {
        Self::Git {
            message: message.into(),
        }
    }

    /// Create a server error
    pub fn server<S: Into<String>>(message: S) -> Self {
        Self::Server {
            message: message.into(),
        }
    }

    /// Create an execution error
    pub fn execution<S: Into<String>>(message: S) -> Self {
        Self::Execution {
            message: message.into(),
        }
    }

    /// Create a file system error
    pub fn file_system<S: Into<String>>(message: S) -> Self {
        Self::FileSystem {
            message: message.into(),
        }
    }

    /// Create a network error
    pub fn network<S: Into<String>>(message: S) -> Self {
        Self::Network {
            message: message.into(),
        }
    }

    /// Create a validation error
    pub fn validation<S: Into<String>>(message: S) -> Self {
        Self::Validation {
            message: message.into(),
        }
    }

    /// Create a plugin error
    pub fn plugin<S: Into<String>>(message: S) -> Self {
        Self::Plugin {
            message: message.into(),
        }
    }

    /// Create a storage error
    pub fn storage<S: Into<String>>(message: S) -> Self {
        Self::Storage {
            message: message.into(),
        }
    }

    /// Create an auth error
    pub fn auth<S: Into<String>>(message: S) -> Self {
        Self::Auth {
            message: message.into(),
        }
    }

    /// Create a timeout error
    pub fn timeout<S: Into<String>>(message: S) -> Self {
        Self::Timeout {
            message: message.into(),
        }
    }

    /// Create an internal error
    pub fn internal<S: Into<String>>(message: S) -> Self {
        Self::Internal {
            message: message.into(),
        }
    }

    /// Get the error category for logging and metrics
    pub fn category(&self) -> &'static str {
        match self {
            Self::Configuration { .. } => "configuration",
            Self::Git { .. } => "git",
            Self::Server { .. } => "server",
            Self::Execution { .. } => "execution",
            Self::FileSystem { .. } => "filesystem",
            Self::Network { .. } => "network",
            Self::Validation { .. } => "validation",
            Self::Plugin { .. } => "plugin",
            Self::Storage { .. } => "storage",
            Self::Auth { .. } => "auth",
            Self::Timeout { .. } => "timeout",
            Self::Internal { .. } => "internal",
        }
    }

    /// Check if this is a user-facing error (vs internal error)
    pub fn is_user_facing(&self) -> bool {
        matches!(
            self,
            Self::Configuration { .. }
                | Self::Git { .. }
                | Self::Validation { .. }
                | Self::Network { .. }
                | Self::Timeout { .. }
        )
    }

    /// Get a user-friendly error message
    pub fn user_message(&self) -> String {
        match self {
            Self::Configuration { message } => {
                format!("Configuration issue: {}", message)
            }
            Self::Git { message } => {
                format!("Git repository issue: {}", message)
            }
            Self::Server { .. } => {
                "Unable to connect to Build Pal server. Please ensure the server is running.".to_string()
            }
            Self::Execution { message } => {
                format!("Build execution failed: {}", message)
            }
            Self::FileSystem { message } => {
                format!("File system error: {}", message)
            }
            Self::Network { .. } => {
                "Network connection issue. Please check your connection and try again.".to_string()
            }
            Self::Validation { message } => {
                format!("Invalid input: {}", message)
            }
            Self::Plugin { message } => {
                format!("Plugin error: {}", message)
            }
            Self::Storage { .. } => {
                "Data storage issue. Please try again later.".to_string()
            }
            Self::Auth { message } => {
                format!("Authentication required: {}", message)
            }
            Self::Timeout { .. } => {
                "Operation timed out. Please try again.".to_string()
            }
            Self::Internal { .. } => {
                "An internal error occurred. Please try again or contact support.".to_string()
            }
        }
    }
}

/// Result type alias for Build Pal operations
pub type Result<T> = std::result::Result<T, BuildPalError>;

/// Convert from anyhow::Error to BuildPalError
impl From<anyhow::Error> for BuildPalError {
    fn from(err: anyhow::Error) -> Self {
        Self::internal(err.to_string())
    }
}

/// Convert from std::io::Error to BuildPalError
impl From<std::io::Error> for BuildPalError {
    fn from(err: std::io::Error) -> Self {
        Self::file_system(err.to_string())
    }
}

/// Convert from serde_json::Error to BuildPalError
impl From<serde_json::Error> for BuildPalError {
    fn from(err: serde_json::Error) -> Self {
        Self::configuration(format!("JSON parsing error: {}", err))
    }
}

/// Convert from git2::Error to BuildPalError
impl From<git2::Error> for BuildPalError {
    fn from(err: git2::Error) -> Self {
        Self::git(err.message().to_string())
    }
}

/// Convert from reqwest::Error to BuildPalError
impl From<reqwest::Error> for BuildPalError {
    fn from(err: reqwest::Error) -> Self {
        if err.is_timeout() {
            Self::timeout("Request timed out")
        } else if err.is_connect() {
            Self::network("Connection failed")
        } else {
            Self::network(err.to_string())
        }
    }
}

/// Convert from tokio::time::error::Elapsed to BuildPalError
impl From<tokio::time::error::Elapsed> for BuildPalError {
    fn from(_: tokio::time::error::Elapsed) -> Self {
        Self::timeout("Operation timed out")
    }
}

/// Convert from uuid::Error to BuildPalError
impl From<uuid::Error> for BuildPalError {
    fn from(err: uuid::Error) -> Self {
        Self::validation(format!("Invalid UUID format: {}", err))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_error_creation() {
        let config_err = BuildPalError::configuration("Invalid tool type");
        assert_eq!(config_err.category(), "configuration");
        assert!(config_err.is_user_facing());
        assert!(config_err.user_message().contains("Configuration issue"));

        let server_err = BuildPalError::server("Connection refused");
        assert_eq!(server_err.category(), "server");
        assert!(!server_err.is_user_facing());
        assert!(server_err.user_message().contains("Unable to connect"));
    }

    #[test]
    fn test_error_conversions() {
        let io_err = std::io::Error::new(std::io::ErrorKind::NotFound, "File not found");
        let build_pal_err: BuildPalError = io_err.into();
        assert_eq!(build_pal_err.category(), "filesystem");

        let json_err = serde_json::from_str::<serde_json::Value>("invalid json").unwrap_err();
        let build_pal_err: BuildPalError = json_err.into();
        assert_eq!(build_pal_err.category(), "configuration");
    }

    #[test]
    fn test_user_messages() {
        // Errors that show internal details in user messages
        let errors_with_details = vec![
            BuildPalError::configuration("test"),
            BuildPalError::git("test"),
        ];

        for error in errors_with_details {
            let user_msg = error.user_message();
            assert!(!user_msg.is_empty());
            assert!(user_msg.contains("test")); // These errors should show internal details
        }

        // Errors that hide internal details in user messages
        let errors_without_details = vec![
            BuildPalError::server("test"),
            BuildPalError::network("test"),
            BuildPalError::timeout("test"),
            BuildPalError::internal("test"),
        ];

        for error in errors_without_details {
            let user_msg = error.user_message();
            assert!(!user_msg.is_empty());
            assert!(!user_msg.contains("test")); // Internal details should be hidden for these errors
        }
    }
}