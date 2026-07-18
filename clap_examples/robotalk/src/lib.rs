//! robotalk: a playable text-to-speech instrument. Each note selects a
//! phoneme (see phonemes.rs) rather than a pitch. Three consumers share
//! this crate: the WCLAP plugin (plugin.rs, wasm32-wasip1 only), the
//! native `robotalk` CLI (src/main.rs), and the browser build (web.rs,
//! for webroot/tts/) -- the latter two turn English text into a sequence
//! of notes via g2p.rs + pattern.rs, the plugin just plays them.

pub mod phonemes;

// clack is a guest-side CLAP binding meant for the wasm32-wasip1 plugin
// target -- native/browser builds have no host to talk to, so params.rs
// and voice.rs (which only exist to serve plugin.rs) are skipped there.
#[cfg(target_os = "wasi")]
mod params;
#[cfg(target_os = "wasi")]
mod plugin;
#[cfg(target_os = "wasi")]
mod voice;

// misaki-rs's lexicon data has no reason to be linked into the WCLAP
// plugin binary -- g2p.rs and pattern.rs are for the CLI/browser only.
#[cfg(not(target_os = "wasi"))]
pub mod g2p;
#[cfg(not(target_os = "wasi"))]
pub mod pattern;

#[cfg(all(target_arch = "wasm32", not(target_os = "wasi")))]
mod web;
