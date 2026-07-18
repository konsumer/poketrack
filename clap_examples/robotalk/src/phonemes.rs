//! The phoneme inventory: 37 ARPABET-style symbols (matching CMUdict, so
//! the companion `robotalk` CLI binary needs no separate translation
//! table -- it reuses `PHONEMES` directly), laid out as contiguous
//! MIDI-note blocks starting at `BASE_NOTE`.
//!
//! Frequency data is drawn from real published acoustic-phonetics
//! measurements, not invented: Peterson & Barney (1952) for vowel
//! formants, Jongman/Wayland/Wong (2000) for fricative spectral peaks,
//! Stevens & Blumstein (1978) for stop burst spectra, Fujimura (1962) for
//! nasal formants/antiformants, and Espy-Wilson (1992)/Dalston (1975) for
//! liquid formants.

pub const BASE_NOTE: i32 = 36;
pub const NUM_PHONEMES: usize = 37;

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Category {
    /// Monophthongs, diphthongs (glide from f_start to f_end), liquids and
    /// glides (w, y) all share this: a pulse/noise excitation through 3
    /// resonant peaks, optionally interpolating from f_start to f_end.
    Vowel,
    /// Same engine as Vowel, but the third resonator is *subtracted*
    /// (a notch) instead of added, approximating the antiformant caused
    /// by the nasal cavity side-branch.
    Nasal,
    /// Mostly-noise excitation through the resonator bank; voiced members
    /// mix in a low-frequency voicing bar.
    Fricative,
    /// Not a sustained resonance at all: a one-shot closure -> burst ->
    /// voicing-onset state machine, like a drum hit.
    Stop,
}

pub struct PhonemeEntry {
    #[allow(dead_code)] // kept for readability/debugging, not read at runtime yet
    pub symbol: &'static str,
    pub category: Category,
    pub voiced: bool,
    /// F1/F2/F3 targets in Hz at note-on.
    pub f_start: [f32; 3],
    /// F1/F2/F3 targets glided to by the end of the note (diphthongs);
    /// equal to f_start for monophthongs/liquids/nasals/fricatives.
    pub f_end: [f32; 3],
    /// Per-resonator mix weight (0 disables a slot).
    pub w: [f32; 3],
    /// Stops only: burst spectral center (Hz).
    pub burst_hz: f32,
    /// Stops only: voice onset time in ms (short = voiced, long = voiceless).
    pub vot_ms: f32,
}

const fn vowel(symbol: &'static str, f: [f32; 3]) -> PhonemeEntry {
    PhonemeEntry {
        symbol,
        category: Category::Vowel,
        voiced: true,
        f_start: f,
        f_end: f,
        w: [1.0, 0.5, 0.25],
        burst_hz: 0.0,
        vot_ms: 0.0,
    }
}

const fn glide(symbol: &'static str, f_start: [f32; 3], f_end: [f32; 3]) -> PhonemeEntry {
    PhonemeEntry {
        symbol,
        category: Category::Vowel,
        voiced: true,
        f_start,
        f_end,
        w: [1.0, 0.5, 0.25],
        burst_hz: 0.0,
        vot_ms: 0.0,
    }
}

const fn nasal(symbol: &'static str, f1: f32, f2: f32, antiformant: f32) -> PhonemeEntry {
    PhonemeEntry {
        symbol,
        category: Category::Nasal,
        voiced: true,
        f_start: [f1, f2, antiformant],
        f_end: [f1, f2, antiformant],
        w: [1.0, 0.35, 0.9],
        burst_hz: 0.0,
        vot_ms: 0.0,
    }
}

const fn fricative(symbol: &'static str, voiced: bool, center_hz: f32, weight: f32) -> PhonemeEntry {
    PhonemeEntry {
        symbol,
        category: Category::Fricative,
        voiced,
        f_start: [center_hz, 0.0, 0.0],
        f_end: [center_hz, 0.0, 0.0],
        w: [weight, 0.0, 0.0],
        burst_hz: 0.0,
        vot_ms: 0.0,
    }
}

