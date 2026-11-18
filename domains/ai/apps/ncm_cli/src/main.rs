use candle_core::Device;
use clap::{Parser, Subcommand};
use ncm::{
    Result,
    agent::{Harness, ReadFile},
    data,
    model::Config,
    pipeline,
};
use std::{fs, io::Write, path::PathBuf};

#[derive(Parser)]
#[command(about = "Experimental Rust/Candle next-concept training and agent inference")]
struct Cli {
    #[arg(long, default_value = "cpu", global = true)]
    device: String,
    #[command(subcommand)]
    command: Command,
}
#[derive(Subcommand)]
enum Command {
    /// Collect whole source files into language-tagged JSONL records.
    Prepare {
        #[arg(long)]
        root: PathBuf,
        #[arg(long)]
        language: String,
        #[arg(long, value_delimiter = ',')]
        extensions: Vec<String>,
        #[arg(long)]
        output: PathBuf,
        #[arg(long, default_value_t = 65536)]
        max_bytes: u64,
    },
    /// Print a starter model configuration.
    Config,
    /// Train BPE and the residual VQ autoencoder.
    TrainTokenizer {
        #[arg(long)]
        input: PathBuf,
        #[arg(long)]
        output: PathBuf,
        #[arg(long)]
        config: Option<PathBuf>,
        #[arg(long, default_value_t = 1000)]
        steps: usize,
        #[arg(long, default_value_t = 0.001)]
        lr: f64,
        #[arg(long, default_value_t = 42)]
        seed: u64,
    },
    /// Train or fine-tune the generator; tokenizer stays frozen. Optimizer starts fresh.
    TrainGenerator {
        #[arg(long)]
        input: PathBuf,
        #[arg(long)]
        model: PathBuf,
        #[arg(long)]
        output: PathBuf,
        #[arg(long, default_value_t = 1000)]
        steps: usize,
        #[arg(long, default_value_t = 0.001)]
        lr: f64,
    },
    Evaluate {
        #[arg(long)]
        input: PathBuf,
        #[arg(long)]
        model: PathBuf,
    },
    Generate {
        #[arg(long)]
        model: PathBuf,
        #[arg(long)]
        language: String,
        #[arg(long)]
        prompt: String,
    },
    Agent {
        #[arg(long)]
        model: PathBuf,
        #[arg(long)]
        language: String,
        #[arg(long)]
        task: String,
        #[arg(long)]
        workspace: PathBuf,
        #[arg(long, default_value_t = 8)]
        max_steps: usize,
    },
}
fn device(name: &str) -> Result<Device> {
    match name {
        "cpu" => Ok(Device::Cpu),
        #[cfg(feature = "metal")]
        "metal" => Ok(Device::new_metal(0)?),
        #[cfg(feature = "cuda")]
        "cuda" => Ok(Device::new_cuda(0)?),
        _ => Err(format!(
            "unsupported device {name}; accelerator support must be enabled at build time"
        )
        .into()),
    }
}
fn run() -> Result<()> {
    let cli = Cli::parse();
    let device = device(&cli.device)?;
    match cli.command {
        Command::Config => println!("{}", serde_json::to_string_pretty(&Config::default())?),
        Command::Prepare {
            root,
            language,
            extensions,
            output,
            max_bytes,
        } => {
            let records = data::collect(&root, &language, &extensions, max_bytes)?;
            let mut f = fs::OpenOptions::new()
                .write(true)
                .create_new(true)
                .open(output)?;
            for r in &records {
                writeln!(f, "{}", serde_json::to_string(r)?)?;
            }
            eprintln!(
                "wrote {} unique files; curate prompt/completion tasks before agent training",
                records.len()
            );
        }
        Command::TrainTokenizer {
            input,
            output,
            config,
            steps,
            lr,
            seed,
        } => {
            let c = match config {
                Some(p) => serde_json::from_slice(&fs::read(p)?)?,
                None => Config::default(),
            };
            pipeline::train_tokenizer(&input, &output, c, steps, lr, seed, &device)?;
        }
        Command::TrainGenerator {
            input,
            model,
            output,
            steps,
            lr,
        } => pipeline::train_generator(&input, &model, &output, steps, lr, &device)?,
        Command::Evaluate { input, model } => println!(
            "{}",
            serde_json::to_string_pretty(&pipeline::evaluate(&input, &model, &device)?)?
        ),
        Command::Generate {
            model,
            language,
            prompt,
        } => println!(
            "{}",
            pipeline::LocalBackend::load(&model, language, &device)?.generate(&prompt)?
        ),
        Command::Agent {
            model,
            language,
            task,
            workspace,
            max_steps,
        } => {
            let mut backend = pipeline::LocalBackend::load(&model, language, &device)?;
            let mut harness = Harness::default();
            harness.max_steps = max_steps;
            harness.register(
                "read_file".into(),
                Box::new(ReadFile::new(&workspace, 4096)?),
            )?;
            println!(
                "{}",
                serde_json::to_string_pretty(&harness.run(&mut backend, &task)?)?
            );
        }
    }
    Ok(())
}
fn main() {
    if let Err(e) = run() {
        eprintln!("ncm: {e}");
        std::process::exit(1);
    }
}
