use std::collections::{HashMap, VecDeque};
use log::info;

#[derive(Debug, Clone)]
pub struct Node {
    pub value: String,
    pub parent: Option<Box<Node>>,
}

pub fn build_graph(words: &Vec<String>) -> (Vec<Vec<usize>>, Vec<usize>) {
    info!("building graph...");
    let mut matches: Vec<usize> = Vec::new();
    let num_words = words.len();
    let mut word_graph: Vec<Vec<usize>> = vec![vec![]; num_words];
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
    (word_graph, matches)
}

pub fn bfs_for_target(
    start: String,
    target_word: &String,
    word_graph: &[Vec<usize>],
    word_to_index: &HashMap<String, usize>,
    words: &[String],
) -> Option<Vec<String>> {
    if start.eq(target_word) {
        return Some(vec![start]);
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
            let target_node = seen.get(&current).unwrap().clone();
            return Some(to_path(target_node));
        }

        let i = word_to_index.get(&current).unwrap();
        for j in &word_graph[*i] {
            if !seen.contains_key(&words[*j]) {
                let parent_node = Box::new(seen.get(&current)?.clone());
                let neighbor_node = Node {
                    value: words[*j].clone(),
                    parent: Some(parent_node),
                };
                seen.insert(words[*j].clone(), neighbor_node);
                queue.push_back(words[*j].clone());
            }
        }
    }
    None
}

fn to_path(end: Node) -> Vec<String> {
    let mut path: Vec<String> = vec![end.value.clone()];
    let mut node = end;
    while node.parent.is_some() {
        let parent = node.parent.unwrap();
        path.push(parent.value.clone());
        node = *parent;
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
        assert_eq!(words_are_one_away("star", "stat"), true);
        assert_eq!(words_are_one_away("star", "stub"), false);
        assert_eq!(words_are_one_away("foo", "foop"), false);
    }
}
