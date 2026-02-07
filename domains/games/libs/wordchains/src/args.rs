use clap::{Command, arg, value_parser};

pub fn get_cmd() -> Command {
    Command::new("wordchains")
        .bin_name("wordchains")
        .subcommand_required(false)
        .arg(arg!(start: <START>).value_parser(value_parser!(String)))
        .arg(arg!(end: <END>).value_parser(value_parser!(String)))
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
                    .default_value("/usr/share/dict/words")
                    .value_parser(value_parser!(std::path::PathBuf)),
            ),
        )
        .subcommand(
            clap::command!("generate-graph").arg(
                arg!(--"dictionary-path" <PATH>).value_parser(value_parser!(std::path::PathBuf)),
            ),
        )
}
