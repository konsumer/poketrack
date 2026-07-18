//! English text -> robotalk's 37-symbol ARPABET set, via `misaki-rs`.
//!
//! misaki-rs's lexicon/tagger data is `include_str!`'d into the binary at
//! compile time (no runtime data directory), which is the whole reason for
//! using it here. Its actual output was verified empirically (not just
//! from its docs, which are stale in places) with `default-features =
//! false` (no espeak, no C toolchain): diphthongs and affricates come out
//! as two base IPA characters joined by a U+200D ZERO WIDTH JOINER rather
//! than single symbols, and rhotic vowels come out as a vowel followed by
//! `ɹ` -- except the NURSE vowel, which comes out as `ɜ`/`ɚ` optionally
//! followed by a bare `ː` length mark instead. Out-of-vocabulary words
//! (misaki-rs has no CMUdict-style clean-room fallback without espeak) get
//! spelled out letter by letter -- that's accepted as-is; type a
//! phonetic-ish spelling instead if a specific word matters.

use misaki_rs::{G2P, Language};

pub fn new_g2p() -> G2P {
    G2P::new(Language::EnglishUS)
}

/// word -> ARPABET phoneme sequence, one entry per token misaki-rs
/// produced (words, numbers-expanded-to-words, punctuation is skipped).
pub fn text_to_arpabet(g2p: &G2P, text: &str) -> Vec<(String, Vec<&'static str>)> {
    let (_, tokens) = match g2p.g2p(text) {
        Ok(result) => result,
        Err(e) => {
            eprintln!("warning: g2p failed for {:?}: {}", text, e);
            return Vec::new();
        }
    };

    let mut out = Vec::new();
    for tok in tokens {
        let Some(ipa) = tok.phonemes else { continue };
        let phonemes = ipa_to_arpabet(&ipa);
        if !phonemes.is_empty() {
            out.push((tok.text, phonemes));
        }
    }
    out
}

/// Compound (ZWJ-joined) diphthong/affricate pairs -> one or two ARPABET
/// symbols. Affricates expand the same way the old text2phonemes.py did
/// (CH -> T, SH; JH -> D, ZH), since robotalk doesn't model them as their
/// own phoneme.
fn compound_symbol(pair: (char, char)) -> Option<&'static [&'static str]> {
    match pair {
        ('a', 'ɪ') => Some(&["AY"]),
        ('a', 'ʊ') => Some(&["AW"]),
        ('ɔ', 'ɪ') => Some(&["OY"]),
        ('e', 'ɪ') => Some(&["EY"]),
        ('o', 'ʊ') => Some(&["OW"]),
        ('d', 'ʒ') => Some(&["D", "ZH"]),
        ('t', 'ʃ') => Some(&["T", "SH"]),
        _ => None,
    }
}

/// Everything that isn't a compound pair or a rhotic vowel: consonants
/// 1:1, plus the capital diphthong markers misaki-rs's letter-spelling
/// fallback occasionally emits for single-letter words (e.g. "I" -> "eye").
fn single_symbol(c: char) -> Option<&'static str> {
    match c {
        'b' => Some("B"),
        'd' => Some("D"),
        'f' => Some("F"),
        'h' => Some("HH"),
        'j' => Some("Y"),
        'k' => Some("K"),
        'l' => Some("L"),
        'm' => Some("M"),
        'n' => Some("N"),
        'p' => Some("P"),
        's' => Some("S"),
        't' => Some("T"),
        'v' => Some("V"),
        'w' => Some("W"),
        'z' => Some("Z"),
        'ɡ' => Some("G"),
        'ŋ' => Some("NG"),
        'ɹ' => Some("R"),
        'ʃ' => Some("SH"),
        'ʒ' => Some("ZH"),
        'ð' => Some("DH"),
        'θ' => Some("TH"),
        'ɾ' => Some("T"), // flap t/d ("butter"): CMUdict transcribes this as T
        // schwa/near-open central/STRUT: CMUdict uses AH for all three
        // (disambiguated only by a stress digit robotalk doesn't track).
        'ə' | 'ɐ' | 'ʌ' => Some("AH"),
        'i' => Some("IY"),
        'u' => Some("UW"),
        'ɑ' => Some("AA"),
        'ɔ' => Some("AO"),
        'ɛ' => Some("EH"),
        'ɪ' => Some("IH"),
        'ʊ' => Some("UH"),
        'æ' | 'a' => Some("AE"),
        // capital letter-name/diphthong markers (documented upstream,
        // occasionally surfaces for single-letter words)
        'I' => Some("AY"),
        'O' | 'Q' => Some("OW"),
        'A' => Some("EY"),
        'W' => Some("AW"),
        'Y' => Some("OY"),
        _ => None,
    }
}

