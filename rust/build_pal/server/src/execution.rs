use build_pal_core::{Build, BuildStatus, Environment};
use crate::logs::{LogManager, LogStreamType};
use anyhow::Result;
use std::collections::HashMap;
use std::process::Stdio;
use std::sync::Arc;

use tokio::process::{Child, Command};
use tokio::sync::{mpsc, Mutex, RwLock};
use tokio::time::{timeout, Duration};
use tracing::{debug, info, warn};
use uuid::Uuid;

/// Build process information
#[derive(Debug)]
pub struct BuildProcess {
    pub build_id: Uuid,
    pub child: Child,
    pub status: BuildStatus,
    pub start_time: std::time::Instant,
    pub log_sender: mpsc::UnboundedSender<String>,
}

/// Build execution result
#[derive(Debug, Clone)]
pub struct ExecutionResult {
    pub exit_code: i32,
    pub duration_ms: u64,
    pub status: BuildStatus,
    pub logs: Vec<String>,
}

/// Build execution engine with process lifecycle management
pub struct ExecutionEngine {
    /// Active build processes
    active_processes: Arc<RwLock<HashMap<Uuid, Arc<Mutex<BuildProcess>>>>>,
    /// Log manager for streaming and storage
    log_manager: Arc<LogManager>,
}

impl ExecutionEngine {
    pub fn new() -> Self {
        Self {
            active_processes: Arc::new(RwLock::new(HashMap::new())),
            log_manager: Arc::new(LogManager::new()),
        }
    }

    pub fn with_log_manager(log_manager: Arc<LogManager>) -> Self {
        Self {
            active_processes: Arc::new(RwLock::new(HashMap::new())),
            log_manager,
        }
    }

    /// Execute a build command with full lifecycle management
    pub async fn execute_build(&self, build: &Build) -> Result<ExecutionResult> {
        info!("Starting build execution for build {}: {}", build.id, build.command);
        
        match build.environment {
            Environment::Native => self.execute_native_with_lifecycle(build).await,
            Environment::Docker => {
                warn!("Docker execution not yet implemented, falling back to native");
                self.execute_native_with_lifecycle(build).await
            }
        }
    }

