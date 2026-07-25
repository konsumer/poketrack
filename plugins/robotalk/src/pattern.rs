//! Phoneme sequence -> `.rptp` pattern bytes. Shared by the native CLI
//! (main.rs, which adds the file-writing step) and the browser build
//! (web.rs, which hands the bytes straight to JS as a download).

use crate::phonemes::{BASE_NOTE, PHONEMES};

pub const NOTE_EMPTY: u8 = 0x00;
pub const FX_EMPTY: u8 = 0xFF;
pub const PATTERN_TRACKS: u8 = 16;
pub const MAX_PATTERN_STEPS: usize = 1024;
pub const DEFAULT_VELOCITY: u8 = 100;

pub fn note_for_phoneme(symbol: &str) -> Option<u8> {
    PHONEMES
        .iter()
        .position(|p| p.symbol == symbol)
        .map(|i| (BASE_NOTE + i as i32) as u8)
}

/// One entry per pattern step: `Some((note, velocity))` or `None` (rest).
pub fn build_steps(
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

/// Encode an RPTP v2 pattern: track 0 carries the phoneme notes, tracks
/// 1-15 are left empty. Step layout: note, vel, inst, fx0, fxv0, fx1, fxv1.
pub fn rptp_bytes(steps: &[Option<(u8, u8)>], instrument: u8) -> Vec<u8> {
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
    out
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
    fn rptp_bytes_matches_expected_byte_layout() {
        let steps = vec![Some((66u8, 100u8)), None];
        let bytes = rptp_bytes(&steps, 3);

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