const fn stop(symbol: &'static str, voiced: bool, burst_hz: f32, vot_ms: f32) -> PhonemeEntry {
    PhonemeEntry {
        symbol,
        category: Category::Stop,
        voiced,
        f_start: [0.0, 0.0, 0.0],
        f_end: [0.0, 0.0, 0.0],
        w: [1.0, 0.0, 0.0],
        burst_hz,
        vot_ms,
    }
}

// F1, F2, F3 (Hz), Peterson & Barney (1952) rounded averages.
const IY: [f32; 3] = [270.0, 2290.0, 3010.0];
const IH: [f32; 3] = [390.0, 1990.0, 2550.0];
const EH: [f32; 3] = [530.0, 1840.0, 2480.0];
const AE: [f32; 3] = [660.0, 1720.0, 2410.0];
const AA: [f32; 3] = [730.0, 1090.0, 2440.0];
const AH: [f32; 3] = [640.0, 1190.0, 2390.0];
const ER: [f32; 3] = [490.0, 1350.0, 1690.0];
const AO: [f32; 3] = [570.0, 840.0, 2410.0];
const UH: [f32; 3] = [440.0, 1020.0, 2240.0];
const UW: [f32; 3] = [300.0, 870.0, 2240.0];

pub static PHONEMES: [PhonemeEntry; NUM_PHONEMES] = [
    // --- vowels (10 monophthongs + 5 diphthongs) ---
    vowel("IY", IY),
    vowel("IH", IH),
    vowel("EH", EH),
    vowel("AE", AE),
    vowel("AA", AA),
    vowel("AH", AH),
    vowel("ER", ER),
    vowel("AO", AO),
    vowel("UH", UH),
    vowel("UW", UW),
    glide("EY", EH, IY),
    glide("AY", AA, IY),
    glide("AW", AA, UW),
    glide("OY", AO, IY),
    glide("OW", AO, UW),
    // --- liquids / glides ---
    vowel("L", [350.0, 1500.0, 2700.0]),
    vowel("R", [350.0, 1100.0, 1600.0]), // low F3 is American /r/'s hallmark
    vowel("W", UW),                      // glide toward /u/
    vowel("Y", IY),                      // glide toward /i/
    // --- nasals: strong low F1, weak F2, place-dependent antiformant ---
    nasal("M", 275.0, 1100.0, 1000.0),
    nasal("N", 275.0, 1000.0, 1800.0),
    nasal("NG", 275.0, 1100.0, 3300.0),
    // --- fricatives: Jongman, Wayland & Wong (2000) spectral peaks ---
    fricative("S", false, 4500.0, 1.0),
    fricative("Z", true, 4500.0, 1.0),
    fricative("SH", false, 3000.0, 0.9),
    fricative("ZH", true, 3000.0, 0.9),
    fricative("F", false, 3500.0, 0.35),
    fricative("V", true, 3500.0, 0.35),
    fricative("TH", false, 3000.0, 0.25),
    fricative("DH", true, 3000.0, 0.25),
    fricative("HH", false, 1500.0, 0.6), // aspiration shaped loosely, no fixed peak
    // --- stops: Stevens & Blumstein (1978) burst place, Lisker & Abramson (1964) VOT ---
    stop("P", false, 700.0, 70.0),
    stop("T", false, 4500.0, 80.0),
    stop("K", false, 2000.0, 80.0),
    stop("B", true, 700.0, 5.0),
    stop("D", true, 4500.0, 5.0),
    stop("G", true, 2000.0, 20.0),
];

pub fn phoneme_for_key(key: i32) -> &'static PhonemeEntry {
    let idx = (key - BASE_NOTE).rem_euclid(NUM_PHONEMES as i32) as usize;
    &PHONEMES[idx]
}
