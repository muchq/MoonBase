mod commands;

pub use commands::CommandHandler;

use crate::archetype::ArchetypeProfile;
use crate::evidence::EvidenceStore;
use crate::llm::{self, LlmClient};
use crate::readiness::ReadinessMap;
use crate::rubric::Rubric;

/// The StaffTrack agent — an interactive, chat-based assistant that
/// reasons over the local evidence store and rubric to help the user
/// understand and improve their promotion readiness.
///
/// Known commands are handled deterministically (fast path). Freeform
/// questions fall through to the LLM, which is grounded in a system
/// prompt built from the evidence store and rubric.
///
/// The agent is **read-only by default**: it never performs external
/// actions without explicit user approval.
pub struct Agent {
    pub store: EvidenceStore,
    pub rubric: Rubric,
    pub archetype_profile: ArchetypeProfile,
    handler: CommandHandler,
    llm: Option<LlmClient>,
    history: std::sync::Mutex<Vec<(String, String)>>,
}

impl Agent {
    /// Create an agent without LLM support (deterministic commands only).
    pub fn new(store: EvidenceStore, rubric: Rubric) -> Self {
        Self {
            store,
            rubric,
            archetype_profile: ArchetypeProfile::default(),
            handler: CommandHandler,
            llm: None,
            history: std::sync::Mutex::new(Vec::new()),
        }
    }

    /// Create an agent with LLM support for freeform conversation.
    pub fn with_llm(store: EvidenceStore, rubric: Rubric) -> Self {
        let system_prompt = llm::build_system_prompt(&store, &rubric);
        Self {
            store,
            rubric,
            archetype_profile: ArchetypeProfile::default(),
            handler: CommandHandler,
            llm: Some(LlmClient::new(system_prompt)),
            history: std::sync::Mutex::new(Vec::new()),
        }
    }

    /// Process a user message and produce a response.
    ///
    /// Known commands are handled deterministically. Unrecognized input
    /// is forwarded to the LLM (if configured) with full conversation
    /// history for context.
    pub async fn handle(&self, input: &str) -> String {
        // Fast path: deterministic command
        if let Some(response) = self.handler.try_dispatch(input, self) {
            self.push_history(input, &response);
            return response;
        }

        // LLM path
        match &self.llm {
            Some(client) => {
                let history = self.history.lock().unwrap().clone();
                match client.chat(&history, input).await {
                    Ok(response) => {
                        self.push_history(input, &response);
                        response
                    }
                    Err(e) => format!("LLM error: {e}\n\nType \"help\" for built-in commands."),
                }
            }
            None => {
                "I didn't recognize that command. Type \"help\" for a list of \
                 supported commands.\n\n\
                 Tip: Set an API key (e.g. ANTHROPIC_API_KEY) and restart to \
                 enable freeform conversation."
                    .into()
            }
        }
    }

    /// Compute current readiness based on evidence and rubric.
    pub fn readiness(&self) -> ReadinessMap {
        ReadinessMap::compute(&self.store, &self.rubric)
    }

    /// Whether the LLM backend is available.
    pub fn has_llm(&self) -> bool {
        self.llm.is_some()
    }

    /// The model name in use (if LLM is configured).
    pub fn model_name(&self) -> Option<&str> {
        self.llm.as_ref().map(|c| c.model())
    }

    fn push_history(&self, user: &str, assistant: &str) {
        let mut history = self.history.lock().unwrap();
        history.push((user.to_string(), assistant.to_string()));
        // Keep last 20 turns to bound context size
        let len = history.len();
        if len > 20 {
            history.drain(..len - 20);
        }
    }
}
