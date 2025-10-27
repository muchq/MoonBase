use build_pal_core::GitContext;
use anyhow::{Context, Result};
use git2::{Repository, StatusOptions};
use std::path::Path;
use tracing::{debug, warn};

/// Git context capture functionality
pub struct GitCapture;

impl GitCapture {
    /// Capture git context from the given directory
    /// Returns Ok(GitContext) if git repository is found, Err if not a git repository
    pub fn capture_context<P: AsRef<Path>>(path: P) -> Result<GitContext> {
        let path_ref = path.as_ref();
        debug!("Attempting to capture git context from: {:?}", path_ref);
        
        let repo = Repository::discover(path_ref)
            .with_context(|| format!("Failed to find git repository in or above: {:?}", path_ref))?;
        
        let mut context = GitContext::default();
        
        // Get current branch and commit information
        match repo.head() {
            Ok(head) => {
                // Get branch name
                if let Some(branch_name) = head.shorthand() {
                    context.branch = Some(branch_name.to_string());
                    debug!("Found branch: {}", branch_name);
                } else if head.is_branch() {
                    // Handle case where shorthand() returns None but it's still a branch
                    if let Some(branch_name) = head.name() {
                        context.branch = Some(branch_name.replace("refs/heads/", ""));
                    }
                }
                
                // Get commit information
                match head.peel_to_commit() {
                    Ok(commit) => {
                        let commit_id = commit.id();
                        context.commit_hash = Some(commit_id.to_string());
                        debug!("Found commit hash: {}", commit_id);
                        
                        // Get commit message (first line only for brevity)
                        if let Some(message) = commit.message() {
                            let first_line = message.lines().next().unwrap_or(message);
                            context.commit_message = Some(first_line.to_string());
                        }
                        
                        // Get author information
                        let signature = commit.author();
                        if let Some(name) = signature.name() {
                            context.author = Some(name.to_string());
                            debug!("Found author: {}", name);
                        }
                    }
                    Err(e) => {
                        warn!("Failed to get commit information: {}", e);
                    }
                }
            }
            Err(e) => {
                warn!("Failed to get HEAD reference: {}", e);
                // This might be an empty repository or detached HEAD
                // Try to handle gracefully
            }
        }
        
        // Check for uncommitted changes
        match Self::has_uncommitted_changes(&repo) {
            Ok(has_changes) => {
                context.has_uncommitted_changes = has_changes;
                debug!("Uncommitted changes detected: {}", has_changes);
                
                // Capture diff if there are uncommitted changes
                if has_changes {
                    match Self::capture_diff(&repo) {
                        Ok(diff) => context.diff = diff,
                        Err(e) => warn!("Failed to capture diff: {}", e),
                    }
                }
            }
            Err(e) => {
                warn!("Failed to check for uncommitted changes: {}", e);
                // Default to false if we can't determine
                context.has_uncommitted_changes = false;
            }
        }
        
        debug!("Successfully captured git context: branch={:?}, commit={:?}, has_changes={}", 
               context.branch, context.commit_hash, context.has_uncommitted_changes);
        
        Ok(context)
    }
    
    /// Check if the repository has uncommitted changes
    fn has_uncommitted_changes(repo: &Repository) -> Result<bool> {
        let mut opts = StatusOptions::new();
        opts.include_untracked(true)
            .include_ignored(false);
            
        let statuses = repo.statuses(Some(&mut opts))
            .with_context(|| "Failed to get repository status")?;
        
        // Check if there are any modified, added, deleted, or untracked files
        for status in statuses.iter() {
            let flags = status.status();
            if flags.is_wt_modified() 
                || flags.is_wt_deleted() 
                || flags.is_wt_new() 
                || flags.is_index_modified() 
                || flags.is_index_deleted() 
                || flags.is_index_new() {
                return Ok(true);
            }
        }
        
        Ok(false)
    }
    
