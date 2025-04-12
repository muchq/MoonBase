use log::{info, warn, LevelFilter};
use std::fs::File;
use std::io::Write;
use std::{
    collections::{HashMap, VecDeque},
    env,
    fs::read_to_string,
};
use std::collections::HashSet;

use simplelog::{ColorChoice, Config, TermLogger, TerminalMode};

#[derive(Debug, Clone)]
struct Node {
    value: String,
    parent: Option<Box<Node>>,
}

// TODO: handle missing file
fn read_words(filename: String) -> Vec<String> {
    read_to_string(filename)
        .unwrap()
        .lines()
        .map(|l| l.trim().to_lowercase())
        .collect::<HashSet<String>>()
        .into_iter()
        .collect()
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

fn bfs_for_target(
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

fn read_dictionary(path: &str) -> Vec<String> {
    info!("Reading dictionary from \"{}\"...", path);
    read_words(String::from(path))
        .iter()
        .filter(|word| word.len() < 9)
        .filter(|word| word.len() == 5)
        .map(|word| word.to_owned())
        .collect()
}

fn build_graph(words: &Vec<String>) -> (Vec<Vec<usize>>, Vec<usize>) {
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

fn write_graph_to_file(matches: Vec<usize>, path: &str) -> () {
    let mut indexes: Vec<u8> = Vec::new();
    for m in matches {
        for x in m.to_be_bytes() {
            indexes.push(x);
        }
    }

    let mut file = File::create(path).unwrap();
    file.write_all(&indexes).unwrap();
}

fn main() {
    TermLogger::init(
        LevelFilter::Debug,
        Config::default(),
        TerminalMode::Stdout,
        ColorChoice::Always
    ).unwrap();

    let args: Vec<String> = env::args().collect();
    if args.len() != 3 {
        info!("Usage: {} <start> <end>", args[0]);
        return;
    }

    let start = &args[1];
    let end = &args[2];

    if start.len() != end.len() {
        info!("No chain between {} and {}", start, end);
        return;
    }

    // TODO: dictionary path from args
    let path = "/usr/share/dict/words";
    //let path = "/Volumes/Envoy Ultra 4TB/words/oed_2/oed_words.txt";

    // TODO: prebuilt dictionary + graph?
    let words: Vec<String> = read_dictionary(path);
    let mut word_to_index: HashMap<String, usize> = HashMap::new();
    for (i, word) in words.iter().enumerate() {
        word_to_index.insert(word.to_owned(), i);
    }

    if !word_to_index.contains_key(start) {
        warn!("{} is not in my dictionary.", &start);
        return;
    }

    if !word_to_index.contains_key(end) {
        warn!("{} is not in my dictionary.", &end);
        return;
    }

    let (word_graph, matches) = build_graph(&words);

    // println!("{:?}", word_graph);
    // TODO: use digest of read_dictionary
    write_graph_to_file(matches, "60df739928ea654af9a0e7cf8fa19e3f.graph");

    info!("starting search...");
    let target = bfs_for_target(start.clone(), end, &word_graph, &word_to_index, &words);

    match target {
        Some(path) => info!("path found from {} to {}: {:?}", start, end, path),
        None => info!("no path found from {} to {}", start, end),
    }
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
