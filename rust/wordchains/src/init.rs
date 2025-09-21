use crate::graph::build_graph;
use crate::model::Graph;
use crate::sha::compute_sha;
use crate::storage::{read_dictionary, read_existing_graph, write_graph_to_file};

pub fn initialize_graph(dict_path: &str, data_dir: Option<&str>) -> Graph {
    let words = read_dictionary(dict_path);
    let digest = compute_sha(&words);

    match read_existing_graph(&digest, words.len(), data_dir) {
        Some(e) => Graph {
            nodes: words,
            edges: e,
        },
        None => {
            let (g, data) = build_graph(words);
            write_graph_to_file(data, &digest, data_dir);
            g
        }
    }
}
