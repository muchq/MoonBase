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
    // Checked before the push, not after it. Below the target check this bounded
    // descent but not the pushes themselves: a frame entered while there was
    // still room walks its neighbours regardless, and stepping into the target
    // pushed unconditionally — so every frame already past its own check could
    // add one more path after the bound had been reached, and the cap was
    // advisory rather than a bound (#1339).
    if result.len() >= MAX_ALL_PATHS_RESULTS {
        return;
    }

    if current == target {
        result.push(path.clone());
        return;
    }

    if path.len() > max_depth {
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

    /// The cap is a bound, not a suggestion.
    ///
    /// `test_find_all_paths_dense_graph_terminates` below asserts the same
    /// property but only fails on unlucky runs, because `build_graph` iterates
    /// `HashMap`s and Rust seeds their order randomly per process — so the
    /// traversal order, and with it whether the overshoot happens at all,
    /// changes run to run. It failed roughly 2 times in 25. That reads as a
    /// flaky test rather than as the bug report it is, and a test that only
    /// sometimes catches a regression will eventually be re-run instead of read.
    ///
    /// This one builds the graph directly instead. `dfs_all_paths` walks
    /// `edges[u]`, which is a `Vec`, and the `HashMap`s it consults are only
    /// ever read by key — so with the adjacency lists written out explicitly the
    /// whole traversal is deterministic and the outcome is the same every run.
    ///
    /// The shape is what makes the overshoot reachable: `target` is the *last*
    /// neighbour of every `a`, so each `a` frame generates its paths through the
    /// `b` layer first and only then steps to `target`. When the cap trips deep
    /// in that `b` loop, the `a` frame above is still mid-iteration with
    /// `target` pending — and before #1339 the target branch pushed without
    /// consulting the cap at all. 1240 paths exist, so the walk is guaranteed to
    /// cross 1000 well before it runs out.
    #[test]
    fn test_find_all_paths_cap_is_not_exceeded() {
        const A_COUNT: usize = 40;
        const B_COUNT: usize = 30;

        // 0 = start, 1 = target, then the a layer, then the b layer.
        let mut nodes = vec!["start".to_string(), "target".to_string()];
        for i in 0..A_COUNT {
            nodes.push(format!("a{}", i));
        }
        for j in 0..B_COUNT {
            nodes.push(format!("b{}", j));
        }

        let a_start = 2;
        let b_start = 2 + A_COUNT;
        let mut edges: Vec<Vec<usize>> = vec![vec![]; nodes.len()];

        edges[0] = (a_start..a_start + A_COUNT).collect();
        for i in 0..A_COUNT {
            // The b layer first, target last: the ordering the overshoot needs.
            let mut neighbours: Vec<usize> = (b_start..b_start + B_COUNT).collect();
            neighbours.push(1);
            edges[a_start + i] = neighbours;
        }
        for j in 0..B_COUNT {
            edges[b_start + j] = vec![1];
        }

        let graph = Graph { nodes, edges };
        let paths = find_all_paths("start".to_string(), "target", &graph);

        assert!(
            paths.len() <= super::MAX_ALL_PATHS_RESULTS,
            "find_all_paths returned {} paths, over the {} cap: the bound only stopped further \
             descent, so frames already past it still pushed on their way to the target",
            paths.len(),
            super::MAX_ALL_PATHS_RESULTS
        );
        assert_eq!(
            paths.len(),
            super::MAX_ALL_PATHS_RESULTS,
            "1240 paths exist within the depth limit, so collection should stop exactly at the \
             cap — fewer means it now gives up early, which this test would otherwise hide"
        );
        for p in &paths {
            assert_eq!(p.first().unwrap(), "start");
            assert_eq!(p.last().unwrap(), "target");
        }
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
