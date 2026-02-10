use std::collections::HashMap;
use std::fs;
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};
use uuid::Uuid;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Project {
    pub id: Uuid,
    pub name: String,
    pub role: String,
    #[serde(default)]
    pub jira_projects: Vec<String>,
    #[serde(default)]
    pub git_repos: Vec<String>,
}

impl Project {
    pub fn new(name: &str, role: &str) -> Self {
        Self {
            id: Uuid::new_v4(),
            name: name.to_string(),
            role: role.to_string(),
            jira_projects: Vec::new(),
            git_repos: Vec::new(),
        }
    }

    pub fn with_jira_projects(mut self, projects: Vec<String>) -> Self {
        self.jira_projects = projects;
        self
    }

    pub fn with_git_repos(mut self, repos: Vec<String>) -> Self {
        self.git_repos = repos;
        self
    }
}

pub struct ProjectStore {
    path: PathBuf,
    projects: HashMap<Uuid, Project>,
}

impl ProjectStore {
    pub fn open(dir: &Path) -> Result<Self, StoreError> {
        let path = dir.join("projects.json");
        let projects = if path.exists() {
            let data = fs::read_to_string(&path).map_err(StoreError::Io)?;
            let list: Vec<Project> = serde_json::from_str(&data).map_err(StoreError::Parse)?;
            list.into_iter().map(|p| (p.id, p)).collect()
        } else {
            HashMap::new()
        };

        Ok(Self { path, projects })
    }

    pub fn save(&self) -> Result<(), StoreError> {
        let list: Vec<&Project> = self.projects.values().collect();
        let data = serde_json::to_string_pretty(&list).map_err(StoreError::Parse)?;
        fs::write(&self.path, data).map_err(StoreError::Io)?;
        Ok(())
    }

    pub fn insert(&mut self, project: Project) -> Result<(), StoreError> {
        self.projects.insert(project.id, project);
        self.save()
    }

    pub fn remove(&mut self, id: Uuid) -> Result<Option<Project>, StoreError> {
        let removed = self.projects.remove(&id);
        if removed.is_some() {
            self.save()?;
        }
        Ok(removed)
    }

    pub fn remove_by_name(&mut self, name: &str) -> Result<Option<Project>, StoreError> {
        let id = self.projects.values().find(|p| p.name == name).map(|p| p.id);
        if let Some(id) = id {
            self.remove(id)
        } else {
            Ok(None)
        }
    }

    pub fn all(&self) -> Vec<&Project> {
        self.projects.values().collect()
    }
}

#[derive(Debug)]
pub enum StoreError {
    Io(std::io::Error),
    Parse(serde_json::Error),
}

impl std::fmt::Display for StoreError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Io(e) => write!(f, "store I/O error: {e}"),
            Self::Parse(e) => write!(f, "store parse error: {e}"),
        }
    }
}

impl std::error::Error for StoreError {}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    fn tmp_store() -> (TempDir, ProjectStore) {
        let dir = TempDir::new().unwrap();
        let store = ProjectStore::open(dir.path()).unwrap();
        (dir, store)
    }

    #[test]
    fn test_add_remove_project() {
        let (_dir, mut store) = tmp_store();

        let project = Project::new("Project A", "Lead");
        store.insert(project.clone()).unwrap();

        assert_eq!(store.all().len(), 1);
        assert_eq!(store.all()[0].name, "Project A");

        store.remove(project.id).unwrap();
        assert_eq!(store.all().len(), 0);
    }

    #[test]
    fn test_persistence() {
        let (dir, mut store) = tmp_store();
        let project = Project::new("Project B", "Dev")
            .with_jira_projects(vec!["JIRA-1".to_string()])
            .with_git_repos(vec!["repo/b".to_string()]);

        store.insert(project).unwrap();

        // Re-open store
        let store2 = ProjectStore::open(dir.path()).unwrap();
        assert_eq!(store2.all().len(), 1);
        let p = store2.all()[0];
        assert_eq!(p.name, "Project B");
        assert_eq!(p.jira_projects, vec!["JIRA-1"]);
    }
}
