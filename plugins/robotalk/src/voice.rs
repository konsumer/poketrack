//! Per-note DSP. One shared engine handles all phoneme categories:
//! vowels/glides/liquids/nasals share a 3-resonator filter bank fed by a
//! voiced/noise excitation mix; fricatives reuse the same bank tuned to a
//! single spectral peak; stops bypass it entirely with a one-shot
//! closure -> burst -> voicing-onset state machine.

use crate::phonemes::{Category, PhonemeEntry, phoneme_for_key};

/// A single resonant bandpass (Chamberlin state-variable filter).
#[derive(Default, Clone, Copy)]
struct Resonator {
    low: f32,
    band: f32,
    f: f32,
    q: f32,
}

impl Resonator {
    fn set(&mut self, freq: f32, resonance: f32, sample_rate: f32) {
        if freq <= 0.0 {
            self.f = 0.0;
            self.q = 1.0;
            return;
        }
        self.f = 2.0 * (std::f32::consts::PI * freq / sample_rate).sin();
        self.q = 1.0 / resonance;
    }

    fn reset(&mut self) {
        self.low = 0.0;
        self.band = 0.0;
    }

    fn step(&mut self, input: f32) -> f32 {
        self.low += self.f * self.band;
        let high = input - self.low - self.q * self.band;
        self.band += self.f * high;
        self.band
    }
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum StopStage {
    Closure,
    Burst,
    VoicingOnset,
    Done,
}

pub struct Voice {
    pub active: bool,
    pub key: i32,
    pub note_id: i32,

    velocity: f32,
    sample_rate: f32,
    phoneme: &'static PhonemeEntry,

    // Attack -> self-terminating decay, latched (not re-checked every
    // sample -- that bug already cost a real regression once).
    env: f32,
    attacking: bool,
    attack_slew: f32,
    decay_gain: f32,

    // Formant glide progress, 0..1 over the note's held duration.
    glide_pos: f32,
    glide_step: f32,

    phase: f32,
    phase_step: f32,
    rng: u32,

    res: [Resonator; 3],

    stop_stage: StopStage,
    stop_timer: u32,
    stop_decay: f32,
}

fn lerp(a: f32, b: f32, t: f32) -> f32 {
    a + (b - a) * t
}

impl Voice {
    pub fn new() -> Self {
        Self {
            active: false,
            key: -1,
            note_id: -1,
            velocity: 0.0,
            sample_rate: 44100.0,
            phoneme: &crate::phonemes::PHONEMES[0],
            env: 0.0,
            attacking: true,
            attack_slew: 0.3,
            decay_gain: 0.999,
            glide_pos: 0.0,
            glide_step: 0.0,
            phase: 0.0,
            phase_step: 0.0,
            rng: 0x9e3779b9,
            res: [Resonator::default(); 3],
            stop_stage: StopStage::Done,
            stop_timer: 0,
            stop_decay: 0.0,
        }
    }

    pub fn matches(&self, key: i32, note_id: i32) -> bool {
        if self.note_id != -1 && note_id != -1 {
            self.note_id == note_id
        } else {
            self.key == key
        }
    }

    fn next_noise(&mut self) -> f32 {
        let mut x = self.rng;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        self.rng = x;
        (x as f32 / u32::MAX as f32) * 2.0 - 1.0
    }

    pub fn start(
        &mut self,
        key: i32,
        note_id: i32,
        velocity: f32,
        sample_rate: f32,
        pitch_hz: f32,
        resonance: f32,
        decay_param: f32,
        rate_mult: f32,
    ) {
        self.active = true;
        self.key = key;
        self.note_id = note_id;
        self.velocity = velocity;
        self.sample_rate = sample_rate;
        let phoneme = phoneme_for_key(key);
        self.phoneme = phoneme;

        self.phase = 0.0;
        self.phase_step = pitch_hz / sample_rate;

        for r in self.res.iter_mut() {
            r.reset();
        }

        match phoneme.category {
            Category::Vowel | Category::Nasal | Category::Fricative => {
                self.env = 0.0;
                self.attacking = true;
                self.attack_slew = 1.0 / (sample_rate * 0.005 + 1.0);

                let target_seconds = (0.15 + decay_param * decay_param * 2.85) / rate_mult;
                self.decay_gain =
                    0.0005f32.powf(1.0 / (target_seconds * sample_rate).max(1.0));

                self.glide_pos = 0.0;
                let glide_seconds = (0.12 / rate_mult).max(0.01);
                self.glide_step = 1.0 / (glide_seconds * sample_rate);

                for (i, r) in self.res.iter_mut().enumerate() {
                    r.set(phoneme.f_start[i], resonance, sample_rate);
                }
            }
            Category::Stop => {
                self.stop_stage = StopStage::Closure;
                let closure_ms = 30.0 / rate_mult;
                self.stop_timer = ((closure_ms * 0.001 * sample_rate) as u32).max(1);
                self.res[0].set(phoneme.burst_hz, resonance * 0.6, sample_rate);
                self.stop_decay = 1.0;
            }
        }
    }

