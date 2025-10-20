use build_pal_core::GitContext;
use anyhow::{Context, Result};
use git2::Repository;
use std::path::Path;

/// Git context capture functionality
pub struct GitCapture;

impl GitCapture {
    /// Capture git context from the given directory
    pub fn capture_context<P: AsRef<Path>>(path: P) -> Result<GitContext> {
        let repo = Repository::discover(path.as_ref())
            .with_context(|| "Failed to find git repository")?;
        
        let mut context = GitContext::default();
        
        // Get current branch
        if let Ok(head) = repo.head() {
            if let Some(branch_name) = head.shorthand() {
                context.branch = Some(branch_name.to_string());
            }
            
            // Get commit hash
            if let Ok(commit) = head.peel_to_commit() {
                context.commit_hash = Some(commit.id().to_string());
                context.commit_message = commit.message().map(|s| s.to_string());
                
                let signature = commit.author();
                if let Some(name) = signature.name() {
                    context.author = Some(name.to_string());
                }
            }
        }
        
        // Check for uncommitted changes
        context.has_uncommitted_changes = Self::has_uncommitted_changes(&repo)?;
        
        // Capture diff if there are uncommitted changes
        if context.has_uncommitted_changes {
            context.diff = Self::capture_diff(&repo)?;
        }
        
        Ok(context)
    }
    
    fn has_uncommitted_changes(repo: &Repository) -> Result<bool> {
        let statuses = repo.statuses(None)
            .with_context(|| "Failed to get repository status")?;
        
        Ok(!statuses.is_empty())
    }
    
    fn capture_diff(repo: &Repository) -> Result<Option<String>> {
        let mut diff_output = Vec::new();
        
        // Get diff between working directory and HEAD
        let tree = repo.head()?.peel_to_tree()?;
        let diff = repo.diff_tree_to_workdir_with_index(Some(&tree), None)?;
        
        diff.print(git2::DiffFormat::Patch, |_delta, _hunk, line| {
            diff_output.extend_from_slice(line.content());
            true
        })?;
        
        if diff_output.is_empty() {
            Ok(None)
        } else {
            Ok(Some(String::from_utf8_lossy(&diff_output).to_string()))
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    // use std::fs;
    // use tempfile::tempdir;

    #[test]
    fn test_capture_context_no_git() {
        // Test with a directory that doesn't have git
        let result = GitCapture::capture_context("/tmp");
        assert!(result.is_err());
    }

    #[test]
    fn test_git_context_default() {
        let context = GitContext::default();
        assert!(context.branch.is_none());
        assert!(context.commit_hash.is_none());
        assert!(!context.has_uncommitted_changes);
        assert!(context.diff.is_none());
    }
}