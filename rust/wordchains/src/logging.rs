use log::LevelFilter;
use simplelog::{ColorChoice, Config, TermLogger, TerminalMode};

pub fn init_logging() {
    TermLogger::init(
        LevelFilter::Debug,
        Config::default(),
        TerminalMode::Stdout,
        ColorChoice::Always,
    )
        .unwrap();
}
