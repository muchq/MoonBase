use anyhow::Result;
use serde::{Serialize, Deserialize};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::{mpsc, Mutex, RwLock};
use tokio_stream::{wrappers::UnboundedReceiverStream, Stream, StreamExt};
use tracing::{debug, info, warn};
use uuid::Uuid;

/// A single log entry with metadata
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LogEntry {
    pub line_number: usize,
    pub timestamp: chrono::DateTime<chrono::Utc>,
    pub content: String,
    pub stream_type: LogStreamType,
}

/// Type of log stream (stdout, stderr, etc.)
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum LogStreamType {
    Stdout,
    Stderr,
    System,
}

/// Log storage for a single build
#[derive(Debug)]
struct BuildLogStorage {
    entries: Vec<LogEntry>,
    active_streams: Vec<mpsc::UnboundedSender<LogEntry>>,
}

/// In-memory log storage and streaming manager
pub struct LogManager {
    /// Storage for build logs indexed by build ID
    build_logs: Arc<RwLock<HashMap<Uuid, Arc<Mutex<BuildLogStorage>>>>>,
}

impl LogManager {
    pub fn new() -> Self {
        Self {
            build_logs: Arc::new(RwLock::new(HashMap::new())),
        }
    }

    /// Initialize log storage for a new build
    pub async fn initialize_build_logs(&self, build_id: Uuid) -> Result<()> {
        let mut logs = self.build_logs.write().await;
        
        if logs.contains_key(&build_id) {
            warn!("Log storage already exists for build {}", build_id);
            return Ok(());
        }

        let storage = BuildLogStorage {
            entries: Vec::new(),
            active_streams: Vec::new(),
        };

        logs.insert(build_id, Arc::new(Mutex::new(storage)));
        info!("Initialized log storage for build {}", build_id);
        Ok(())
    }

    /// Append a log entry to a build's logs
    pub async fn append_log(&self, build_id: Uuid, content: String, stream_type: LogStreamType) -> Result<()> {
        let logs = self.build_logs.read().await;
        
        if let Some(storage_arc) = logs.get(&build_id) {
            let mut storage = storage_arc.lock().await;
            
            let entry = LogEntry {
                line_number: storage.entries.len() + 1,
                timestamp: chrono::Utc::now(),
                content,
                stream_type,
            };

            // Add to storage
            storage.entries.push(entry.clone());

            // Send to active streams
            let mut failed_streams = Vec::new();
            for (idx, sender) in storage.active_streams.iter().enumerate() {
                if sender.send(entry.clone()).is_err() {
                    failed_streams.push(idx);
                }
            }

            // Remove failed streams (receivers dropped)
            for &idx in failed_streams.iter().rev() {
                storage.active_streams.remove(idx);
            }

            if !failed_streams.is_empty() {
                debug!("Removed {} failed log streams for build {}", failed_streams.len(), build_id);
            }

            Ok(())
        } else {
            Err(anyhow::anyhow!("No log storage found for build {}", build_id))
        }
    }

    /// Get all logs for a build
    pub async fn get_build_logs(&self, build_id: Uuid) -> Option<Vec<LogEntry>> {
        let logs = self.build_logs.read().await;
        
        if let Some(storage_arc) = logs.get(&build_id) {
            let storage = storage_arc.lock().await;
            Some(storage.entries.clone())
        } else {
            None
        }
    }

    /// Get logs as formatted strings for a build
    pub async fn get_build_logs_as_strings(&self, build_id: Uuid) -> Option<Vec<String>> {
        let logs = self.get_build_logs(build_id).await?;
        
        Some(logs.into_iter().map(|entry| {
            let stream_prefix = match entry.stream_type {
                LogStreamType::Stdout => "[STDOUT]",
                LogStreamType::Stderr => "[STDERR]",
                LogStreamType::System => "[SYSTEM]",
            };
            format!("{} {}", stream_prefix, entry.content)
        }).collect())
    }

