//! `robotalk`: turns English text (or a raw ARPABET phoneme list) into a
//! poketrack `.rptp` pattern that plays it on a ROBOTALK instrument, or
//! just previews the phonemes text-to-speech would produce. Replaces the
//! old text2phonemes.py + phonemes2rptp.py pair with one self-contained
//! binary -- see g2p.rs for the English-text half, this file for pattern
//! writing.

use clap::{Parser, Subcommand};
use robotalk::g2p;
use robotalk::pattern::{self, DEFAULT_VELOCITY};

#[derive(Parser)]
#[command(name = "robotalk", about = "Text-to-speech helper for the ROBOTALK CLAP instrument")]
struct Cli {
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
    /// Print each word's ARPABET phonemes (debug/preview, no pattern written)
    Phonemes { text: String },
    /// Convert text (or a raw phoneme list) to a poketrack .rptp pattern
    Pattern {
        /// Text to speak, or a space-separated phoneme list with --phonemes
        text: String,
        /// Treat `text` as an already-tokenized phoneme list, not English text
        #[arg(long)]
        phonemes: bool,
        /// Output .rptp path (default: text.rptp)
        #[arg(long)]
        out: Option<String>,
        /// Instrument index the ROBOTALK plugin is set up on
        #[arg(long, default_value_t = 0)]
        instrument: u8,
        /// Rows between each phoneme; 4 = one per beat on a 16th-note grid
        /// at a normal ~120 BPM speaking pace
        #[arg(long, default_value_t = 4)]
        steps_per_phoneme: usize,
        /// Extra rest steps between words, on top of --steps-per-phoneme
        #[arg(long, default_value_t = 4)]
        gap_steps: usize,
    },
}

fn main() {
    match Cli::parse().command {
        Command::Phonemes { text } => cmd_phonemes(&text),
        Command::Pattern { text, phonemes, out, instrument, steps_per_phoneme, gap_steps } => {
            cmd_pattern(&text, phonemes, out, instrument, steps_per_phoneme, gap_steps);
        }
    }
}

fn cmd_phonemes(text: &str) {
    let engine = g2p::new_g2p();
    for (word, phones) in g2p::text_to_arpabet(&engine, text) {
        println!("{:<20} {}", word, phones.join(" "));
    }
}

fn cmd_pattern(text: &str, raw_phonemes: bool, out: Option<String>, instrument: u8, steps_per_phoneme: usize, gap_steps: usize) {
    let words_and_phonemes: Vec<(String, Vec<&str>)> = if raw_phonemes {
        vec![(String::new(), text.split_whitespace().collect())]
    } else {
        let engine = g2p::new_g2p();
        g2p::text_to_arpabet(&engine, text)
    };

    let steps = pattern::build_steps(&words_and_phonemes, steps_per_phoneme, gap_steps, DEFAULT_VELOCITY);
    if steps.is_empty() {
        eprintln!("error: no phonemes produced for {:?}", text);
        std::process::exit(1);
    }

    let out_path = out.unwrap_or_else(|| "text.rptp".to_string());
    let bytes = pattern::rptp_bytes(&steps, instrument);
    if let Some(parent) = std::path::Path::new(&out_path).parent()
        && !parent.as_os_str().is_empty() {
            std::fs::create_dir_all(parent).expect("failed to create output directory");
        }
    std::fs::write(&out_path, &bytes).expect("failed to write pattern file");
    println!("wrote {}  ({} steps)", out_path, steps.len());
}
