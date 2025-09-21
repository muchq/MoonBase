mod logging;
mod args;

use log::info;
use crate::args::get_cmd;
use crate::logging::init_logging;
use wordchains::{initialize_graph, bfs_for_target};

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

    let word_graph = initialize_graph(path, None);

    info!("starting search...");
    let target = bfs_for_target(start.clone(), end, &word_graph);

    match target {
        Some(path) => info!("path found from {} to {}: {:?}", start, end, path),
        None => info!("no path found from {} to {}", start, end),
    }
}
