use crate::projects::Project;

pub fn generate_pull_prompt(projects: &[&Project]) -> String {
    let mut prompt = String::new();
    prompt.push_str("You are an AI assistant helping me track my impact. Please use your available MCP tools to pull relevant evidence for my projects.\n\n");

    if projects.is_empty() {
        prompt.push_str("I have no projects defined yet. Please ask me to define my key projects (name, role, status) using `impact-mcp projects add` so we can track evidence against them effectively.\n\n");
    } else {
        prompt.push_str("My current projects are:\n");
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
        prompt.push_str("\n");
    }

    prompt.push_str("For each project (or generally if no projects are defined), please:\n");
    prompt.push_str("1. Check for recent activity (last 7 days) in the associated Git repositories (PRs, commits, reviews) using `gh`.\n");
    prompt.push_str("2. Check for recent updates in Jira tickets or Confluence pages using `atlassian`.\n");
    prompt.push_str("   - CRITICAL: Analyze the JIRA tickets for quality.\n");
    prompt.push_str("   - If a ticket has no description, prompt me to add one to clarify the scope.\n");
    prompt.push_str("   - If a ticket is 'In Progress' but hasn't been updated in over 14 days, flag it as stale and ask if I should update or close it.\n");
    prompt.push_str("   - If a ticket has no comments, suggest adding a status update.\n");
    prompt.push_str("3. Summarize the findings and ask me if I want to save any of them as evidence cards using `impact-mcp evidence add`.\n");

    prompt
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_generate_prompt_empty() {
        let projects: Vec<&Project> = vec![];
        let prompt = generate_pull_prompt(&projects);
        assert!(prompt.contains("You are an AI assistant"));
        assert!(prompt.contains("I have no projects defined yet"));
        assert!(prompt.contains("CRITICAL: Analyze the JIRA tickets for quality"));
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
        assert!(prompt.contains("  - Jira Projects: ALPHA"));
        assert!(prompt.contains("  - Git Repos: repo/alpha"));
        assert!(prompt.contains("* Project Beta (Role: Contributor, Status: Active, Completion: 0%)"));
        assert!(prompt.contains("CRITICAL: Analyze the JIRA tickets for quality"));
    }
}