    /// Capture the diff of uncommitted changes
    fn capture_diff(repo: &Repository) -> Result<Option<String>> {
        let mut diff_output = Vec::new();
        
        // Get diff between working directory and HEAD
        match repo.head() {
            Ok(head) => {
                match head.peel_to_tree() {
                    Ok(tree) => {
                        let diff = repo.diff_tree_to_workdir_with_index(Some(&tree), None)
                            .with_context(|| "Failed to create diff")?;
                        
                        diff.print(git2::DiffFormat::Patch, |_delta, _hunk, line| {
                            diff_output.extend_from_slice(line.content());
                            true
                        }).with_context(|| "Failed to print diff")?;
                    }
                    Err(e) => {
                        warn!("Failed to get tree from HEAD: {}", e);
                        // Try to get diff of staged changes only
                        let diff = repo.diff_index_to_workdir(None, None)
                            .with_context(|| "Failed to create index-to-workdir diff")?;
                        
                        diff.print(git2::DiffFormat::Patch, |_delta, _hunk, line| {
                            diff_output.extend_from_slice(line.content());
                            true
                        }).with_context(|| "Failed to print index diff")?;
                    }
                }
            }
            Err(_) => {
                // No HEAD (empty repository), try to show all files as new
                let diff = repo.diff_index_to_workdir(None, None)
                    .with_context(|| "Failed to create diff for empty repository")?;
                
                diff.print(git2::DiffFormat::Patch, |_delta, _hunk, line| {
                    diff_output.extend_from_slice(line.content());
                    true
                }).with_context(|| "Failed to print diff for empty repository")?;
            }
        }
        
        if diff_output.is_empty() {
            Ok(None)
        } else {
            let diff_string = String::from_utf8_lossy(&diff_output).to_string();
            // Limit diff size to prevent excessive memory usage
            if diff_string.len() > 100_000 {
                let truncated = format!("{}... [diff truncated, {} bytes total]", 
                                      &diff_string[..100_000], diff_string.len());
                Ok(Some(truncated))
            } else {
                Ok(Some(diff_string))
            }
        }
    }
    
    /// Check if a directory is within a git repository
    pub fn is_git_repository<P: AsRef<Path>>(path: P) -> bool {
        Repository::discover(path.as_ref()).is_ok()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use tempfile::tempdir;
    use git2::{Signature, Time};

    #[test]
    fn test_capture_context_no_git() {
        // Test with a directory that doesn't have git
        let temp_dir = tempdir().unwrap();
        let result = GitCapture::capture_context(temp_dir.path());
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("Failed to find git repository"));
    }

    #[test]
    fn test_git_context_default() {
        let context = GitContext::default();
        assert!(context.branch.is_none());
        assert!(context.commit_hash.is_none());
        assert!(context.commit_message.is_none());
        assert!(context.author.is_none());
        assert!(!context.has_uncommitted_changes);
        assert!(context.diff.is_none());
    }

    #[test]
    fn test_is_git_repository() {
        // Test with non-git directory
        let temp_dir = tempdir().unwrap();
        assert!(!GitCapture::is_git_repository(temp_dir.path()));
        
        // Test with git repository
        let repo = Repository::init(temp_dir.path()).unwrap();
        assert!(GitCapture::is_git_repository(temp_dir.path()));
        
        // Test with subdirectory of git repository
        let sub_dir = temp_dir.path().join("subdir");
        fs::create_dir(&sub_dir).unwrap();
        assert!(GitCapture::is_git_repository(&sub_dir));
        
        drop(repo);
    }

    #[test]
    fn test_capture_context_empty_repository() {
        let temp_dir = tempdir().unwrap();
        let _repo = Repository::init(temp_dir.path()).unwrap();
        
        // Empty repository should still work but have no commit info
        let result = GitCapture::capture_context(temp_dir.path());
        assert!(result.is_ok());
        
        let context = result.unwrap();
        assert!(context.branch.is_none() || context.branch == Some("master".to_string()) || context.branch == Some("main".to_string()));
        assert!(context.commit_hash.is_none());
        assert!(context.commit_message.is_none());
        assert!(context.author.is_none());
        // Empty repo might have uncommitted changes if there are untracked files
    }

