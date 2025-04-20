#[derive(Debug, Clone)]
pub struct Node {
    pub value: String,
    pub parent: Option<Box<Node>>,
}

#[derive(Debug, Clone)]
pub struct Graph {
    pub nodes: Vec<String>,
    pub edges: Vec<Vec<usize>>,
}

