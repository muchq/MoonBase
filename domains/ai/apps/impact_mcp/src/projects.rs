use std::collections::HashMap;
use std::fs;
use std::path::{Path, PathBuf};

use crate::cli::ProjectsCommand;
use serde::{Deserialize, Serialize};
use uuid::Uuid;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Project {
    pub id: Uuid,
    pub name: String,
    pub role: String,
    #[serde(default = "default_status")]
    pub status: String,
    #[serde(default)]
    pub completion: f64,
    #[serde(default)]
    pub jira_projects: Vec<String>,
    #[serde(default)]
    pub git_repos: Vec<String>,
}

fn default_status() -> String {
    "Active".to_string()
}

impl Project {
    pub fn new(name: &str, role: &str) -> Self {
        Self {
            id: Uuid::new_v4(),
            name: name.to_string(),
            role: role.to_string(),
            status: "Active".to_string(),
            completion: 0.0,
            jira_projects: Vec::new(),
            git_repos: Vec::new(),
        }
    }

    pub fn with_status(mut self, status: &str) -> Self {
        self.status = status.to_string();
        self
    }

    pub fn with_completion(mut self, completion: f64) -> Self {
        self.completion = completion.clamp(0.0, 1.0);
        self
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

pub fn handle_project_command<W: std::io::Write>(
    store: &mut ProjectStore,
    command: ProjectsCommand,
    writer: &mut W,
) -> std::io::Result<()> {
    match command {
        ProjectsCommand::List => {
            let projects = store.all();
            if projects.is_empty() {
                writeln!(writer, "No tracked projects. Use `impact-mcp projects add`.")?;
                return Ok(());
            }
            writeln!(writer, "{} tracked project(s):\n", projects.len())?;
            for p in &projects {
                writeln!(
                    writer,
                    "  * {} (Role: {}) — {} ({:.0}%)",
                    p.name,
                    p.role,
                    p.status,
                    p.completion * 100.0
                )?;
                if !p.jira_projects.is_empty() {
                    writeln!(writer, "    Jira: {}", p.jira_projects.join(", "))?;
                }
                if !p.git_repos.is_empty() {
                    writeln!(writer, "    Repos: {}", p.git_repos.join(", "))?;
                }
            }
        }
        ProjectsCommand::Add {
            name,
            role,
            jira,
            repos,
            status,
            completion,
        } => {
            let mut project = Project::new(&name, &role)
                .with_status(&status)
                .with_completion(completion);

            if let Some(j) = jira {
                project =
                    project.with_jira_projects(j.split(',').map(|s| s.trim().to_string()).collect());
            }
            if let Some(r) = repos {
                project =
                    project.with_git_repos(r.split(',').map(|s| s.trim().to_string()).collect());
            }

            match store.insert(project) {
                Ok(()) => writeln!(writer, "Project \"{}\" added.", name)?,
                Err(e) => {
                    return Err(std::io::Error::new(std::io::ErrorKind::Other, e.to_string()));
                }
            }
        }
        ProjectsCommand::Update {
            name,
            role,
            status,
            completion,
            jira,
            repos,
        } => {
            let mut project = match store.all().into_iter().find(|p| p.name == name) {
                Some(p) => p.clone(),
                None => {
                    writeln!(writer, "Project \"{}\" not found.", name)?;
                    return Ok(());
                }
            };

            if let Some(r) = role {
                project.role = r;
            }
            if let Some(ref s) = status {
                project = project.with_status(s);
            }
            if let Some(c) = completion {
                project = project.with_completion(c);
            }
            if let Some(j) = jira {
                project =
                    project.with_jira_projects(j.split(',').map(|s| s.trim().to_string()).collect());
            }
            if let Some(r) = repos {
                project =
                    project.with_git_repos(r.split(',').map(|s| s.trim().to_string()).collect());
            }

            match store.insert(project) {
                Ok(()) => writeln!(writer, "Project \"{}\" updated.", name)?,
                Err(e) => {
                    return Err(std::io::Error::new(std::io::ErrorKind::Other, e.to_string()));
                }
            }
        }
        ProjectsCommand::Remove { name } => {
            match store.remove_by_name(&name) {
                Ok(Some(_)) => writeln!(writer, "Project \"{}\" removed.", name)?,
                Ok(None) => writeln!(writer, "Project \"{}\" not found.", name)?,
                Err(e) => {
                    return Err(std::io::Error::new(std::io::ErrorKind::Other, e.to_string()));
                }
            }
        }
    }
    Ok(())
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
    use crate::cli::ProjectsCommand;
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

    #[test]
    fn test_handle_add_project() {
        let (_dir, mut store) = tmp_store();
        let mut output = Vec::new();

        let cmd = ProjectsCommand::Add {
            name: "New Project".to_string(),
            role: "Owner".to_string(),
            jira: Some("JIRA-123".to_string()),
            repos: Some("repo/new".to_string()),
            status: "Active".to_string(),
            completion: 0.25,
        };

        handle_project_command(&mut store, cmd, &mut output).unwrap();

        let out_str = String::from_utf8(output).unwrap();
        assert!(out_str.contains("Project \"New Project\" added."));

        let projects = store.all();
        assert_eq!(projects.len(), 1);
        let p = projects[0];
        assert_eq!(p.name, "New Project");
        assert_eq!(p.role, "Owner");
        assert_eq!(p.status, "Active");
        assert_eq!(p.completion, 0.25);
        assert_eq!(p.jira_projects, vec!["JIRA-123"]);
        assert_eq!(p.git_repos, vec!["repo/new"]);
    }

    #[test]
    fn test_handle_update_project() {
        let (_dir, mut store) = tmp_store();
        let project = Project::new("Update Me", "Dev");
        store.insert(project).unwrap();

        let mut output = Vec::new();
        let cmd = ProjectsCommand::Update {
            name: "Update Me".to_string(),
            role: Some("Lead".to_string()),
            status: Some("Done".to_string()),
            completion: Some(1.0),
            jira: Some("JIRA-999".to_string()),
            repos: None,
        };

        handle_project_command(&mut store, cmd, &mut output).unwrap();

        let out_str = String::from_utf8(output).unwrap();
        assert!(out_str.contains("Project \"Update Me\" updated."));

        let projects = store.all();
        assert_eq!(projects.len(), 1);
        let p = projects[0];
        assert_eq!(p.role, "Lead");
        assert_eq!(p.status, "Done");
        assert_eq!(p.completion, 1.0);
        assert_eq!(p.jira_projects, vec!["JIRA-999"]);
        assert!(p.git_repos.is_empty());
    }

    #[test]
    fn test_handle_update_non_existent_project() {
        let (_dir, mut store) = tmp_store();
        let mut output = Vec::new();

        let cmd = ProjectsCommand::Update {
            name: "Ghost Project".to_string(),
            role: None,
            status: None,
            completion: None,
            jira: None,
            repos: None,
        };

        handle_project_command(&mut store, cmd, &mut output).unwrap();

        let out_str = String::from_utf8(output).unwrap();
        assert!(out_str.contains("Project \"Ghost Project\" not found."));
    }

    #[test]
    fn test_handle_list_projects() {
        let (_dir, mut store) = tmp_store();
        let mut output = Vec::new();

        handle_project_command(&mut store, ProjectsCommand::List, &mut output).unwrap();
        assert!(String::from_utf8(output.clone()).unwrap().contains("No tracked projects"));

        output.clear();
        let project = Project::new("List Me", "Viewer")
            .with_status("Planning")
            .with_completion(0.1);
        store.insert(project).unwrap();

        handle_project_command(&mut store, ProjectsCommand::List, &mut output).unwrap();
        let out_str = String::from_utf8(output).unwrap();
        assert!(out_str.contains("1 tracked project(s)"));
        assert!(out_str.contains("* List Me (Role: Viewer) — Planning (10%)"));
    }

    #[test]
    fn test_handle_remove_project() {
        let (_dir, mut store) = tmp_store();
        let project = Project::new("Remove Me", "Dev");
        store.insert(project).unwrap();

        let mut output = Vec::new();
        let cmd = ProjectsCommand::Remove {
            name: "Remove Me".to_string(),
        };

        handle_project_command(&mut store, cmd, &mut output).unwrap();
        let out_str = String::from_utf8(output).unwrap();
        assert!(out_str.contains("Project \"Remove Me\" removed."));
        assert!(store.all().is_empty());
    }
}