    #[test]
    fn test_capture_context_with_commit() {
        let temp_dir = tempdir().unwrap();
        let repo = Repository::init(temp_dir.path()).unwrap();
        
        // Create a file and commit it
        let file_path = temp_dir.path().join("test.txt");
        fs::write(&file_path, "Hello, world!").unwrap();
        
        // Add file to index
        let mut index = repo.index().unwrap();
        index.add_path(std::path::Path::new("test.txt")).unwrap();
        index.write().unwrap();
        
        // Create commit
        let signature = Signature::new("Test User", "test@example.com", &Time::new(1234567890, 0)).unwrap();
        let tree_id = index.write_tree().unwrap();
        let tree = repo.find_tree(tree_id).unwrap();
        
        repo.commit(
            Some("HEAD"),
            &signature,
            &signature,
            "Initial commit",
            &tree,
            &[],
        ).unwrap();
        
        // Capture context
        let result = GitCapture::capture_context(temp_dir.path());
        assert!(result.is_ok());
        
        let context = result.unwrap();
        assert!(context.branch.is_some());
        let branch = context.branch.unwrap();
        assert!(branch == "master" || branch == "main");
        
        assert!(context.commit_hash.is_some());
        assert_eq!(context.commit_hash.as_ref().unwrap().len(), 40); // SHA-1 hash length
        
        assert!(context.commit_message.is_some());
        assert_eq!(context.commit_message.unwrap(), "Initial commit");
        
        assert!(context.author.is_some());
        assert_eq!(context.author.unwrap(), "Test User");
        
        assert!(!context.has_uncommitted_changes);
        assert!(context.diff.is_none());
    }

    #[test]
    fn test_capture_context_with_uncommitted_changes() {
        let temp_dir = tempdir().unwrap();
        let repo = Repository::init(temp_dir.path()).unwrap();
        
        // Create initial commit
        let file_path = temp_dir.path().join("test.txt");
        fs::write(&file_path, "Hello, world!").unwrap();
        
        let mut index = repo.index().unwrap();
        index.add_path(std::path::Path::new("test.txt")).unwrap();
        index.write().unwrap();
        
        let signature = Signature::new("Test User", "test@example.com", &Time::new(1234567890, 0)).unwrap();
        let tree_id = index.write_tree().unwrap();
        let tree = repo.find_tree(tree_id).unwrap();
        
        repo.commit(
            Some("HEAD"),
            &signature,
            &signature,
            "Initial commit",
            &tree,
            &[],
        ).unwrap();
        
        // Modify the file to create uncommitted changes
        fs::write(&file_path, "Hello, modified world!").unwrap();
        
        // Capture context
        let result = GitCapture::capture_context(temp_dir.path());
        assert!(result.is_ok());
        
        let context = result.unwrap();
        assert!(context.has_uncommitted_changes);
        assert!(context.diff.is_some());
        
        let diff = context.diff.unwrap();
        // The diff should contain both the old and new content
        assert!(diff.contains("Hello, world!"));
        assert!(diff.contains("Hello, modified world!"));
        // Check for diff markers (might be different formats)
        assert!(diff.contains("-") || diff.contains("@@"));
        assert!(diff.contains("+") || diff.contains("@@"));
    }

    #[test]
    fn test_capture_context_with_untracked_files() {
        let temp_dir = tempdir().unwrap();
        let repo = Repository::init(temp_dir.path()).unwrap();
        
        // Create initial commit
        let file_path = temp_dir.path().join("test.txt");
        fs::write(&file_path, "Hello, world!").unwrap();
        
        let mut index = repo.index().unwrap();
        index.add_path(std::path::Path::new("test.txt")).unwrap();
        index.write().unwrap();
        
        let signature = Signature::new("Test User", "test@example.com", &Time::new(1234567890, 0)).unwrap();
        let tree_id = index.write_tree().unwrap();
        let tree = repo.find_tree(tree_id).unwrap();
        
        repo.commit(
            Some("HEAD"),
            &signature,
            &signature,
            "Initial commit",
            &tree,
            &[],
        ).unwrap();
        
        // Add an untracked file
        let untracked_path = temp_dir.path().join("untracked.txt");
        fs::write(&untracked_path, "This is untracked").unwrap();
        
        // Capture context
        let result = GitCapture::capture_context(temp_dir.path());
        assert!(result.is_ok());
        
        let context = result.unwrap();
        assert!(context.has_uncommitted_changes);
        // Diff might or might not include untracked files depending on git2 behavior
    }

    #[test]
    fn test_capture_context_graceful_error_handling() {
        // Test that the function handles various error conditions gracefully
        
        // Test with a path that doesn't exist
        let result = GitCapture::capture_context("/path/that/does/not/exist");
        assert!(result.is_err());
        
        // Test with a path that exists but is not a git repository
        let temp_dir = tempdir().unwrap();
        let result = GitCapture::capture_context(temp_dir.path());
        assert!(result.is_err());
        
        // Test with a corrupted git repository (simulate by creating .git directory but not proper repo)
        let corrupt_dir = tempdir().unwrap();
        let git_dir = corrupt_dir.path().join(".git");
        fs::create_dir(&git_dir).unwrap();
        
        let result = GitCapture::capture_context(corrupt_dir.path());
        assert!(result.is_err());
    }