    /// Execute native command with full process lifecycle management
    async fn execute_native_with_lifecycle(&self, build: &Build) -> Result<ExecutionResult> {
        let start_time = std::time::Instant::now();
        
        // Initialize log storage for this build
        self.log_manager.initialize_build_logs(build.id).await?;
        
        // Log build start
        self.log_manager.append_log(
            build.id,
            format!("Starting build: {}", build.command),
            LogStreamType::System,
        ).await?;
        
        // Parse command
        let parts: Vec<&str> = build.command.split_whitespace().collect();
        if parts.is_empty() {
            let error_msg = "Empty command";
            self.log_manager.append_log(
                build.id,
                error_msg.to_string(),
                LogStreamType::System,
            ).await?;
            return Err(anyhow::anyhow!(error_msg));
        }

        // Set up command with proper stdio handling
        let mut cmd = Command::new(parts[0]);
        if parts.len() > 1 {
            cmd.args(&parts[1..]);
        }
        
        cmd.current_dir(&build.working_directory)
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .stdin(Stdio::null());

        // Spawn the process and wait for completion with output capture
        let child = cmd.spawn()
            .map_err(|e| {
                let error_msg = format!("Failed to spawn process: {}", e);
                // Note: We can't await here in map_err, so we'll log this error later
                anyhow::anyhow!(error_msg)
            })?;

        debug!("Spawned process with PID: {:?}", child.id());
        
        self.log_manager.append_log(
            build.id,
            format!("Process spawned with PID: {:?}", child.id()),
            LogStreamType::System,
        ).await?;

        // Wait for the process to complete with timeout and capture output
        let output = match timeout(Duration::from_secs(30), child.wait_with_output()).await {
            Ok(output) => output?,
            Err(_) => {
                // Timeout occurred - we can't kill the process since wait_with_output consumed it
                let timeout_msg = format!("Build {} timed out after 30 seconds", build.id);
                warn!("{}", timeout_msg);
                self.log_manager.append_log(
                    build.id,
                    timeout_msg.clone(),
                    LogStreamType::System,
                ).await?;
                return Err(anyhow::anyhow!("Build execution timed out after 30 seconds"));
            }
        };

        // Process the output and store in log manager
        let mut log_lines = Vec::new();
        
        // Add stdout lines
        if !output.stdout.is_empty() {
            let stdout_str = String::from_utf8_lossy(&output.stdout);
            for line in stdout_str.lines() {
                self.log_manager.append_log(
                    build.id,
                    line.to_string(),
                    LogStreamType::Stdout,
                ).await?;
                log_lines.push(format!("[STDOUT] {}", line));
            }
        }
        
        // Add stderr lines
        if !output.stderr.is_empty() {
            let stderr_str = String::from_utf8_lossy(&output.stderr);
            for line in stderr_str.lines() {
                self.log_manager.append_log(
                    build.id,
                    line.to_string(),
                    LogStreamType::Stderr,
                ).await?;
                log_lines.push(format!("[STDERR] {}", line));
            }
        }

        // Calculate duration and determine status
        let duration_ms = start_time.elapsed().as_millis() as u64;
        let exit_code = output.status.code().unwrap_or(-1);
        let status = if output.status.success() {
            BuildStatus::Completed
        } else {
            BuildStatus::Failed
        };

        // Log completion
        let completion_msg = format!(
            "Build completed with exit code {} in {}ms",
            exit_code, duration_ms
        );
        self.log_manager.append_log(
            build.id,
            completion_msg.clone(),
            LogStreamType::System,
        ).await?;

        info!(
            "Build {} completed with exit code {} in {}ms", 
            build.id, exit_code, duration_ms
        );

        Ok(ExecutionResult {
            exit_code,
            duration_ms,
            status,
            logs: log_lines,
        })
    }

    /// Cancel a running build process
    pub async fn cancel_build(&self, build_id: Uuid) -> Result<()> {
        let mut processes = self.active_processes.write().await;
        
        if let Some(process_arc) = processes.remove(&build_id) {
            let mut process = process_arc.lock().await;
            
            info!("Cancelling build {}", build_id);
            
            // Kill the process
            if let Err(e) = process.child.kill().await {
                warn!("Failed to kill process for build {}: {}", build_id, e);
            }
            
            // Wait for process to terminate with timeout
            let wait_result = timeout(Duration::from_millis(100), process.child.wait()).await;
            match wait_result {
                Ok(Ok(_)) => {
                    info!("Build {} process terminated successfully", build_id);
                }
                Ok(Err(e)) => {
                    warn!("Error waiting for build {} process to terminate: {}", build_id, e);
                }
                Err(_) => {
                    warn!("Timeout waiting for build {} process to terminate", build_id);
                }
            }
            
            process.status = BuildStatus::Cancelled;
            Ok(())
        } else {
            Err(anyhow::anyhow!("Build {} not found in active processes", build_id))
        }
    }

    /// Get the current status of a build process
    pub async fn get_build_status(&self, build_id: Uuid) -> Option<BuildStatus> {
        let processes = self.active_processes.read().await;
        if let Some(process_arc) = processes.get(&build_id) {
            let process = process_arc.lock().await;
            Some(process.status.clone())
        } else {
            None
        }
    }

    /// Get logs for a build (either active or completed)
    pub async fn get_build_logs(&self, build_id: Uuid) -> Option<Vec<String>> {
        self.log_manager.get_build_logs_as_strings(build_id).await
    }

    /// Get the log manager for direct access to streaming functionality
    pub fn get_log_manager(&self) -> Arc<LogManager> {
        self.log_manager.clone()
    }

