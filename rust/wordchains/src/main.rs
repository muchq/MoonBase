use std::{
    collections::{HashMap, VecDeque},
    env,
    fs::read_to_string,
};

#[derive(Debug, Clone)]
struct Node {
    value: String,
    parent: Option<Box<Node>>,
}

fn read_words(filename: String) -> Vec<String> {
    let mut result = Vec::new();

    for line in read_to_string(filename).unwrap().lines() {
        result.push(line.trim().to_lowercase());
    }

    result
}

// TODO: doc + tests
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

fn to_path(end: Node) -> Option<Vec<String>> {
    let mut path: Vec<String> = vec![end.value.clone()];
    let mut node = end;
    while node.parent.is_some() {
        let parent = node.parent?;
        path.push(parent.value.clone());
        node = *parent;
    }
    path.reverse();
    Some(path)
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
            return to_path(target_node);
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

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() != 3 {
        println!("Usage: {} <start> <end>", args[0]);
        return;
    }

    let start = &args[1];
    let end = &args[2];

    if start.len() != end.len() {
        println!("No chain between {} and {}", start, end);
        return;
    }

    // TODO: dictionary path from args
    let path = "/usr/share/dict/words";
    //let path = "/Volumes/Envoy Ultra 4TB/words/oed_2/oed_words.txt";

    // TODO: prebuilt dictionary + graph?
    println!("Reading dictionary from \"{}\"...", path);
    let words: Vec<String> = read_words(String::from(path))
        .iter()
        .filter(|word| word.len() == start.len())
        .map(|word| word.to_owned())
        .collect();
    let num_words = words.len();
    let mut word_to_index: HashMap<String, usize> = HashMap::new();
    for (i, word) in words.iter().enumerate() {
        word_to_index.insert(word.to_owned(), i);
    }

    if !word_to_index.contains_key(start) {
        println!("{} is not in my dictionary.", &start);
        return;
    }

    if !word_to_index.contains_key(end) {
        println!("{} is not in my dictionary.", &end);
        return;
    }

    println!("building graph...");
    let mut word_graph: Vec<Vec<usize>> = vec![vec![]; num_words];
    for (i, word1) in words.iter().enumerate() {
        for j in 0..num_words {
            let word2 = words[j].clone();
            if words_are_one_away(word1, &word2) {
                word_graph[i].push(j);
            }
        }
    }
    // println!("{:?}", word_graph);

    // let mut file = File::create("60df739928ea654af9a0e7cf8fa19e3f.graph").unwrap();
    // file.write_all(&indexes).unwrap();
    println!("starting search...");
    let target = bfs_for_target(start.clone(), end, &word_graph, &word_to_index, &words);

    match target {
        Some(path) => println!("Path from {} to {}: {:?}", start, end, path),
        None => println!("No path found from {} to {}", start, end),
    }
}