    #[test]
    fn test_capture_context_detached_head() {
        let temp_dir = tempdir().unwrap();
        let repo = Repository::init(temp_dir.path()).unwrap();
        
        // Create initial commit
        let file_path = temp_dir.path().join("test.txt");
        fs::write(&file_path, "Hello, world!").unwrap();
        
        let mut index = repo.index().unwrap();
        index.add_path(std::path::Path::new("test.txt")).unwrap();
        index.write().unwrap();
        
        let signature = Signature::new("Test User", "test@example.com", &Time::new(1234567890, 0)).unwrap();
        let tree_id = index.write_tree().unwrap();
        let tree = repo.find_tree(tree_id).unwrap();
        
        let commit_id = repo.commit(
            Some("HEAD"),
            &signature,
            &signature,
            "Initial commit",
            &tree,
            &[],
        ).unwrap();
        
        // Create second commit
        fs::write(&file_path, "Hello, second commit!").unwrap();
        index.add_path(std::path::Path::new("test.txt")).unwrap();
        index.write().unwrap();
        let tree_id2 = index.write_tree().unwrap();
        let tree2 = repo.find_tree(tree_id2).unwrap();
        let parent_commit = repo.find_commit(commit_id).unwrap();
        
        let _commit_id2 = repo.commit(
            Some("HEAD"),
            &signature,
            &signature,
            "Second commit",
            &tree2,
            &[&parent_commit],
        ).unwrap();
        
        // Checkout first commit to create detached HEAD
        let commit1 = repo.find_commit(commit_id).unwrap();
        repo.set_head_detached(commit1.id()).unwrap();
        
        // Capture context in detached HEAD state
        let result = GitCapture::capture_context(temp_dir.path());
        assert!(result.is_ok());
        
        let context = result.unwrap();
        // In detached HEAD, branch might be None or contain the commit hash
        assert!(context.commit_hash.is_some());
        assert_eq!(context.commit_hash.unwrap(), commit_id.to_string());
        assert_eq!(context.commit_message.unwrap(), "Initial commit");
        assert_eq!(context.author.unwrap(), "Test User");
    }

    #[test]
    fn test_has_uncommitted_changes_various_states() {
        let temp_dir = tempdir().unwrap();
        let repo = Repository::init(temp_dir.path()).unwrap();
        
        // Empty repository - no uncommitted changes
        let result = GitCapture::has_uncommitted_changes(&repo);
        assert!(result.is_ok());
        // Empty repo might have changes if there are untracked files, so we don't assert the value
        
        // Add a file but don't commit - should have changes
        let file_path = temp_dir.path().join("test.txt");
        fs::write(&file_path, "Hello, world!").unwrap();
        
        let result = GitCapture::has_uncommitted_changes(&repo);
        assert!(result.is_ok());
        assert!(result.unwrap()); // Should have uncommitted changes
    }

    #[test]
    fn test_diff_truncation() {
        let temp_dir = tempdir().unwrap();
        let repo = Repository::init(temp_dir.path()).unwrap();
        
        // Create initial commit
        let file_path = temp_dir.path().join("large_file.txt");
        fs::write(&file_path, "Initial content").unwrap();
        
        let mut index = repo.index().unwrap();
        index.add_path(std::path::Path::new("large_file.txt")).unwrap();
        index.write().unwrap();
        
        let signature = Signature::new("Test User", "test@example.com", &Time::new(1234567890, 0)).unwrap();
        let tree_id = index.write_tree().unwrap();
        let tree = repo.find_tree(tree_id).unwrap();
        
        repo.commit(
            Some("HEAD"),
            &signature,
            &signature,
            "Initial commit",
            &tree,
            &[],
        ).unwrap();
        
        // Create a very large change to test truncation
        let large_content = "x".repeat(150_000);
        fs::write(&file_path, &large_content).unwrap();
        
        let result = GitCapture::capture_diff(&repo);
        assert!(result.is_ok());
        
        let diff = result.unwrap();
        assert!(diff.is_some());
        
        let diff_content = diff.unwrap();
        // Should be truncated
        assert!(diff_content.len() <= 100_100); // 100k + some buffer for truncation message
        assert!(diff_content.contains("diff truncated"));
    }
}