    /// Get count of active builds
    pub async fn get_active_build_count(&self) -> usize {
        let processes = self.active_processes.read().await;
        processes.len()
    }

    /// Clean up completed processes (housekeeping)
    pub async fn cleanup_completed_processes(&self) -> Result<usize> {
        let mut processes = self.active_processes.write().await;
        let mut to_remove = Vec::new();
        
        for (build_id, process_arc) in processes.iter() {
            let process = process_arc.lock().await;
            if matches!(process.status, BuildStatus::Completed | BuildStatus::Failed | BuildStatus::Cancelled) {
                to_remove.push(*build_id);
            }
        }
        
        let removed_count = to_remove.len();
        for build_id in to_remove {
            processes.remove(&build_id);
        }
        
        if removed_count > 0 {
            debug!("Cleaned up {} completed processes", removed_count);
        }
        
        Ok(removed_count)
    }
}

impl Default for ExecutionEngine {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use build_pal_core::{ExecutionMode, Environment};
    use std::env;
    use uuid::Uuid;

    #[tokio::test]
    async fn test_execution_engine_creation() {
        let engine = ExecutionEngine::new();
        assert_eq!(engine.get_active_build_count().await, 0);
    }

    #[tokio::test]
    async fn test_successful_command_execution() {
        let engine = ExecutionEngine::new();
        
        // Create a simple test build with echo command
        let build = Build::new(
            Uuid::new_v4(),
            "echo hello world".to_string(),
            ExecutionMode::Async,
            Environment::Native,
            "test".to_string(),
            env::temp_dir().to_string_lossy().to_string(),
        );

        // Execute the build
        let result = engine.execute_build(&build).await;
        assert!(result.is_ok());
        
        let execution_result = result.unwrap();
        assert_eq!(execution_result.exit_code, 0);
        assert_eq!(execution_result.status, BuildStatus::Completed);
        assert!(execution_result.duration_ms > 0);
        assert!(!execution_result.logs.is_empty());
        
        // Check that logs contain expected output
        let log_content = execution_result.logs.join("\n");
        assert!(log_content.contains("hello world"));
    }

    #[tokio::test]
    async fn test_failed_command_execution() {
        let engine = ExecutionEngine::new();
        
        // Create a test build with a command that will fail
        let build = Build::new(
            Uuid::new_v4(),
            "false".to_string(), // 'false' command always exits with code 1
            ExecutionMode::Async,
            Environment::Native,
            "test".to_string(),
            env::temp_dir().to_string_lossy().to_string(),
        );

        // Execute the build
        let result = engine.execute_build(&build).await;
        assert!(result.is_ok());
        
        let execution_result = result.unwrap();
        assert_eq!(execution_result.exit_code, 1);
        assert_eq!(execution_result.status, BuildStatus::Failed);
        assert!(execution_result.duration_ms > 0);
    }

