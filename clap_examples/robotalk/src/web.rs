//! wasm-bindgen bindings for the browser build (webroot/tts/). Exposes
//! the same text -> phonemes -> `.rptp` pipeline as the native CLI
//! (main.rs), minus file I/O -- JS handles saving the returned bytes.

use crate::{g2p, pattern};
use std::cell::RefCell;
use wasm_bindgen::prelude::*;

thread_local! {
    // misaki-rs's G2P::new() parses embedded lexicon/tagger data -- too
    // slow to redo on every conversion, so build it once per page load.
    static ENGINE: RefCell<Option<misaki_rs::G2P>> = RefCell::new(None);
}

fn with_engine<R>(f: impl FnOnce(&misaki_rs::G2P) -> R) -> R {
    ENGINE.with(|cell| {
        let mut slot = cell.borrow_mut();
        let engine = slot.get_or_insert_with(g2p::new_g2p);
        f(engine)
    })
}

/// Preview each word's ARPABET phonemes, one `word    PH ON EM ES` line
/// per word -- same output as `robotalk phonemes` on the CLI.
#[wasm_bindgen]
pub fn phonemes(text: &str) -> String {
    with_engine(|engine| {
        g2p::text_to_arpabet(engine, text)
            .into_iter()
            .map(|(word, phones)| format!("{:<20} {}", word, phones.join(" ")))
            .collect::<Vec<_>>()
            .join("\n")
    })
}

/// Convert text (or a raw phoneme list, if `raw_phonemes`) to `.rptp`
/// pattern bytes -- same pipeline as `robotalk pattern` on the CLI, minus
/// the file write.
#[wasm_bindgen]
pub fn pattern(
    text: &str,
    raw_phonemes: bool,
    instrument: u8,
    steps_per_phoneme: usize,
    gap_steps: usize,
) -> Result<Vec<u8>, JsValue> {
    let words_and_phonemes: Vec<(String, Vec<&str>)> = if raw_phonemes {
        vec![(String::new(), text.split_whitespace().collect())]
    } else {
        with_engine(|engine| g2p::text_to_arpabet(engine, text))
    };

    let steps = pattern::build_steps(&words_and_phonemes, steps_per_phoneme, gap_steps, pattern::DEFAULT_VELOCITY);
    if steps.is_empty() {
        return Err(JsValue::from_str(&format!("no phonemes produced for {:?}", text)));
    }
    Ok(pattern::rptp_bytes(&steps, instrument))
}