    /// Create a stream of log entries for real-time consumption
    pub async fn create_log_stream(&self, build_id: Uuid) -> Result<impl Stream<Item = LogEntry>> {
        let logs = self.build_logs.read().await;
        
        if let Some(storage_arc) = logs.get(&build_id) {
            let mut storage = storage_arc.lock().await;
            
            let (sender, receiver) = mpsc::unbounded_channel();
            
            // Send existing logs first
            for entry in &storage.entries {
                if sender.send(entry.clone()).is_err() {
                    return Err(anyhow::anyhow!("Failed to send existing logs to stream"));
                }
            }
            
            // Register for future logs
            storage.active_streams.push(sender);
            
            Ok(UnboundedReceiverStream::new(receiver))
        } else {
            Err(anyhow::anyhow!("No log storage found for build {}", build_id))
        }
    }

    /// Get the number of log entries for a build
    pub async fn get_log_count(&self, build_id: Uuid) -> usize {
        let logs = self.build_logs.read().await;
        
        if let Some(storage_arc) = logs.get(&build_id) {
            let storage = storage_arc.lock().await;
            storage.entries.len()
        } else {
            0
        }
    }

    /// Get the number of active streams for a build
    pub async fn get_active_stream_count(&self, build_id: Uuid) -> usize {
        let logs = self.build_logs.read().await;
        
        if let Some(storage_arc) = logs.get(&build_id) {
            let storage = storage_arc.lock().await;
            storage.active_streams.len()
        } else {
            0
        }
    }

    /// Clean up logs for a build (useful for retention policies)
    pub async fn cleanup_build_logs(&self, build_id: Uuid) -> Result<()> {
        let mut logs = self.build_logs.write().await;
        
        if logs.remove(&build_id).is_some() {
            info!("Cleaned up logs for build {}", build_id);
            Ok(())
        } else {
            Err(anyhow::anyhow!("No logs found for build {}", build_id))
        }
    }

    /// Get total number of builds with logs
    pub async fn get_total_build_count(&self) -> usize {
        let logs = self.build_logs.read().await;
        logs.len()
    }

    /// Clean up all logs (for testing or maintenance)
    pub async fn cleanup_all_logs(&self) -> usize {
        let mut logs = self.build_logs.write().await;
        let count = logs.len();
        logs.clear();
        info!("Cleaned up logs for {} builds", count);
        count
    }
}

impl Default for LogManager {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tokio::time::{sleep, Duration};
    use uuid::Uuid;

    #[tokio::test]
    async fn test_log_manager_creation() {
        let log_manager = LogManager::new();
        assert_eq!(log_manager.get_total_build_count().await, 0);
    }

    #[tokio::test]
    async fn test_initialize_build_logs() {
        let log_manager = LogManager::new();
        let build_id = Uuid::new_v4();

        // Initialize logs for a build
        let result = log_manager.initialize_build_logs(build_id).await;
        assert!(result.is_ok());
        assert_eq!(log_manager.get_total_build_count().await, 1);

        // Initialize again should not fail
        let result = log_manager.initialize_build_logs(build_id).await;
        assert!(result.is_ok());
        assert_eq!(log_manager.get_total_build_count().await, 1);
    }

    #[tokio::test]
    async fn test_append_and_retrieve_logs() {
        let log_manager = LogManager::new();
        let build_id = Uuid::new_v4();

        // Initialize logs
        log_manager.initialize_build_logs(build_id).await.unwrap();

        // Append some logs
        log_manager.append_log(build_id, "Hello world".to_string(), LogStreamType::Stdout).await.unwrap();
        log_manager.append_log(build_id, "Error occurred".to_string(), LogStreamType::Stderr).await.unwrap();
        log_manager.append_log(build_id, "Build started".to_string(), LogStreamType::System).await.unwrap();

        // Retrieve logs
        let logs = log_manager.get_build_logs(build_id).await;
        assert!(logs.is_some());
        
        let log_entries = logs.unwrap();
        assert_eq!(log_entries.len(), 3);
        
        assert_eq!(log_entries[0].content, "Hello world");
        assert_eq!(log_entries[0].stream_type, LogStreamType::Stdout);
        assert_eq!(log_entries[0].line_number, 1);
        
        assert_eq!(log_entries[1].content, "Error occurred");
        assert_eq!(log_entries[1].stream_type, LogStreamType::Stderr);
        assert_eq!(log_entries[1].line_number, 2);
        
        assert_eq!(log_entries[2].content, "Build started");
        assert_eq!(log_entries[2].stream_type, LogStreamType::System);
        assert_eq!(log_entries[2].line_number, 3);
    }

