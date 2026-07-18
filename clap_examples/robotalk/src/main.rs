//! `robotalk`: turns English text (or a raw ARPABET phoneme list) into a
//! poketrack `.rptp` pattern that plays it on a ROBOTALK instrument, or
//! just previews the phonemes text-to-speech would produce. Replaces the
//! old text2phonemes.py + phonemes2rptp.py pair with one self-contained
//! binary -- see g2p.rs for the English-text half, this file for pattern
//! writing.

use clap::{Parser, Subcommand};
use robotalk::phonemes::{BASE_NOTE, PHONEMES};

mod g2p;

const NOTE_EMPTY: u8 = 0x00;
const FX_EMPTY: u8 = 0xFF;
const PATTERN_TRACKS: u8 = 16;
const MAX_PATTERN_STEPS: usize = 1024;
const DEFAULT_VELOCITY: u8 = 100;

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

    let steps = build_steps(&words_and_phonemes, steps_per_phoneme, gap_steps, DEFAULT_VELOCITY);
    if steps.is_empty() {
        eprintln!("error: no phonemes produced for {:?}", text);
        std::process::exit(1);
    }

    let out_path = out.unwrap_or_else(|| "text.rptp".to_string());
    write_rptp(&out_path, &steps, instrument);
    println!("wrote {}  ({} steps)", out_path, steps.len());
}

fn note_for_phoneme(symbol: &str) -> Option<u8> {
    PHONEMES
        .iter()
        .position(|p| p.symbol == symbol)
        .map(|i| (BASE_NOTE + i as i32) as u8)
}

/// One entry per pattern step: `Some((note, velocity))` or `None` (rest).
fn build_steps(
    words_and_phonemes: &[(String, Vec<&str>)],
    steps_per_phoneme: usize,
    gap_steps: usize,
    velocity: u8,
) -> Vec<Option<(u8, u8)>> {
    let mut steps: Vec<Option<(u8, u8)>> = Vec::new();
    for (_word, phones) in words_and_phonemes {
        let notes: Vec<u8> = phones
            .iter()
            .filter_map(|p| {
                let note = note_for_phoneme(p);
                if note.is_none() {
                    eprintln!("warning: unknown phoneme {:?}, skipped", p);
                }
                note
            })
            .collect();
        if notes.is_empty() {
            continue;
        }
        if !steps.is_empty() {
            steps.extend(std::iter::repeat_n(None, gap_steps));
        }
        for note in notes {
            steps.push(Some((note, velocity)));
            steps.extend(std::iter::repeat_n(None, steps_per_phoneme.saturating_sub(1)));
        }
    }
    while matches!(steps.last(), Some(None)) {
        steps.pop();
    }
    steps
}

/// Write an RPTP v2 pattern: track 0 carries the phoneme notes, tracks
/// 1-15 are left empty. Step layout: note, vel, inst, fx0, fxv0, fx1, fxv1.
fn write_rptp(path: &str, steps: &[Option<(u8, u8)>], instrument: u8) {
    let length = steps.len().clamp(1, MAX_PATTERN_STEPS);
    let mut out = Vec::with_capacity(9 + PATTERN_TRACKS as usize * length * 7);
    out.extend_from_slice(b"RPTP");
    out.extend_from_slice(&2u16.to_le_bytes());
    out.extend_from_slice(&(length as u16).to_le_bytes());
    out.push(PATTERN_TRACKS);

    for track in 0..PATTERN_TRACKS {
        for step_index in 0..length {
            let cell = if track == 0 { steps.get(step_index).copied().flatten() } else { None };
            match cell {
                None => out.extend_from_slice(&[NOTE_EMPTY, 0, 0, FX_EMPTY, 0, FX_EMPTY, 0]),
                Some((note, vel)) => out.extend_from_slice(&[note, vel, instrument, FX_EMPTY, 0, FX_EMPTY, 0]),
            }
        }
    }

    if let Some(parent) = std::path::Path::new(path).parent()
        && !parent.as_os_str().is_empty() {
            std::fs::create_dir_all(parent).expect("failed to create output directory");
        }
    std::fs::write(path, &out).expect("failed to write pattern file");
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn note_for_phoneme_matches_plugin_table() {
        // BASE_NOTE (36) + index in PHONEMES, same table the plugin itself
        // uses -- these two must never drift apart.
        assert_eq!(note_for_phoneme("IY"), Some(36));
        assert_eq!(note_for_phoneme("HH"), Some(30 + 36));
        assert_eq!(note_for_phoneme("G"), Some(36 + 36));
        assert_eq!(note_for_phoneme("nope"), None);
    }

    #[test]
    fn build_steps_lays_out_notes_gaps_and_trims_trailing_rest() {
        let words = vec![("hi".to_string(), vec!["HH", "AY"])];
        let steps = build_steps(&words, 2, 3, 100);
        // HH, rest, AY -- trailing rest after the last phoneme is trimmed.
        let hh = note_for_phoneme("HH").unwrap();
        let ay = note_for_phoneme("AY").unwrap();
        assert_eq!(steps, vec![Some((hh, 100)), None, Some((ay, 100))]);
    }

    #[test]
    fn build_steps_inserts_gap_between_words() {
        let words = vec![("a".to_string(), vec!["AH"]), ("b".to_string(), vec!["B"])];
        let steps = build_steps(&words, 1, 2, 100);
        let ah = note_for_phoneme("AH").unwrap();
        let b = note_for_phoneme("B").unwrap();
        assert_eq!(steps, vec![Some((ah, 100)), None, None, Some((b, 100))]);
    }

    #[test]
    fn build_steps_skips_unknown_phonemes() {
        let words = vec![("x".to_string(), vec!["NOTAPHONEME", "P"])];
        let steps = build_steps(&words, 1, 4, 100);
        let p = note_for_phoneme("P").unwrap();
        assert_eq!(steps, vec![Some((p, 100))]);
    }

    #[test]
    fn write_rptp_matches_expected_byte_layout() {
        let path = std::env::temp_dir().join("robotalk_test_write_rptp.rptp");
        let path_str = path.to_str().unwrap();
        let steps = vec![Some((66u8, 100u8)), None];
        write_rptp(path_str, &steps, 3);

        let bytes = std::fs::read(&path).unwrap();
        std::fs::remove_file(&path).ok();

        assert_eq!(&bytes[0..4], b"RPTP");
        assert_eq!(u16::from_le_bytes([bytes[4], bytes[5]]), 2); // version
        assert_eq!(u16::from_le_bytes([bytes[6], bytes[7]]), 2); // length
        assert_eq!(bytes[8], PATTERN_TRACKS);
        assert_eq!(bytes.len(), 9 + PATTERN_TRACKS as usize * 2 * 7);
        // track 0, step 0: note=66, vel=100, inst=3, fx0=0xFF, fxv0=0, fx1=0xFF, fxv1=0
        assert_eq!(&bytes[9..16], &[66, 100, 3, 0xFF, 0, 0xFF, 0]);
        // track 0, step 1: empty rest
        assert_eq!(&bytes[16..23], &[0, 0, 0, 0xFF, 0, 0xFF, 0]);
        // track 1, step 0: also empty (only track 0 carries notes)
        let track1_start = 9 + 2 * 7;
        assert_eq!(&bytes[track1_start..track1_start + 7], &[0, 0, 0, 0xFF, 0, 0xFF, 0]);
    }
}
