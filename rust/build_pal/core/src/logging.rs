use serde::{Deserialize, Serialize};
use std::str::FromStr;
use tracing::Level;

/// Logging configuration for Build Pal components
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LoggingConfig {
    /// Log level (trace, debug, info, warn, error)
    pub level: LogLevel,
    /// Whether to include timestamps
    pub timestamps: bool,
    /// Whether to include target module names
    pub targets: bool,
    /// Whether to use JSON format (vs human-readable)
    pub json_format: bool,
    /// Component name for structured logging
    pub component: String,
}

/// Log level enumeration
#[derive(Debug, Clone, Copy, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
}

impl Default for LoggingConfig {
    fn default() -> Self {
        Self {
            level: LogLevel::Info,
            timestamps: true,
            targets: false,
            json_format: false,
            component: "build_pal".to_string(),
        }
    }
}

impl LoggingConfig {
    /// Create a new logging config for a specific component
    pub fn for_component(component: &str) -> Self {
        Self {
            component: component.to_string(),
            ..Default::default()
        }
    }

    /// Set the log level
    pub fn with_level(mut self, level: LogLevel) -> Self {
        self.level = level;
        self
    }

    /// Enable JSON formatting
    pub fn with_json_format(mut self) -> Self {
        self.json_format = true;
        self
    }

    /// Enable target module names
    pub fn with_targets(mut self) -> Self {
        self.targets = true;
        self
    }

    /// Initialize the global tracing subscriber with this configuration
    pub fn init(&self) -> crate::Result<()> {
        // Use simple tracing_subscriber::fmt::init for now to avoid feature dependencies
        if std::env::var("RUST_LOG").is_err() {
            unsafe {
                std::env::set_var("RUST_LOG", self.level.as_str());
            }
        }
        
        tracing_subscriber::fmt::init();

        tracing::info!(
            component = %self.component,
            level = %self.level.as_str(),
            json_format = %self.json_format,
            "Logging initialized"
        );

        Ok(())
    }
}

impl LogLevel {
    /// Convert to tracing::Level
    pub fn as_tracing_level(&self) -> Level {
        match self {
            Self::Trace => Level::TRACE,
            Self::Debug => Level::DEBUG,
            Self::Info => Level::INFO,
            Self::Warn => Level::WARN,
            Self::Error => Level::ERROR,
        }
    }

    /// Convert to string
    pub fn as_str(&self) -> &'static str {
        match self {
            Self::Trace => "trace",
            Self::Debug => "debug",
            Self::Info => "info",
            Self::Warn => "warn",
            Self::Error => "error",
        }
    }
}

impl FromStr for LogLevel {
    type Err = crate::BuildPalError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s.to_lowercase().as_str() {
            "trace" => Ok(Self::Trace),
            "debug" => Ok(Self::Debug),
            "info" => Ok(Self::Info),
            "warn" => Ok(Self::Warn),
            "error" => Ok(Self::Error),
            _ => Err(crate::BuildPalError::validation(format!(
                "Invalid log level: {}. Valid levels are: trace, debug, info, warn, error",
                s
            ))),
        }
    }
}

/// Structured logging macros for Build Pal
#[macro_export]
macro_rules! log_error {
    ($category:expr, $message:expr) => {
        tracing::error!(
            category = $category,
            error = $message,
            "Error occurred"
        );
    };
    ($category:expr, $message:expr, $($field:tt)*) => {
        tracing::error!(
            category = $category,
            error = $message,
            $($field)*,
            "Error occurred"
        );
    };
}

#[macro_export]
macro_rules! log_warn {
    ($category:expr, $message:expr) => {
        tracing::warn!(
            category = $category,
            warning = $message,
            "Warning occurred"
        );
    };
    ($category:expr, $message:expr, $($field:tt)*) => {
        tracing::warn!(
            category = $category,
            warning = $message,
            $($field)*,
            "Warning occurred"
        );
    };
}

#[macro_export]
macro_rules! log_info {
    ($category:expr, $message:expr) => {
        tracing::info!(
            category = $category,
            message = $message,
            "Info"
        );
    };
    ($category:expr, $message:expr, $($field:tt)*) => {
        tracing::info!(
            category = $category,
            message = $message,
            $($field)*,
            "Info"
        );
    };
}

#[macro_export]
macro_rules! log_debug {
    ($category:expr, $message:expr) => {
        tracing::debug!(
            category = $category,
            message = $message,
            "Debug"
        );
    };
    ($category:expr, $message:expr, $($field:tt)*) => {
        tracing::debug!(
            category = $category,
            message = $message,
            $($field)*,
            "Debug"
        );
    };
}

/// Log a Build Pal error with structured context
pub fn log_build_pal_error(error: &crate::BuildPalError, context: Option<&str>) {
    let category = error.category();
    let message = error.to_string();
    
    if let Some(ctx) = context {
        tracing::error!(
            category = category,
            error = %message,
            context = ctx,
            user_facing = error.is_user_facing(),
            "Build Pal error occurred"
        );
    } else {
        tracing::error!(
            category = category,
            error = %message,
            user_facing = error.is_user_facing(),
            "Build Pal error occurred"
        );
    }
}

/// Log a successful operation with structured context
pub fn log_success(category: &str, operation: &str, duration_ms: Option<u64>) {
    if let Some(duration) = duration_ms {
        tracing::info!(
            category = category,
            operation = operation,
            duration_ms = duration,
            "Operation completed successfully"
        );
    } else {
        tracing::info!(
            category = category,
            operation = operation,
            "Operation completed successfully"
        );
    }
}

/// Log the start of an operation
pub fn log_operation_start(category: &str, operation: &str) {
    tracing::info!(
        category = category,
        operation = operation,
        "Operation started"
    );
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_log_level_parsing() {
        assert!(matches!(LogLevel::from_str("info").unwrap(), LogLevel::Info));
        assert!(matches!(LogLevel::from_str("DEBUG").unwrap(), LogLevel::Debug));
        assert!(matches!(LogLevel::from_str("Error").unwrap(), LogLevel::Error));
        
        assert!(LogLevel::from_str("invalid").is_err());
    }

    #[test]
    fn test_logging_config_builder() {
        let config = LoggingConfig::for_component("test")
            .with_level(LogLevel::Debug)
            .with_json_format()
            .with_targets();

        assert_eq!(config.component, "test");
        assert!(matches!(config.level, LogLevel::Debug));
        assert!(config.json_format);
        assert!(config.targets);
    }

    #[test]
    fn test_log_level_conversion() {
        assert_eq!(LogLevel::Info.as_str(), "info");
        assert_eq!(LogLevel::Debug.as_tracing_level(), Level::DEBUG);
    }
}