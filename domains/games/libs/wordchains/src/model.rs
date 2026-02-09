use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Node {
    pub value: String,
    pub parent: Option<Box<Node>>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Graph {
    pub nodes: Vec<String>,
    pub edges: Vec<Vec<usize>>,
}
