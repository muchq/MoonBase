use std::collections::HashSet;
use std::fs::{read, read_to_string, write};
use tracing::{event, Level};

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

pub fn read_dictionary(path: &str) -> Vec<String> {
    event!(Level::INFO, "Reading dictionary from \"{}\"...", path);
    let mut word_list: Vec<String> = read_words(String::from(path))
        .iter()
        .filter(|word| word.len() < 9)
        .map(|word| word.to_owned())
        .collect::<HashSet<String>>()
        .iter()
        .map(|x| x.to_owned())
        .collect();

    word_list.sort();

    word_list
}

fn digest_to_path(digest: &str, data_dir: Option<&str>) -> String {
    match data_dir {
        Some(dir) => format!("{}/{}.graph", dir, digest),
        None => format!("{}.graph", digest),
    }
}

pub fn read_existing_graph(digest: &str, num_words: usize, data_dir: Option<&str>) -> Option<Vec<Vec<usize>>> {
    let mut word_graph = vec![vec![]; num_words];
    match read(digest_to_path(digest, data_dir)) {
        Ok(content) => {
            event!(Level::INFO, "reading graph from {}.graph...", digest);
            let mut chunks = content.chunks(8);
            let num_chunks = chunks.len();
            for _ in 0..(num_chunks / 2) {
                let i_bytes: [u8; 8] = chunks.next().unwrap().try_into().expect("invalid graph file");
                let j_bytes: [u8; 8] = chunks.next().unwrap().try_into().expect("invalid graph file");
                let i = usize::from_be_bytes(i_bytes);
                let j = usize::from_be_bytes(j_bytes);
                word_graph[i].push(j);
                word_graph[j].push(i);
            }
            Some(word_graph)
        },
        Err(_) => {
            None
        }
    }
}

pub fn write_graph_to_file(matches: Vec<usize>, digest: &str, data_dir: Option<&str>) {
    let mut indexes: Vec<u8> = Vec::new();
    for m in matches {
        for x in m.to_be_bytes() {
            indexes.push(x);
        }
    }

    write(digest_to_path(digest, data_dir), indexes).unwrap();
}
