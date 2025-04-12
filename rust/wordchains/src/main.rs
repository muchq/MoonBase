mod graph;

use log::{info, warn, LevelFilter};
use std::fs::File;
use std::io::Write;
use std::{
    collections::HashMap,
    env,
    fs::read_to_string,
};
use std::collections::HashSet;

use simplelog::{ColorChoice, Config, TermLogger, TerminalMode};
use crate::graph::{bfs_for_target, build_graph};

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

fn read_dictionary(path: &str) -> Vec<String> {
    info!("Reading dictionary from \"{}\"...", path);
    read_words(String::from(path))
        .iter()
        .filter(|word| word.len() < 9)
        .filter(|word| word.len() == 5)
        .map(|word| word.to_owned())
        .collect()
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
