use crate::model::Graph;
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

    // Optimization: Use HashMap<&str, usize> to avoid cloning strings
    let mut word_to_index: HashMap<&str, usize> = HashMap::with_capacity(word_graph.nodes.len());
    for (i, word) in word_graph.nodes.iter().enumerate() {
        word_to_index.insert(word.as_str(), i);
    }

    let start_idx = match word_to_index.get(start.as_str()) {
        Some(&i) => i,
        None => {
            event!(Level::DEBUG, "{} is not in dictionary.", &start);
            return None;
        }
    };

    let target_idx = match word_to_index.get(target_word) {
        Some(&i) => i,
        None => {
            event!(Level::DEBUG, "{} is not in dictionary.", &target_word);
            return None;
        }
    };

    // Optimization: Use integer-based BFS
    let mut parents: Vec<Option<usize>> = vec![None; word_graph.nodes.len()];
    let mut visited: Vec<bool> = vec![false; word_graph.nodes.len()];
    let mut queue: VecDeque<usize> = VecDeque::new();

    visited[start_idx] = true;
    queue.push_back(start_idx);

    while let Some(u) = queue.pop_front() {
        if u == target_idx {
            // Path reconstruction
            let mut path = Vec::new();
            let mut curr = u;
            path.push(word_graph.nodes[curr].clone());
            while let Some(p) = parents[curr] {
                path.push(word_graph.nodes[p].clone());
                curr = p;
            }
            path.reverse();
            return Some(path);
        }

        for &v in &word_graph.edges[u] {
            if !visited[v] {
                visited[v] = true;
                parents[v] = Some(u);
                queue.push_back(v);
            }
        }
    }
    None
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

const MAX_ALL_PATHS_RESULTS: usize = 1000;
const MAX_ALL_PATHS_DEPTH_MARGIN: usize = 2;

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

    if start == target_word {
        return vec![vec![start]];
    }

    // BFS to find shortest path length, then cap DFS depth to avoid combinatorial explosion.
    let max_depth = match bfs_shortest_distance(&start, target_word, word_graph, &word_to_index) {
        Some(d) => d + MAX_ALL_PATHS_DEPTH_MARGIN,
        None => return vec![],
    };

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
        max_depth,
    );

    result
}

fn bfs_shortest_distance(
    start: &str,
    target: &str,
    graph: &Graph,
    word_to_index: &HashMap<String, usize>,
) -> Option<usize> {
    let mut dist: HashMap<&str, usize> = HashMap::new();
    let mut queue: VecDeque<&str> = VecDeque::new();
    dist.insert(start, 0);
    queue.push_back(start);

    while let Some(current) = queue.pop_front() {
        let d = dist[current];
        if current == target {
            return Some(d);
        }
        let u_idx = *word_to_index.get(current).unwrap();
        for v_idx in &graph.edges[u_idx] {
            let neighbor = graph.nodes[*v_idx].as_str();
            if !dist.contains_key(neighbor) {
                dist.insert(neighbor, d + 1);
                queue.push_back(neighbor);
            }
        }
    }
    None
}

fn dfs_all_paths(
    current: &str,
    target: &str,
    graph: &Graph,
    word_to_index: &HashMap<String, usize>,
    visited: &mut HashMap<String, bool>,
    path: &mut Vec<String>,
    result: &mut Vec<Vec<String>>,
    max_depth: usize,
) {
    if current == target {
        result.push(path.clone());
        return;
    }

    if path.len() > max_depth || result.len() >= MAX_ALL_PATHS_RESULTS {
        return;
    }

    let u_idx = *word_to_index.get(current).unwrap();
    for v_idx in &graph.edges[u_idx] {
        let neighbor = &graph.nodes[*v_idx];
        if !*visited.get(neighbor).unwrap_or(&false) {
            visited.insert(neighbor.clone(), true);
            path.push(neighbor.clone());
            dfs_all_paths(neighbor, target, graph, word_to_index, visited, path, result, max_depth);
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

    #[test]
    fn test_find_all_paths_multiple_routes() {
        // Two shortest paths plus one longer path within the depth margin
        let words = vec![
            "cat".to_string(),
            "cot".to_string(),
            "cog".to_string(),
            "dog".to_string(),
            "cag".to_string(),
            "dag".to_string(),
        ];
        let (graph, _) = build_graph(words);
        let paths = find_all_paths("cat".to_string(), "dog", &graph);
        // Shortest is length 4 (cat-cot-cog-dog, cat-cag-cog-dog)
        // With margin of 2, max depth is 6, so longer routes are also included
        assert!(paths.len() >= 2);
        for p in &paths {
            assert_eq!(p.first().unwrap(), "cat");
            assert_eq!(p.last().unwrap(), "dog");
            assert!(p.len() <= 7); // shortest (4) + margin (2) + 1
        }
    }

    #[test]
    fn test_find_all_paths_no_path() {
        let words = vec![
            "cat".to_string(),
            "cot".to_string(),
            "dog".to_string(), // not reachable from cat/cot (no cog)
        ];
        let (graph, _) = build_graph(words);
        let paths = find_all_paths("cat".to_string(), "dog", &graph);
        assert!(paths.is_empty());
    }

    #[test]
    fn test_find_all_paths_same_word() {
        let words = vec!["cat".to_string()];
        let (graph, _) = build_graph(words);
        let paths = find_all_paths("cat".to_string(), "cat", &graph);
        assert_eq!(paths.len(), 1);
        assert_eq!(paths[0], vec!["cat"]);
    }

    #[test]
    fn test_find_all_paths_missing_word() {
        let words = vec!["cat".to_string()];
        let (graph, _) = build_graph(words);
        let paths = find_all_paths("cat".to_string(), "dog", &graph);
        assert!(paths.is_empty());
        let paths = find_all_paths("dog".to_string(), "cat", &graph);
        assert!(paths.is_empty());
    }

    #[test]
    fn test_find_all_paths_dense_graph_terminates() {
        // Build a dense 3-letter word graph that would explode without bounds
        let mut words = Vec::new();
        for a in b'a'..=b'z' {
            for b in b'a'..=b'z' {
                for c in b'a'..=b'z' {
                    words.push(String::from_utf8(vec![a, b, c]).unwrap());
                }
            }
        }
        let (graph, _) = build_graph(words);
        let paths = find_all_paths("cat".to_string(), "bot", &graph);
        // Should terminate and return bounded results
        assert!(!paths.is_empty());
        assert!(paths.len() <= super::MAX_ALL_PATHS_RESULTS);
        for p in &paths {
            assert_eq!(p.first().unwrap(), "cat");
            assert_eq!(p.last().unwrap(), "bot");
        }
    }
}