    pub fn release(&mut self) {
        if matches!(self.phoneme.category, Category::Vowel | Category::Nasal | Category::Fricative) {
            let quick_seconds = 0.08;
            self.decay_gain = 0.0005f32.powf(1.0 / (quick_seconds * self.sample_rate).max(1.0));
        }
        // Stops always self-terminate quickly on their own; nothing to do.
    }

    fn step_sustained(&mut self, breath: f32, pitch_hz: f32, resonance: f32) -> f32 {
        if self.attacking {
            self.env += (1.0 - self.env) * self.attack_slew;
            if self.env >= 0.999 {
                self.attacking = false;
            }
        } else {
            self.env *= self.decay_gain;
            if self.env < 1e-4 {
                self.active = false;
            }
        }

        // PITCH and RES are live, not baked in at note-start -- a held
        // note should audibly respond to turning either knob, the same
        // way BREATH/VOLUME already do (that was the actual bug: this
        // used to read a value captured once at note-on and never again,
        // so those two knobs had no audible effect on a sounding note).
        self.phase_step = pitch_hz / self.sample_rate;
        if self.glide_pos < 1.0 {
            self.glide_pos = (self.glide_pos + self.glide_step).min(1.0);
        }
        let sample_rate = self.sample_rate;
        for (i, r) in self.res.iter_mut().enumerate() {
            let f = lerp(self.phoneme.f_start[i], self.phoneme.f_end[i], self.glide_pos);
            r.set(f, resonance, sample_rate);
        }

        let phase = self.phase + self.phase_step;
        self.phase = phase - phase.floor();
        let voiced = 2.0 * self.phase - 1.0;
        let noise = self.next_noise();

        let voicing_mix = if self.phoneme.category == Category::Fricative {
            // Fricatives are noise-dominant; voiced members add a low buzz.
            if self.phoneme.voiced { 0.75 } else { 1.0 }
        } else {
            breath
        };
        let exc = (voiced * (1.0 - voicing_mix) + noise * voicing_mix) * self.velocity * self.env;

        let r0 = self.res[0].step(exc) * self.phoneme.w[0];
        let r1 = self.res[1].step(exc) * self.phoneme.w[1];
        let r2 = self.res[2].step(exc) * self.phoneme.w[2];

        if self.phoneme.category == Category::Nasal {
            (r0 + r1 - r2) * 0.5
        } else {
            (r0 + r1 + r2) * 0.5
        }
    }

    fn step_stop(&mut self, rate_mult: f32) -> f32 {
        if self.stop_timer > 0 {
            self.stop_timer -= 1;
        }
        let advance = self.stop_timer == 0;

        let out = match self.stop_stage {
            StopStage::Closure => {
                if advance {
                    self.stop_stage = StopStage::Burst;
                    let burst_ms = 12.0 / rate_mult;
                    self.stop_timer = ((burst_ms * 0.001 * self.sample_rate) as u32).max(1);
                    self.stop_decay = 1.0;
                }
                0.0
            }
            StopStage::Burst => {
                // A narrow resonant bandpass driven by broadband noise only
                // passes a thin slice of it, and this SVF's gain-at-resonance
                // rises with frequency -- a flat gain overcorrects the high
                // bursts (T/D, K/G) into clipping while barely fixing the low
                // ones (P/B). Compensate with the same frequency coefficient
                // the resonator itself uses, so all six stops land at a
                // comparable, audible loudness instead of wildly different
                // ones. A real plosive burst is a strong perceptual cue, not
                // a subtle one, hence needing real make-up gain at all.
                const TARGET_GAIN: f32 = 0.73;
                let f_coef = 2.0 * (std::f32::consts::PI * self.phoneme.burst_hz / self.sample_rate).sin();
                let burst_gain = TARGET_GAIN / f_coef.max(0.01);
                let noise = self.next_noise();
                let burst = self.res[0].step(noise * self.velocity * self.stop_decay * burst_gain);
                self.stop_decay *= 0.90;
                if advance {
                    self.stop_stage = StopStage::VoicingOnset;
                    let vot_ms = self.phoneme.vot_ms / rate_mult;
                    self.stop_timer = ((vot_ms * 0.001 * self.sample_rate) as u32).max(1);
                    self.stop_decay = if self.phoneme.voiced { 0.4 } else { 0.15 };
                }
                burst
            }
            StopStage::VoicingOnset => {
                let noise = self.next_noise();
                let out = noise * self.velocity * self.stop_decay;
                self.stop_decay *= 0.95;
                if advance {
                    self.stop_stage = StopStage::Done;
                    self.active = false;
                }
                out
            }
            StopStage::Done => {
                self.active = false;
                0.0
            }
        };
        out * 0.6
    }

    pub fn step(&mut self, breath: f32, pitch_hz: f32, resonance: f32, rate_mult: f32) -> f32 {
        match self.phoneme.category {
            Category::Stop => self.step_stop(rate_mult),
            _ => self.step_sustained(breath, pitch_hz, resonance),
        }
    }
}