    #[tokio::test]
    async fn test_get_logs_as_strings() {
        let log_manager = LogManager::new();
        let build_id = Uuid::new_v4();

        // Initialize and add logs
        log_manager.initialize_build_logs(build_id).await.unwrap();
        log_manager.append_log(build_id, "stdout message".to_string(), LogStreamType::Stdout).await.unwrap();
        log_manager.append_log(build_id, "stderr message".to_string(), LogStreamType::Stderr).await.unwrap();

        // Get formatted strings
        let log_strings = log_manager.get_build_logs_as_strings(build_id).await;
        assert!(log_strings.is_some());
        
        let strings = log_strings.unwrap();
        assert_eq!(strings.len(), 2);
        assert_eq!(strings[0], "[STDOUT] stdout message");
        assert_eq!(strings[1], "[STDERR] stderr message");
    }

    #[tokio::test]
    async fn test_log_streaming() {
        let log_manager = LogManager::new();
        let build_id = Uuid::new_v4();

        // Initialize logs
        log_manager.initialize_build_logs(build_id).await.unwrap();

        // Add some initial logs
        log_manager.append_log(build_id, "Initial log".to_string(), LogStreamType::Stdout).await.unwrap();

        // Create a stream
        let mut stream = log_manager.create_log_stream(build_id).await.unwrap();

        // Should receive the initial log immediately
        let first_entry = stream.next().await;
        assert!(first_entry.is_some());
        assert_eq!(first_entry.unwrap().content, "Initial log");

        // Add more logs in a separate task
        let log_manager_clone = log_manager.clone();
        let build_id_clone = build_id;
        tokio::spawn(async move {
            sleep(Duration::from_millis(10)).await;
            log_manager_clone.append_log(build_id_clone, "Streamed log 1".to_string(), LogStreamType::Stdout).await.unwrap();
            sleep(Duration::from_millis(10)).await;
            log_manager_clone.append_log(build_id_clone, "Streamed log 2".to_string(), LogStreamType::Stderr).await.unwrap();
        });

        // Should receive the streamed logs
        let second_entry = stream.next().await;
        assert!(second_entry.is_some());
        assert_eq!(second_entry.unwrap().content, "Streamed log 1");

        let third_entry = stream.next().await;
        assert!(third_entry.is_some());
        assert_eq!(third_entry.unwrap().content, "Streamed log 2");
    }

    #[tokio::test]
    async fn test_concurrent_log_access() {
        let log_manager = Arc::new(LogManager::new());
        let build_id = Uuid::new_v4();

        // Initialize logs
        log_manager.initialize_build_logs(build_id).await.unwrap();

        // Spawn multiple tasks that append logs concurrently
        let mut handles = Vec::new();
        
        for i in 0..10 {
            let log_manager_clone = log_manager.clone();
            let build_id_clone = build_id;
            
            let handle = tokio::spawn(async move {
                for j in 0..5 {
                    let message = format!("Task {} - Log {}", i, j);
                    log_manager_clone.append_log(build_id_clone, message, LogStreamType::Stdout).await.unwrap();
                }
            });
            
            handles.push(handle);
        }

        // Wait for all tasks to complete
        for handle in handles {
            handle.await.unwrap();
        }

        // Verify all logs were stored
        let log_count = log_manager.get_log_count(build_id).await;
        assert_eq!(log_count, 50); // 10 tasks * 5 logs each

        let logs = log_manager.get_build_logs(build_id).await.unwrap();
        assert_eq!(logs.len(), 50);

        // Verify line numbers are sequential
        for (idx, log) in logs.iter().enumerate() {
            assert_eq!(log.line_number, idx + 1);
        }
    }

