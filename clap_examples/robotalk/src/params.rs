//! Plugin parameters: PITCH, RATE, BREATH, RES, DECAY, VOLUME. Most of a
//! phoneme's character comes from which note is played (see phonemes.rs);
//! these just shape the voice globally, same as `formant`'s params.

use clack_extensions::params::*;
use clack_plugin::events::event_types::ParamValueEvent;
use clack_plugin::prelude::*;
use std::ffi::CStr;
use std::sync::atomic::{AtomicU32, Ordering};

pub const PARAM_PITCH: ClapId = ClapId::new(0);
pub const PARAM_RATE: ClapId = ClapId::new(1);
pub const PARAM_BREATH: ClapId = ClapId::new(2);
pub const PARAM_RES: ClapId = ClapId::new(3);
pub const PARAM_DECAY: ClapId = ClapId::new(4);
pub const PARAM_VOLUME: ClapId = ClapId::new(5);

const DEFAULT_PITCH: f64 = 0.35;
const DEFAULT_RATE: f64 = 0.5;
const DEFAULT_BREATH: f64 = 0.15;
const DEFAULT_RES: f64 = 0.4;
const DEFAULT_DECAY: f64 = 0.3;
const DEFAULT_VOLUME: f64 = 0.8;

struct AtomicF32(AtomicU32);

impl AtomicF32 {
    fn new(value: f32) -> Self {
        Self(AtomicU32::new(value.to_bits()))
    }
    fn load(&self) -> f32 {
        f32::from_bits(self.0.load(Ordering::Relaxed))
    }
    fn store(&self, value: f32) {
        self.0.store(value.to_bits(), Ordering::Relaxed)
    }
}

pub struct RobotalkParams {
    pitch: AtomicF32,
    rate: AtomicF32,
    breath: AtomicF32,
    res: AtomicF32,
    decay: AtomicF32,
    volume: AtomicF32,
}

impl RobotalkParams {
    pub fn new() -> Self {
        Self {
            pitch: AtomicF32::new(DEFAULT_PITCH as f32),
            rate: AtomicF32::new(DEFAULT_RATE as f32),
            breath: AtomicF32::new(DEFAULT_BREATH as f32),
            res: AtomicF32::new(DEFAULT_RES as f32),
            decay: AtomicF32::new(DEFAULT_DECAY as f32),
            volume: AtomicF32::new(DEFAULT_VOLUME as f32),
        }
    }

    pub fn pitch(&self) -> f32 {
        self.pitch.load()
    }
    pub fn rate(&self) -> f32 {
        self.rate.load()
    }
    pub fn breath(&self) -> f32 {
        self.breath.load()
    }
    pub fn res(&self) -> f32 {
        self.res.load()
    }
    pub fn decay(&self) -> f32 {
        self.decay.load()
    }
    pub fn volume(&self) -> f32 {
        self.volume.load()
    }

    pub fn handle_event(&self, event: &ParamValueEvent) {
        let v = event.value() as f32;
        let Some(id) = event.param_id() else { return };
        match id {
            PARAM_PITCH => self.pitch.store(v),
            PARAM_RATE => self.rate.store(v),
            PARAM_BREATH => self.breath.store(v),
            PARAM_RES => self.res.store(v),
            PARAM_DECAY => self.decay.store(v),
            PARAM_VOLUME => self.volume.store(v),
            _ => {}
        }
    }

    fn get_value(&self, id: ClapId) -> Option<f64> {
        Some(match id {
            PARAM_PITCH => self.pitch() as f64,
            PARAM_RATE => self.rate() as f64,
            PARAM_BREATH => self.breath() as f64,
            PARAM_RES => self.res() as f64,
            PARAM_DECAY => self.decay() as f64,
            PARAM_VOLUME => self.volume() as f64,
            _ => return None,
        })
    }
}

struct ParamSpec {
    id: ClapId,
    name: &'static [u8],
    default: f64,
}

const PARAM_SPECS: [ParamSpec; 6] = [
    ParamSpec { id: PARAM_PITCH, name: b"Pitch", default: DEFAULT_PITCH },
    ParamSpec { id: PARAM_RATE, name: b"Rate", default: DEFAULT_RATE },
    ParamSpec { id: PARAM_BREATH, name: b"Breath", default: DEFAULT_BREATH },
    ParamSpec { id: PARAM_RES, name: b"Res", default: DEFAULT_RES },
    ParamSpec { id: PARAM_DECAY, name: b"Decay", default: DEFAULT_DECAY },
    ParamSpec { id: PARAM_VOLUME, name: b"Volume", default: DEFAULT_VOLUME },
];

pub fn params_count() -> u32 {
    PARAM_SPECS.len() as u32
}

pub fn params_get_info(index: u32, info: &mut ParamInfoWriter) {
    let Some(spec) = PARAM_SPECS.get(index as usize) else {
        return;
    };
    info.set(&ParamInfo {
        id: spec.id,
        flags: ParamInfoFlags::IS_AUTOMATABLE,
        cookie: Default::default(),
        name: spec.name,
        module: b"",
        min_value: 0.0,
        max_value: 1.0,
        default_value: spec.default,
    });
}

pub fn params_get_value(params: &RobotalkParams, id: ClapId) -> Option<f64> {
    params.get_value(id)
}

pub fn params_value_to_text(id: ClapId, value: f64, writer: &mut ParamDisplayWriter) -> std::fmt::Result {
    use std::fmt::Write;
    if id == PARAM_PITCH {
        write!(writer, "{}Hz", (70.0 + value * 150.0).round() as i32)
    } else if PARAM_SPECS.iter().any(|s| s.id == id) {
        write!(writer, "{}%", (value * 100.0).round() as i32)
    } else {
        Err(std::fmt::Error)
    }
}

pub fn params_text_to_value(id: ClapId, text: &CStr) -> Option<f64> {
    let text = text.to_str().ok()?;
    if id == PARAM_PITCH {
        let text = text.strip_suffix("Hz").unwrap_or(text).trim();
        let hz: f64 = text.parse().ok()?;
        return Some(((hz - 70.0) / 150.0).clamp(0.0, 1.0));
    }
    if PARAM_SPECS.iter().any(|s| s.id == id) {
        let text = text.strip_suffix('%').unwrap_or(text).trim();
        let pct: f64 = text.parse().ok()?;
        return Some((pct / 100.0).clamp(0.0, 1.0));
    }
    None
}
