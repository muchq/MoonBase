use crate::model::{Graph, Node};
use std::collections::{HashMap, VecDeque};
use tracing::{Level, event};

pub fn build_graph(words: Vec<String>) -> (Graph, Vec<usize>) {
    let num_words = words.len();
    let mut word_graph: Vec<Vec<usize>> = vec![vec![]; num_words];
    let mut matches: Vec<usize> = Vec::new();

    event!(Level::INFO, "building graph...");
    for (i, word1) in words.iter().enumerate() {
        for j in (i + 1)..num_words {
            let word2 = words[j].clone();
            if words_are_one_away(word1, &word2) {
                word_graph[i].push(j);
                word_graph[j].push(i);
                matches.push(i);
                matches.push(j);
            }
        }
    }

    (
        Graph {
            nodes: words,
            edges: word_graph,
        },
        matches,
    )
}

pub fn bfs_for_target(start: String, target_word: &str, word_graph: &Graph) -> Option<Vec<String>> {
    if start.eq(target_word) {
        return Some(vec![start]);
    }

    if start.len() != target_word.len() {
        return None;
    }

    let mut word_to_index: HashMap<String, usize> = HashMap::new();
    for (i, word) in word_graph.nodes.iter().enumerate() {
        word_to_index.insert(word.to_owned(), i);
    }

    if !word_to_index.contains_key(&start) {
        event!(Level::DEBUG, "{} is not in dictionary.", &start);
        return None;
    }

    if !word_to_index.contains_key(target_word) {
        event!(Level::DEBUG, "{} is not in dictionary.", &target_word);
        return None;
    }

    let mut seen: HashMap<String, Node> = HashMap::new();
    let mut queue: VecDeque<String> = VecDeque::new();
    seen.insert(
        start.clone(),
        Node {
            value: start.clone(),
            parent: None,
        },
    );
    queue.push_back(start);

    while !queue.is_empty() {
        let current = queue.pop_front().unwrap();
        if current.eq(target_word) {
            return Some(to_path(&current, &seen));
        }

        let i = word_to_index.get(&current).unwrap();
        for j in &word_graph.edges[*i] {
            if !seen.contains_key(&word_graph.nodes[*j]) {
                let neighbor_node = Node {
                    value: word_graph.nodes[*j].clone(),
                    parent: Some(current.clone()),
                };
                seen.insert(word_graph.nodes[*j].clone(), neighbor_node);
                queue.push_back(word_graph.nodes[*j].clone());
            }
        }
    }
    None
}

fn to_path(target_val: &str, seen: &HashMap<String, Node>) -> Vec<String> {
    let mut path: Vec<String> = vec![target_val.to_string()];
    let mut current_val = target_val.to_string();

    while let Some(node) = seen.get(&current_val) {
        if let Some(parent_val) = &node.parent {
            path.push(parent_val.clone());
            current_val = parent_val.clone();
        } else {
            break;
        }
    }
    path.reverse();
    path
}

fn words_are_one_away(first: &str, second: &str) -> bool {
    if first.len() != second.len() {
        return false;
    }
    let mut diff = 0;
    for (a, b) in first.chars().zip(second.chars()) {
        if a != b {
            diff += 1;
        }
    }
    diff == 1
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_words_are_one_away() {
        assert!(words_are_one_away("star", "stat"));
        assert!(!words_are_one_away("star", "stub"));
        assert!(!words_are_one_away("foo", "foop"));
    }

    #[test]
    fn test_bfs_for_target() {
        let words = vec![
            "cat".to_string(),
            "cot".to_string(),
            "cog".to_string(),
            "dog".to_string(),
        ];
        let (graph, _) = build_graph(words);
        let path = bfs_for_target("cat".to_string(), "dog", &graph);
        assert_eq!(
            path,
            Some(vec![
                "cat".to_string(),
                "cot".to_string(),
                "cog".to_string(),
                "dog".to_string()
            ])
        );
    }
}