fn ipa_to_arpabet(ipa: &str) -> Vec<&'static str> {
    let chars: Vec<char> = ipa.chars().collect();
    let mut out = Vec::new();
    let mut i = 0;
    while i < chars.len() {
        let c = chars[i];

        if c == 'ˈ' || c == 'ˌ' || c.is_whitespace() {
            i += 1;
            continue;
        }

        // ZWJ-joined compound: two base chars joined by U+200D.
        if i + 2 < chars.len() && chars[i + 1] == '\u{200d}'
            && let Some(syms) = compound_symbol((c, chars[i + 2])) {
                out.extend_from_slice(syms);
                i += 3;
                continue;
            }

        // NURSE vowel: ɜ/ɚ, optionally followed by a length mark and/or a
        // (redundant, in this crate's output) trailing ɹ -- collapses to
        // one r-colored ARPABET symbol, not vowel+R.
        if c == 'ɜ' || c == 'ɚ' {
            i += 1;
            while i < chars.len() && (chars[i] == 'ː' || chars[i] == 'ɹ') {
                i += 1;
            }
            out.push("ER");
            continue;
        }

        // A stray length mark with no rhotic context to attach to: drop.
        // Same for a lone joiner -- e.g. rhotic vowels come out as
        // vowel + ː + ZWJ + ɹ, so once the vowel and length mark are
        // each consumed on their own, the joiner is left dangling before
        // ɹ's own iteration.
        if c == 'ː' || c == '\u{200d}' {
            i += 1;
            continue;
        }

        // Any other rhotic vowel (e.g. "car" -> ɑːɹ, "here" -> ɪɹ) needs no
        // special-casing: it's just a vowel followed, a couple characters
        // later, by ɹ -- dropping ː/ZWJ as no-ops above and mapping each
        // remaining symbol on its own already yields the right ARPABET
        // pair (AA, R), matching CMUdict's own two-phoneme convention.
        if let Some(sym) = single_symbol(c) {
            out.push(sym);
            i += 1;
            continue;
        }

        eprintln!("warning: unmapped IPA symbol {:?} (U+{:04X}), skipped", c, c as u32);
        i += 1;
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Raw IPA strings below are misaki-rs's *actual* output (captured
    /// empirically from `G2P::new(Language::EnglishUS)`, not hand-guessed
    /// from its docs, which are stale in places) -- these pin the mapper
    /// against that observed behavior, independent of whether misaki-rs
    /// itself changes its lexicon between versions.
    #[test]
    fn plain_consonants_and_vowels() {
        assert_eq!(ipa_to_arpabet("kwˈɪk"), vec!["K", "W", "IH", "K"]); // quick
        assert_eq!(ipa_to_arpabet("fˈɑks"), vec!["F", "AA", "K", "S"]); // fox
        assert_eq!(ipa_to_arpabet("vˈɪʒən"), vec!["V", "IH", "ZH", "AH", "N"]); // vision
    }

    #[test]
    fn diphthongs_via_zwj() {
        assert_eq!(ipa_to_arpabet("bɹˈa\u{200d}ʊn"), vec!["B", "R", "AW", "N"]); // brown
        assert_eq!(ipa_to_arpabet("lˈe\u{200d}ɪzi"), vec!["L", "EY", "Z", "IY"]); // lazy
        assert_eq!(ipa_to_arpabet("kˈo\u{200d}ʊt"), vec!["K", "OW", "T"]); // coat
        assert_eq!(ipa_to_arpabet("bˈɔ\u{200d}ɪ"), vec!["B", "OY"]); // boy
    }

    #[test]
    fn affricates_expand_like_old_script() {
        // church -> t‍ʃˈɜːt‍ʃ, judge -> d‍ʒˈʌd‍ʒ
        assert_eq!(ipa_to_arpabet("t\u{200d}ʃˈɜːt\u{200d}ʃ"), vec!["T", "SH", "ER", "T", "SH"]);
        assert_eq!(ipa_to_arpabet("d\u{200d}ʒˈʌd\u{200d}ʒ"), vec!["D", "ZH", "AH", "D", "ZH"]);
    }

    #[test]
    fn nurse_vowel_collapses_to_er() {
        assert_eq!(ipa_to_arpabet("nˈɜːs"), vec!["N", "ER", "S"]); // nurse
        assert_eq!(ipa_to_arpabet("hɜː"), vec!["HH", "ER"]); // her
        assert_eq!(ipa_to_arpabet("hˈæmɚ"), vec!["HH", "AE", "M", "ER"]); // hammer (ɚ, no ɹ)
        assert_eq!(ipa_to_arpabet("ˌo\u{200d}ʊvɚɹ"), vec!["OW", "V", "ER"]); // over (ɚ + redundant ɹ)
    }

    #[test]
    fn other_rhotic_vowels_keep_separate_r() {
        // car -> kˈɑː‍ɹ (vowel, length mark, ZWJ, ɹ)
        assert_eq!(ipa_to_arpabet("kˈɑː\u{200d}ɹ"), vec!["K", "AA", "R"]);
        // here -> hˈɪ‍ɹ (vowel directly ZWJ-joined to ɹ, no length mark)
        assert_eq!(ipa_to_arpabet("hˈɪ\u{200d}ɹ"), vec!["HH", "IH", "R"]);
    }

    #[test]
    fn flap_maps_to_t() {
        assert_eq!(ipa_to_arpabet("bˈʌɾɚ"), vec!["B", "AH", "T", "ER"]); // butter
    }

    #[test]
    fn stress_marks_and_letter_names_ignored_or_mapped() {
        assert_eq!(ipa_to_arpabet("ˌI"), vec!["AY"]); // "I" letter-name edge case
        assert_eq!(ipa_to_arpabet("ˈO"), vec!["OW"]); // "O" letter-name edge case
    }

    #[test]
    fn end_to_end_known_words() {
        let g2p = new_g2p();
        let result = text_to_arpabet(&g2p, "car computer");
        assert_eq!(
            result,
            vec![
                ("car".to_string(), vec!["K", "AA", "R"]),
                ("computer".to_string(), vec!["K", "AH", "M", "P", "Y", "UW", "T", "ER"]),
            ]
        );
    }

    #[test]
    fn end_to_end_oov_word_gets_letter_spelled() {
        // No espeak fallback (default-features = false): out-of-vocabulary
        // words are spelled out letter by letter rather than guessed --
        // accepted behavior, this test just documents/pins it.
        let g2p = new_g2p();
        let result = text_to_arpabet(&g2p, "robotalk");
        assert_eq!(result.len(), 1);
        let (word, phonemes) = &result[0];
        assert_eq!(word, "robotalk");
        assert!(phonemes.len() > 8, "expected letter-by-letter spelling, got {:?}", phonemes);
    }
}