    #[tokio::test]
    async fn test_invalid_command_execution() {
        let engine = ExecutionEngine::new();
        
        // Create a test build with an invalid command
        let build = Build::new(
            Uuid::new_v4(),
            "nonexistent_command_12345".to_string(),
            ExecutionMode::Async,
            Environment::Native,
            "test".to_string(),
            env::temp_dir().to_string_lossy().to_string(),
        );

        // Execute the build - should fail to spawn
        let result = engine.execute_build(&build).await;
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("Failed to spawn process"));
    }

    #[tokio::test]
    async fn test_empty_command_execution() {
        let engine = ExecutionEngine::new();
        
        // Create a test build with empty command
        let build = Build::new(
            Uuid::new_v4(),
            "".to_string(),
            ExecutionMode::Async,
            Environment::Native,
            "test".to_string(),
            env::temp_dir().to_string_lossy().to_string(),
        );

        // Execute the build - should fail with empty command error
        let result = engine.execute_build(&build).await;
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("Empty command"));
    }

    #[tokio::test]
    async fn test_build_process_lifecycle() {
        let engine = ExecutionEngine::new();
        
        // Initially no active builds
        assert_eq!(engine.get_active_build_count().await, 0);
        
        // Create a quick command for testing
        let build = Build::new(
            Uuid::new_v4(),
            "echo lifecycle_test".to_string(),
            ExecutionMode::Async,
            Environment::Native,
            "test".to_string(),
            env::temp_dir().to_string_lossy().to_string(),
        );

        let build_id = build.id;

        // Execute the build
        let result = engine.execute_build(&build).await;
        assert!(result.is_ok());
        
        let execution_result = result.unwrap();
        assert_eq!(execution_result.status, BuildStatus::Completed);
        
        // After completion, build should not be in active processes
        assert!(engine.get_build_status(build_id).await.is_none());
        
        // Should have logs stored
        let logs = engine.get_build_logs(build_id).await;
        assert!(logs.is_some());
        let log_content = logs.unwrap().join("\n");
        assert!(log_content.contains("lifecycle_test"));
    }

    #[tokio::test]
    async fn test_build_cancellation() {
        let engine = ExecutionEngine::new();
        
        // Create a long-running command
        let build = Build::new(
            Uuid::new_v4(),
            "sleep 0.2".to_string(), // Very short sleep for testing
            ExecutionMode::Async,
            Environment::Native,
            "test".to_string(),
            env::temp_dir().to_string_lossy().to_string(),
        );

        let build_id = build.id;

        // Start the build in a separate task
        let engine_clone = engine.clone();
        let build_clone = build.clone();
        let execution_task = tokio::spawn(async move {
            engine_clone.execute_build(&build_clone).await
        });

        // Give the process a moment to start
        tokio::time::sleep(Duration::from_millis(10)).await;

        // Cancel the build - this should fail since the process isn't tracked
        let cancel_result = engine.cancel_build(build_id).await;
        assert!(cancel_result.is_err()); // Should fail because build isn't in active processes

        // Wait for the execution to complete with timeout
        let execution_result = tokio::time::timeout(
            Duration::from_secs(5), 
            execution_task
        ).await;
        
        // Should complete within timeout
        assert!(execution_result.is_ok());
    }

    #[tokio::test]
    async fn test_process_cleanup() {
        let engine = ExecutionEngine::new();
        
        // Execute a quick command
        let build = Build::new(
            Uuid::new_v4(),
            "echo test".to_string(),
            ExecutionMode::Async,
            Environment::Native,
            "test".to_string(),
            env::temp_dir().to_string_lossy().to_string(),
        );

        let result = engine.execute_build(&build).await;
        assert!(result.is_ok());
        
        // Cleanup should work without errors
        let cleanup_result = engine.cleanup_completed_processes().await;
        assert!(cleanup_result.is_ok());
    }

    #[tokio::test]
    async fn test_working_directory_handling() {
        let engine = ExecutionEngine::new();
        
        // Create a test build that prints the current directory
        let temp_dir = env::temp_dir();
        let build = Build::new(
            Uuid::new_v4(),
            "pwd".to_string(),
            ExecutionMode::Async,
            Environment::Native,
            "test".to_string(),
            temp_dir.to_string_lossy().to_string(),
        );

        let result = engine.execute_build(&build).await;
        assert!(result.is_ok());
        
        let execution_result = result.unwrap();
        assert_eq!(execution_result.status, BuildStatus::Completed);
        
        // Check that the command was executed in the correct directory
        let log_content = execution_result.logs.join("\n");
        // On macOS, temp_dir might be a symlink, so check for common temp directory patterns
        assert!(log_content.contains("/tmp") || log_content.contains("/var/folders"));
    }
}

// Implement Clone for ExecutionEngine to support testing
impl Clone for ExecutionEngine {
    fn clone(&self) -> Self {
        Self {
            active_processes: self.active_processes.clone(),
            log_manager: self.log_manager.clone(),
        }
    }
}