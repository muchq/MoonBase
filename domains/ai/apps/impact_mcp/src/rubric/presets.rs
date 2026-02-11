use super::{Rubric, RubricDimension};

/// Dropbox Engineering Career Framework
/// https://dropbox.github.io/dbx-career-framework/
pub fn dropbox_rubric() -> Rubric {
    Rubric {
        name: "Dropbox Impact Rubric".into(),
        version: 1,
        dimensions: vec![
            RubricDimension {
                key: "results".into(),
                label: "Results".into(),
                description: "Delivering business impact by executing on goals and shipping high-quality work."
                    .into(),
                weight: 1.0,
            },
            RubricDimension {
                key: "direction".into(),
                label: "Direction".into(),
                description: "Defining technical strategy, setting goals, and aligning teams."
                    .into(),
                weight: 1.0,
            },
            RubricDimension {
                key: "talent".into(),
                label: "Talent".into(),
                description: "Hiring, mentoring, and growing the team."
                    .into(),
                weight: 1.0,
            },
            RubricDimension {
                key: "culture".into(),
                label: "Culture".into(),
                description: "Building an inclusive, collaborative, and high-performing engineering culture."
                    .into(),
                weight: 1.0,
            },
        ],
    }
}

/// Spotify Technology Career Steps
/// https://engineering.atspotify.com/2016/2/spotify-technology-career-steps
pub fn spotify_rubric() -> Rubric {
    Rubric {
        name: "Spotify Impact Rubric".into(),
        version: 1,
        dimensions: vec![
            RubricDimension {
                key: "team".into(),
                label: "Team Success".into(),
                description: "Values team success over individual success. Contributing to the success of the squad, chapter, guild, and tribe."
                    .into(),
                weight: 1.0,
            },
            RubricDimension {
                key: "improvement".into(),
                label: "Improvement".into(),
                description: "Continuously improves themselves and team. Focused on learning and growing."
                    .into(),
                weight: 1.0,
            },
            RubricDimension {
                key: "accountability".into(),
                label: "Accountability".into(),
                description: "Holds themselves and others accountable. Taking responsibility for actions and fostering trust."
                    .into(),
                weight: 1.0,
            },
            RubricDimension {
                key: "impact".into(),
                label: "Business Impact".into(),
                description: "Thinks about the business impact of their work. Aligning technical choices with business goals."
                    .into(),
                weight: 1.0,
            },
            RubricDimension {
                key: "mastery".into(),
                label: "Mastery".into(),
                description: "Demonstrates mastery of their discipline. Becoming a better engineer, coach, or product owner."
                    .into(),
                weight: 1.0,
            },
        ],
    }
}

/// Rent The Runway Engineering Ladder
/// Based on the four pillars often cited for RTR.
pub fn rent_the_runway_rubric() -> Rubric {
    Rubric {
        name: "Rent The Runway Impact Rubric".into(),
        version: 1,
        dimensions: vec![
            RubricDimension {
                key: "technical_skill".into(),
                label: "Technical Skill".into(),
                description: "Technical competence, code quality, architecture, and system design."
                    .into(),
                weight: 1.0,
            },
            RubricDimension {
                key: "get_stuff_done".into(),
                label: "Get Stuff Done".into(),
                description: "Productivity, delivery, and ability to ship features and projects."
                    .into(),
                weight: 1.0,
            },
            RubricDimension {
                key: "impact".into(),
                label: "Impact".into(),
                description: "Business value, moving metrics, and solving problems that matter."
                    .into(),
                weight: 1.0,
            },
            RubricDimension {
                key: "communication_leadership".into(),
                label: "Comm & Leadership".into(), // Shortened label for better display
                description: "Communication, collaboration, mentorship, and leading others."
                    .into(),
                weight: 1.0,
            },
        ],
    }
}

/// Etsy Engineering Career Ladder
/// https://etsy.github.io/Etsy-Engineering-Career-Ladder/
pub fn etsy_rubric() -> Rubric {
    Rubric {
        name: "Etsy Impact Rubric".into(),
        version: 1,
        dimensions: vec![
            RubricDimension {
                key: "delivery".into(),
                label: "Delivery".into(),
                description: "Scoping, prioritization, shipping to production, and initiative."
                    .into(),
                weight: 1.0,
            },
            RubricDimension {
                key: "domain_expertise".into(),
                label: "Domain Expertise".into(),
                description: "Knowledge of discipline, language, tools, and business/product sense."
                    .into(),
                weight: 1.0,
            },
            RubricDimension {
                key: "problem_solving".into(),
                label: "Problem Solving".into(),
                description: "Architecture, design patterns, critical thinking, and decision making."
                    .into(),
                weight: 1.0,
            },
            RubricDimension {
                key: "communication".into(),
                label: "Communication".into(),
                description: "Documentation, collaboration, relationship-building, and listening."
                    .into(),
                weight: 1.0,
            },
            RubricDimension {
                key: "leadership".into(),
                label: "Leadership".into(),
                description: "Accountability, responsibility, mentorship, and setting an example."
                    .into(),
                weight: 1.0,
            },
        ],
    }
}
