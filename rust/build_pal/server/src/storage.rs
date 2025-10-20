use build_pal_core::{Build, Project};
use anyhow::Result;
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::RwLock;
use uuid::Uuid;

/// Storage manager for builds and projects
pub struct StorageManager {
    builds: Arc<RwLock<HashMap<Uuid, Build>>>,
    projects: Arc<RwLock<HashMap<Uuid, Project>>>,
}

impl StorageManager {
    pub fn new() -> Self {
        Self {
            builds: Arc::new(RwLock::new(HashMap::new())),
            projects: Arc::new(RwLock::new(HashMap::new())),
        }
    }

    /// Store a build
    pub async fn store_build(&self, build: Build) -> Result<()> {
        let mut builds = self.builds.write().await;
        builds.insert(build.id, build);
        Ok(())
    }

    /// Get a build by ID
    pub async fn get_build(&self, build_id: Uuid) -> Option<Build> {
        let builds = self.builds.read().await;
        builds.get(&build_id).cloned()
    }

    /// Store a project
    pub async fn store_project(&self, project: Project) -> Result<()> {
        let mut projects = self.projects.write().await;
        projects.insert(project.id, project);
        Ok(())
    }

    /// Get a project by ID
    pub async fn get_project(&self, project_id: Uuid) -> Option<Project> {
        let projects = self.projects.read().await;
        projects.get(&project_id).cloned()
    }

    /// List all projects
    pub async fn list_projects(&self) -> Vec<Project> {
        let projects = self.projects.read().await;
        projects.values().cloned().collect()
    }

    /// List builds for a project
    pub async fn list_project_builds(&self, project_id: Uuid) -> Vec<Build> {
        let builds = self.builds.read().await;
        builds.values()
            .filter(|build| build.project_id == project_id)
            .cloned()
            .collect()
    }
}

impl Default for StorageManager {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use build_pal_core::{BuildTool, ExecutionMode, Environment};

    #[tokio::test]
    async fn test_storage_manager() {
        let storage = StorageManager::new();
        
        // Create and store a project
        let project = Project::new(
            "test-project".to_string(),
            "/path/to/project".to_string(),
            BuildTool::Bazel,
        );
        let project_id = project.id;
        
        storage.store_project(project.clone()).await.unwrap();
        
        // Retrieve the project
        let retrieved_project = storage.get_project(project_id).await;
        assert!(retrieved_project.is_some());
        assert_eq!(retrieved_project.unwrap().name, "test-project");
        
        // Create and store a build
        let build = Build::new(
            project_id,
            "bazel build //...".to_string(),
            ExecutionMode::Async,
            Environment::Native,
            "test".to_string(),
            "/path/to/project".to_string(),
        );
        let build_id = build.id;
        
        storage.store_build(build.clone()).await.unwrap();
        
        // Retrieve the build
        let retrieved_build = storage.get_build(build_id).await;
        assert!(retrieved_build.is_some());
        assert_eq!(retrieved_build.unwrap().command, "bazel build //...");
        
        // List project builds
        let project_builds = storage.list_project_builds(project_id).await;
        assert_eq!(project_builds.len(), 1);
        assert_eq!(project_builds[0].id, build_id);
    }
}