    #[tokio::test]
    async fn test_multiple_streams() {
        let log_manager = LogManager::new();
        let build_id = Uuid::new_v4();

        // Initialize logs
        log_manager.initialize_build_logs(build_id).await.unwrap();

        // Create multiple streams
        let mut stream1 = log_manager.create_log_stream(build_id).await.unwrap();
        let mut stream2 = log_manager.create_log_stream(build_id).await.unwrap();

        // Verify active stream count
        assert_eq!(log_manager.get_active_stream_count(build_id).await, 2);

        // Add a log
        log_manager.append_log(build_id, "Broadcast message".to_string(), LogStreamType::Stdout).await.unwrap();

        // Both streams should receive the message
        let entry1 = stream1.next().await;
        let entry2 = stream2.next().await;

        assert!(entry1.is_some());
        assert!(entry2.is_some());
        assert_eq!(entry1.unwrap().content, "Broadcast message");
        assert_eq!(entry2.unwrap().content, "Broadcast message");
    }

    #[tokio::test]
    async fn test_stream_cleanup_on_drop() {
        let log_manager = LogManager::new();
        let build_id = Uuid::new_v4();

        // Initialize logs
        log_manager.initialize_build_logs(build_id).await.unwrap();

        // Create a stream and drop it
        {
            let _stream = log_manager.create_log_stream(build_id).await.unwrap();
            assert_eq!(log_manager.get_active_stream_count(build_id).await, 1);
        } // stream dropped here

        // Add a log to trigger cleanup of failed streams
        log_manager.append_log(build_id, "Test message".to_string(), LogStreamType::Stdout).await.unwrap();

        // Stream should be cleaned up
        assert_eq!(log_manager.get_active_stream_count(build_id).await, 0);
    }

    #[tokio::test]
    async fn test_append_to_nonexistent_build() {
        let log_manager = LogManager::new();
        let build_id = Uuid::new_v4();

        // Try to append without initializing
        let result = log_manager.append_log(build_id, "Test".to_string(), LogStreamType::Stdout).await;
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("No log storage found"));
    }

    #[tokio::test]
    async fn test_cleanup_build_logs() {
        let log_manager = LogManager::new();
        let build_id = Uuid::new_v4();

        // Initialize and add logs
        log_manager.initialize_build_logs(build_id).await.unwrap();
        log_manager.append_log(build_id, "Test log".to_string(), LogStreamType::Stdout).await.unwrap();

        assert_eq!(log_manager.get_total_build_count().await, 1);
        assert_eq!(log_manager.get_log_count(build_id).await, 1);

        // Cleanup
        let result = log_manager.cleanup_build_logs(build_id).await;
        assert!(result.is_ok());

        assert_eq!(log_manager.get_total_build_count().await, 0);
        assert_eq!(log_manager.get_log_count(build_id).await, 0);
    }

    #[tokio::test]
    async fn test_cleanup_all_logs() {
        let log_manager = LogManager::new();
        
        // Create multiple builds with logs
        for i in 0..5 {
            let build_id = Uuid::new_v4();
            log_manager.initialize_build_logs(build_id).await.unwrap();
            log_manager.append_log(build_id, format!("Log {}", i), LogStreamType::Stdout).await.unwrap();
        }

        assert_eq!(log_manager.get_total_build_count().await, 5);

        // Cleanup all
        let cleaned_count = log_manager.cleanup_all_logs().await;
        assert_eq!(cleaned_count, 5);
        assert_eq!(log_manager.get_total_build_count().await, 0);
    }
}

// Implement Clone for LogManager to support testing
impl Clone for LogManager {
    fn clone(&self) -> Self {
        Self {
            build_logs: self.build_logs.clone(),
        }
    }
}