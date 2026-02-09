mod args;
mod logging;

use crate::args::get_cmd;
use crate::logging::init_logging;
use log::{error, info};
use std::io::{self, Write};
use wordchains::{bfs_for_target, find_all_paths, find_all_shortest_paths, initialize_graph, Graph};

#[cfg(feature = "embedded-graph")]
mod graph_loader {
    include!(env!("GRAPH_LOADER_PATH"));
}

fn main() {
    init_logging();

    let cmd = get_cmd();
    let arg_matches = cmd.get_matches();

    if let Some(matches) = arg_matches.subcommand_matches("generate-graph") {
        let dict_path = matches.get_one::<std::path::PathBuf>("dictionary-path").unwrap();
        let output_path = matches.get_one::<std::path::PathBuf>("output-path");

        let path_str = dict_path.to_str().unwrap();
        info!("Generating graph from dictionary: {}", path_str);

        let graph = initialize_graph(path_str, None);
        let json = serde_json::to_string(&graph).expect("Failed to serialize graph");

        if let Some(out) = output_path {
            std::fs::write(out, json).expect("Failed to write output file");
            info!("Graph written to {}", out.display());
        } else {
            println!("{}", json);
        }
        return;
    }

    if let Some(matches) = arg_matches.subcommand_matches("repl") {
        let dict_path = matches.get_one::<std::path::PathBuf>("dictionary-path");
        let graph = get_graph(dict_path);
        repl(Some(graph));
        return;
    }

    let (start, end) = if let Some(matches) = arg_matches.subcommand_matches("search") {
        (
            matches.get_one::<String>("start").unwrap(),
            matches.get_one::<String>("end").unwrap(),
        )
    } else if let (Some(start), Some(end)) = (
        arg_matches.get_one::<String>("start"),
        arg_matches.get_one::<String>("end"),
    ) {
        (start, end)
    } else {
        info!("Usage: wordchains <start> <end> OR wordchains repl OR wordchains generate-graph");
        return;
    };

    let word_graph = get_graph(None);

    info!("starting search...");
    let target = bfs_for_target(start.clone(), end, &word_graph);

    match target {
        Some(path) => info!("path found from {} to {}: {:?}", start, end, path),
        None => info!("no path found from {} to {}", start, end),
    }
}

fn get_graph(dict_path: Option<&std::path::PathBuf>) -> Graph {
    if let Some(path) = dict_path {
         let path_str = path.to_str().unwrap();
         info!("Loading dictionary from {}", path_str);
         return initialize_graph(path_str, None);
    }

    #[cfg(feature = "embedded-graph")]
    {
        info!("Loading embedded graph...");
        return serde_json::from_slice(graph_loader::GRAPH_BYTES).expect("Failed to deserialize embedded graph");
    }

    #[cfg(not(feature = "embedded-graph"))]
    {
        let path = "/usr/share/dict/words";
        info!("Loading default dictionary from {}", path);
        initialize_graph(path, None)
    }
}

fn repl(initial_graph: Option<Graph>) {
    let mut graph: Option<Graph> = initial_graph;

    print!("> ");
    io::stdout().flush().unwrap();

    let stdin = io::stdin();
    for line in stdin.lines() {
        if let Ok(line) = line {
            let parts: Vec<&str> = line.trim().split_whitespace().collect();
            if parts.is_empty() {
                print!("> ");
                io::stdout().flush().unwrap();
                continue;
            }

            match parts[0] {
                "read-dict" => {
                    if parts.len() < 2 {
                        println!("Usage: read-dict <path>");
                    } else {
                        let path = parts[1];
                        info!("Loading dictionary from {}", path);
                        graph = Some(initialize_graph(path, None));
                    }
                }
                "read-graph" => {
                    if parts.len() < 2 {
                        println!("Usage: read-graph <path>");
                    } else {
                        let path = parts[1];
                        info!("Loading graph from {}", path);
                        match std::fs::File::open(path) {
                            Ok(file) => {
                                let reader = std::io::BufReader::new(file);
                                match serde_json::from_reader(reader) {
                                    Ok(g) => {
                                        graph = Some(g);
                                        info!("Graph loaded successfully.");
                                    }
                                    Err(e) => error!("Failed to parse graph: {}", e),
                                }
                            }
                            Err(e) => error!("Failed to open file: {}", e),
                        }
                    }
                }
                "shortest" => {
                    if parts.len() < 3 {
                        println!("Usage: shortest <start> <end>");
                    } else if let Some(g) = &graph {
                        let start = parts[1].to_string();
                        let end = parts[2];
                        match bfs_for_target(start.clone(), end, g) {
                            Some(path) => println!("{:?}", path),
                            None => println!("No path found"),
                        }
                    } else {
                        println!("Graph not loaded. Use read-dict or read-graph first.");
                    }
                }
                "all-shortest" => {
                    if parts.len() < 3 {
                        println!("Usage: all-shortest <start> <end>");
                    } else if let Some(g) = &graph {
                        let start = parts[1].to_string();
                        let end = parts[2];
                        let paths = find_all_shortest_paths(start, end, g);
                        if paths.is_empty() {
                             println!("No path found");
                        } else {
                             for p in paths {
                                println!("{:?}", p);
                            }
                        }
                    } else {
                        println!("Graph not loaded. Use read-dict or read-graph first.");
                    }
                }
                "all-paths" => {
                    if parts.len() < 3 {
                        println!("Usage: all-paths <start> <end>");
                    } else if let Some(g) = &graph {
                        let start = parts[1].to_string();
                        let end = parts[2];
                        let paths = find_all_paths(start, end, g);
                        if paths.is_empty() {
                             println!("No path found");
                        } else {
                            for p in paths {
                                println!("{:?}", p);
                            }
                        }
                    } else {
                        println!("Graph not loaded. Use read-dict or read-graph first.");
                    }
                }
                "exit" | "quit" => break,
                _ => println!("Unknown command: {}", parts[0]),
            }
        }
        print!("> ");
        io::stdout().flush().unwrap();
    }
}
