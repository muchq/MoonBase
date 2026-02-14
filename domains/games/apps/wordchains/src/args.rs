use clap::{Command, Arg, arg, value_parser};

pub fn get_cmd() -> Command {
    Command::new("wordchains")
        .bin_name("wordchains")
        .version("0.1.1")
        .subcommand_required(false)
        .arg(
            Arg::new("start")
                .value_name("START")
                .required(false)
                .value_parser(value_parser!(String))
        )
        .arg(
            Arg::new("end")
                .value_name("END")
                .required(false)
                .value_parser(value_parser!(String))
        )
        .subcommand(
            clap::command!("search")
                .arg(
                    arg!(start: <START>)
                        .required(true)
                        .value_parser(value_parser!(String)),
                )
                .arg(
                    arg!(end: <END>)
                        .required(true)
                        .value_parser(value_parser!(String)),
                ),
        )
        .subcommand(
            clap::command!("repl").arg(
                arg!(--"dictionary-path" <PATH>)
                    .required(false)
                    .value_parser(value_parser!(std::path::PathBuf)),
            ),
        )
        .subcommand(
            clap::command!("generate-graph")
                .arg(
                    arg!(--"dictionary-path" <PATH>)
                        .required(true)
                        .value_parser(value_parser!(std::path::PathBuf)),
                )
                .arg(
                    arg!(--"output-path" <PATH>)
                        .required(false)
                        .value_parser(value_parser!(std::path::PathBuf)),
                ),
        )
}
