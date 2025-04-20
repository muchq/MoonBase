mod graph;
mod storage;
mod model;
mod repl;
mod logging;
mod args;
mod sha;

use log::info;
use crate::graph::{bfs_for_target, build_graph};
use crate::args::get_cmd;
use crate::logging::init_logging;
use crate::model::Graph;
use crate::sha::compute_sha;
use crate::storage::{read_dictionary, read_existing_graph, write_graph_to_file};

fn main() {
    init_logging();

    let cmd = get_cmd();
    let arg_matches = cmd.get_matches();

    // TODO: de-goop subcommand stuff
    let start_maybe = arg_matches.get_one::<String>("start");
    let end_maybe = arg_matches.get_one::<String>("end");

    let subcommand = arg_matches.subcommand_matches("search");
    if let Some(_search) = subcommand {}

    if start_maybe.is_none() || end_maybe.is_none() {
        info!("Usage: wordchains <start> <end>");
        return;
    }

    let start = start_maybe.unwrap();
    let end = end_maybe.unwrap();

    // TODO: dictionary path from args
    let path = "/usr/share/dict/words";
    //let path = "/Volumes/Envoy Ultra 4TB/words/oed_2/oed_words.txt";

    let words = read_dictionary(path);
    let digest = compute_sha(&words);

    let word_graph = match read_existing_graph(&digest, words.len()) {
        Some(e) => Graph{nodes: words, edges: e},
        None => {
            let (g, data) = build_graph(words, &digest);
            write_graph_to_file(data, &digest);
            g
        }
    };

    info!("starting search...");
    let target = bfs_for_target(start.clone(), end, word_graph);

    match target {
        Some(path) => info!("path found from {} to {}: {:?}", start, end, path),
        None => info!("no path found from {} to {}", start, end),
    }
}
