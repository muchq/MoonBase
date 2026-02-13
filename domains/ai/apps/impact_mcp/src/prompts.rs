use crate::projects::Project;

pub fn generate_pull_prompt(projects: &[&Project]) -> String {
    let mut prompt = String::new();

    // Inject dynamic project context first
    if !projects.is_empty() {
        prompt.push_str("Context: My current projects are:\n");
        for p in projects {
            prompt.push_str(&format!(
                "* {} (Role: {}, Status: {}, Completion: {:.0}%)\n",
                p.name,
                p.role,
                p.status,
                p.completion * 100.0
            ));
            if !p.jira_projects.is_empty() {
                prompt.push_str(&format!(
                    "  - Jira Projects: {}\n",
                    p.jira_projects.join(", ")
                ));
            }
            if !p.git_repos.is_empty() {
                prompt.push_str(&format!("  - Git Repos: {}\n", p.git_repos.join(", ")));
            }
        }
        prompt.push_str("\n---\n\n");
    }

    // Load instructions from the skill file to avoid drift
    let skill_content = include_str!("../commands/impact-pull.md");

    // Strip frontmatter (content between first two `---` blocks)
    let parts: Vec<&str> = skill_content.splitn(3, "---").collect();
    let instructions = if parts.len() == 3 {
        parts[2].trim()
    } else {
        skill_content
    };

    prompt.push_str(instructions);

    prompt
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_generate_prompt_empty() {
        let projects: Vec<&Project> = vec![];
        let prompt = generate_pull_prompt(&projects);

        // Should contain content from impact-pull.md
        assert!(prompt.contains("Pull & Analyze Evidence"));
        assert!(prompt.contains("CRITICAL: Pay close attention to the output"));
        // Should NOT contain frontmatter
        assert!(!prompt.contains("name: impact-pull"));
    }

    #[test]
    fn test_generate_prompt_with_projects() {
        let p1 = Project::new("Project Alpha", "Lead")
            .with_jira_projects(vec!["ALPHA".to_string()])
            .with_git_repos(vec!["repo/alpha".to_string()]);

        let p2 = Project::new("Project Beta", "Contributor");

        let projects = vec![&p1, &p2];
        let prompt = generate_pull_prompt(&projects);

        assert!(prompt.contains("My current projects are:"));
        assert!(prompt.contains("* Project Alpha (Role: Lead, Status: Active, Completion: 0%)"));
        assert!(prompt.contains("Pull & Analyze Evidence"));
    }
}
