use crate::model::{Graph, Node};
use std::collections::{HashMap, VecDeque};
use tracing::{Level, event};

pub fn build_graph(words: Vec<String>) -> (Graph, Vec<usize>) {
    let num_words = words.len();
    let mut word_graph: Vec<Vec<usize>> = vec![vec![]; num_words];
    let mut matches: Vec<usize> = Vec::new();

    event!(Level::INFO, "building graph...");

    // Group words by length to avoid processing mismatches
    let mut words_by_len: HashMap<usize, Vec<usize>> = HashMap::new();
    for (i, word) in words.iter().enumerate() {
        words_by_len.entry(word.len()).or_default().push(i);
    }

    for (_len, group) in words_by_len {
        let mut pattern_map: HashMap<Vec<u8>, Vec<usize>> = HashMap::new();

        for &i in &group {
            let word_bytes = words[i].as_bytes();
            for k in 0..word_bytes.len() {
                let mut pattern = word_bytes.to_vec();
                pattern[k] = b'*'; // Use '*' as wildcard
                pattern_map.entry(pattern).or_default().push(i);
            }
        }

        for list in pattern_map.values() {
             for i in 0..list.len() {
                 for j in (i + 1)..list.len() {
                     let u = list[i];
                     let v = list[j];
                     // Since we process each pattern, and two words differ by exactly one char
                     // if and only if they share exactly one pattern, we don't need to check duplicates.
                     word_graph[u].push(v);
                     word_graph[v].push(u);
                     matches.push(u);
                     matches.push(v);
                 }
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
            let target_node = seen.get(&current).unwrap().clone();
            return Some(to_path(target_node));
        }

        let i = word_to_index.get(&current).unwrap();
        for j in &word_graph.edges[*i] {
            if !seen.contains_key(&word_graph.nodes[*j]) {
                let parent_node = Box::new(seen.get(&current)?.clone());
                let neighbor_node = Node {
                    value: word_graph.nodes[*j].clone(),
                    parent: Some(parent_node),
                };
                seen.insert(word_graph.nodes[*j].clone(), neighbor_node);
                queue.push_back(word_graph.nodes[*j].clone());
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

pub fn find_all_shortest_paths(
    start: String,
    target_word: &str,
    word_graph: &Graph,
) -> Vec<Vec<String>> {
    if start == target_word {
        return vec![vec![start]];
    }

    let mut word_to_index: HashMap<String, usize> = HashMap::new();
    for (i, word) in word_graph.nodes.iter().enumerate() {
        word_to_index.insert(word.clone(), i);
    }

    if !word_to_index.contains_key(&start) || !word_to_index.contains_key(target_word) {
        return vec![];
    }

    let mut queue: VecDeque<String> = VecDeque::new();
    queue.push_back(start.clone());

    let mut dist: HashMap<String, usize> = HashMap::new();
    dist.insert(start.clone(), 0);

    let mut parents: HashMap<String, Vec<String>> = HashMap::new();

    let mut found_min_dist = usize::MAX;

    while !queue.is_empty() {
        let current = queue.pop_front().unwrap();
        let d = *dist.get(&current).unwrap();

        if d >= found_min_dist {
            continue;
        }

        let u_idx = *word_to_index.get(&current).unwrap();
        for v_idx in &word_graph.edges[u_idx] {
            let neighbor = &word_graph.nodes[*v_idx];
            if *neighbor == target_word {
                println!("Found target via {} at dist {}", current, d + 1);
                found_min_dist = d + 1;
                parents.entry(neighbor.clone()).or_default().push(current.clone());
            } else {
                if !dist.contains_key(neighbor) {
                    dist.insert(neighbor.clone(), d + 1);
                    parents.entry(neighbor.clone()).or_default().push(current.clone());
                    queue.push_back(neighbor.clone());
                } else if *dist.get(neighbor).unwrap() == d + 1 {
                    parents.entry(neighbor.clone()).or_default().push(current.clone());
                }
            }
        }
    }

    if found_min_dist == usize::MAX {
        return vec![];
    }

    let mut result = Vec::new();
    let mut path = vec![target_word.to_string()];
    backtrack(target_word, &start, &parents, &mut path, &mut result);
    result
}

fn backtrack(
    current: &str,
    start: &str,
    parents: &HashMap<String, Vec<String>>,
    path: &mut Vec<String>,
    result: &mut Vec<Vec<String>>,
) {
    if current == start {
        let mut p = path.clone();
        p.reverse();
        result.push(p);
        return;
    }

    if let Some(pars) = parents.get(current) {
        for p in pars {
            path.push(p.clone());
            backtrack(p, start, parents, path, result);
            path.pop();
        }
    }
}

pub fn find_all_paths(
    start: String,
    target_word: &str,
    word_graph: &Graph,
) -> Vec<Vec<String>> {
    let mut word_to_index: HashMap<String, usize> = HashMap::new();
    for (i, word) in word_graph.nodes.iter().enumerate() {
        word_to_index.insert(word.clone(), i);
    }

    if !word_to_index.contains_key(&start) || !word_to_index.contains_key(target_word) {
        return vec![];
    }

    let mut result = Vec::new();
    let mut path = vec![start.clone()];
    let mut visited = HashMap::new();
    visited.insert(start.clone(), true);

    dfs_all_paths(
        &start,
        target_word,
        word_graph,
        &word_to_index,
        &mut visited,
        &mut path,
        &mut result,
    );

    result
}

fn dfs_all_paths(
    current: &str,
    target: &str,
    graph: &Graph,
    word_to_index: &HashMap<String, usize>,
    visited: &mut HashMap<String, bool>,
    path: &mut Vec<String>,
    result: &mut Vec<Vec<String>>,
) {
    if current == target {
        result.push(path.clone());
        return;
    }

    // Heuristic: Don't go deeper than typically needed for word ladders to avoid stack overflow or infinite runtime on large graphs?
    // Word ladders can be long, but usually < 20. Let's limit to 50 for safety?
    if path.len() > 50 {
        return;
    }

    let u_idx = *word_to_index.get(current).unwrap();
    for v_idx in &graph.edges[u_idx] {
        let neighbor = &graph.nodes[*v_idx];
        if !*visited.get(neighbor).unwrap_or(&false) {
            visited.insert(neighbor.clone(), true);
            path.push(neighbor.clone());
            dfs_all_paths(neighbor, target, graph, word_to_index, visited, path, result);
            path.pop();
            visited.insert(neighbor.clone(), false);
        }
    }
}

#[allow(dead_code)]
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
    fn test_find_all_shortest_paths() {
        let words = vec![
            "cat".to_string(),
            "cot".to_string(),
            "cog".to_string(),
            "dog".to_string(),
            "cag".to_string(),
        ];
        let (graph, _) = build_graph(words);

        // cat -> cot -> cog -> dog
        // cat -> cag -> cog -> dog
        let paths = find_all_shortest_paths("cat".to_string(), "dog", &graph);
        assert!(!paths.is_empty());
        for p in &paths {
             assert_eq!(p.len(), 4);
             assert_eq!(p[0], "cat");
             assert_eq!(p[3], "dog");
        }
        // Should find exactly 2 shortest paths
        assert_eq!(paths.len(), 2);
    }

    #[test]
    fn test_find_all_paths() {
        let words = vec![
            "cat".to_string(),
            "cot".to_string(),
            "cog".to_string(),
            "dog".to_string(),
        ];
        let (graph, _) = build_graph(words);
        let paths = find_all_paths("cat".to_string(), "dog", &graph);
        assert_eq!(paths.len(), 1);
        assert_eq!(paths[0], vec!["cat", "cot", "cog", "dog"]);
    }
}
