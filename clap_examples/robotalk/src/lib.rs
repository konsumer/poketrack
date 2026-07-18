//! robotalk: a playable text-to-speech instrument. Each note selects a
//! phoneme (see phonemes.rs) rather than a pitch -- pair with
//! text2phonemes.py at the repo root to turn English text into a
//! sequence of notes.

mod params;
mod phonemes;
mod voice;

use clack_extensions::audio_ports::*;
use clack_extensions::note_ports::*;
use clack_extensions::params::*;
use clack_plugin::events::spaces::CoreEventSpace;
use clack_plugin::prelude::*;
use params::RobotalkParams;
use std::ffi::CStr;
use voice::Voice;

const NUM_VOICES: usize = 8;

// Note: wclap-bridge (the native host) requires the guest module to
// export `malloc`, to allocate guest memory across the host/guest
// boundary (e.g. for passing strings). wasi-libc already defines malloc
// internally (Rust's global allocator uses it on this target) -- it just
// isn't exported by default. Rather than defining a second, conflicting
// malloc here, the existing one is exported via a linker flag in
// .cargo/config.toml (`--export=malloc`).

pub struct RobotalkPlugin;

impl Plugin for RobotalkPlugin {
    type AudioProcessor<'a> = RobotalkAudioProcessor<'a>;
    type Shared<'a> = RobotalkShared;
    type MainThread<'a> = RobotalkMainThread<'a>;

    fn declare_extensions(builder: &mut PluginExtensions<Self>, _shared: Option<&RobotalkShared>) {
        builder
            .register::<PluginAudioPorts>()
            .register::<PluginNotePorts>()
            .register::<PluginParams>();
    }
}

impl DefaultPluginFactory for RobotalkPlugin {
    fn get_descriptor() -> PluginDescriptor {
        use clack_plugin::plugin::features::*;

        PluginDescriptor::new("com.poketrack.clap.robotalk", "Robotalk (Rust)")
            .with_features([SYNTHESIZER, INSTRUMENT])
    }

    fn new_shared(_host: HostSharedHandle) -> Result<RobotalkShared, PluginError> {
        Ok(RobotalkShared {
            params: RobotalkParams::new(),
        })
    }

    fn new_main_thread<'a>(
        _host: HostMainThreadHandle<'a>,
        shared: &'a RobotalkShared,
    ) -> Result<RobotalkMainThread<'a>, PluginError> {
        Ok(RobotalkMainThread { shared })
    }
}

pub struct RobotalkShared {
    params: RobotalkParams,
}

impl PluginShared<'_> for RobotalkShared {}

pub struct RobotalkMainThread<'a> {
    shared: &'a RobotalkShared,
}

impl<'a> PluginMainThread<'a, RobotalkShared> for RobotalkMainThread<'a> {}

pub struct RobotalkAudioProcessor<'a> {
    voices: [Voice; NUM_VOICES],
    sample_rate: f32,
    shared: &'a RobotalkShared,
}

impl<'a> PluginAudioProcessor<'a, RobotalkShared, RobotalkMainThread<'a>> for RobotalkAudioProcessor<'a> {
    fn activate(
        _host: HostAudioProcessorHandle<'a>,
        _main_thread: &mut RobotalkMainThread,
        shared: &'a RobotalkShared,
        audio_config: PluginAudioConfiguration,
    ) -> Result<Self, PluginError> {
        Ok(Self {
            voices: std::array::from_fn(|_| Voice::new()),
            sample_rate: audio_config.sample_rate as f32,
            shared,
        })
    }

    fn process(
        &mut self,
        _process: Process,
        mut audio: Audio,
        events: Events,
    ) -> Result<ProcessStatus, PluginError> {
        let mut output_port = audio
            .output_port(0)
            .ok_or(PluginError::Message("No output port found"))?;
        let mut output_channels = output_port
            .channels()?
            .into_f32()
            .ok_or(PluginError::Message("Expected f32 output"))?;

        let params = &self.shared.params;
        let pitch_hz = 70.0 + params.pitch() * 150.0;
        let resonance = 4.0 + params.res() * 16.0;
        let decay_param = params.decay();
        let breath = params.breath();
        let volume = params.volume();
        let rate_mult = 0.5 + params.rate() * 1.5;

        {
            let ch0 = output_channels
                .channel_mut(0)
                .ok_or(PluginError::Message("Expected at least one channel"))?;
            ch0.fill(0.0);

            for event_batch in events.input.batch() {
                for event in event_batch.events() {
                    self.handle_event(event, pitch_hz, resonance, decay_param, rate_mult);
                }

                let sub = &mut ch0[event_batch.sample_bounds()];
                for sample in sub.iter_mut() {
                    let mut mix = 0.0f32;
                    for voice in self.voices.iter_mut() {
                        if voice.active {
                            mix += voice.step(breath, pitch_hz, resonance, rate_mult);
                        }
                    }
                    *sample = mix * volume;
                }
            }
        }

        // Mono-generated signal, copied to every output channel (stereo).
        if output_channels.channel_count() > 1 {
            let (first, rest) = output_channels.split_at_mut(1);
            let first_channel = first.channel(0).unwrap();
            for other_channel in rest {
                other_channel.copy_from_slice(first_channel)
            }
        }

        let any_active = self.voices.iter().any(|v| v.active);
        Ok(if any_active { ProcessStatus::Continue } else { ProcessStatus::Sleep })
    }
}

impl RobotalkAudioProcessor<'_> {
    fn handle_event(
        &mut self,
        event: &UnknownEvent,
        pitch_hz: f32,
        resonance: f32,
        decay_param: f32,
        rate_mult: f32,
    ) {
        match event.as_core_event() {
            Some(CoreEventSpace::NoteOn(e)) => {
                let key = e.key().as_specific().copied().map(|k| k as i32).unwrap_or(60);
                let note_id = e.note_id().as_specific().copied().map(|n| n as i32).unwrap_or(-1);
                let velocity = e.velocity() as f32;

                let mut victim = 0usize;
                let mut found_free = false;
                for (i, voice) in self.voices.iter().enumerate() {
                    if !voice.active {
                        victim = i;
                        found_free = true;
                        break;
                    }
                }
                if !found_free {
                    victim = self.voices.len() - 1;
                }
                self.voices[victim].start(
                    key,
                    note_id,
                    velocity,
                    self.sample_rate,
                    pitch_hz,
                    resonance,
                    decay_param,
                    rate_mult,
                );
            }
            Some(CoreEventSpace::NoteOff(e)) => {
                let key = e.key().as_specific().copied().map(|k| k as i32).unwrap_or(60);
                let note_id = e.note_id().as_specific().copied().map(|n| n as i32).unwrap_or(-1);
                for voice in self.voices.iter_mut() {
                    if voice.active && voice.matches(key, note_id) {
                        voice.release();
                        break;
                    }
                }
            }
            Some(CoreEventSpace::ParamValue(e)) => self.shared.params.handle_event(e),
            _ => {}
        }
    }
}

impl PluginAudioPortsImpl for RobotalkMainThread<'_> {
    fn count(&mut self, is_input: bool) -> u32 {
        if is_input { 0 } else { 1 }
    }

    fn get(&mut self, index: u32, is_input: bool, writer: &mut AudioPortInfoWriter) {
        if !is_input && index == 0 {
            writer.set(&AudioPortInfo {
                id: ClapId::new(0),
                name: b"main",
                channel_count: 2,
                flags: AudioPortFlags::IS_MAIN,
                port_type: Some(AudioPortType::STEREO),
                in_place_pair: None,
            });
        }
    }
}

impl PluginNotePortsImpl for RobotalkMainThread<'_> {
    fn count(&mut self, is_input: bool) -> u32 {
        if is_input { 1 } else { 0 }
    }

    fn get(&mut self, index: u32, is_input: bool, writer: &mut NotePortInfoWriter) {
        if is_input && index == 0 {
            writer.set(&NotePortInfo {
                id: ClapId::new(0),
                name: b"notes",
                preferred_dialect: Some(NoteDialect::Clap),
                supported_dialects: NoteDialects::CLAP,
            });
        }
    }
}

impl PluginMainThreadParams for RobotalkMainThread<'_> {
    fn count(&mut self) -> u32 {
        params::params_count()
    }

    fn get_info(&mut self, param_index: u32, info: &mut ParamInfoWriter) {
        params::params_get_info(param_index, info)
    }

    fn get_value(&mut self, param_id: ClapId) -> Option<f64> {
        params::params_get_value(&self.shared.params, param_id)
    }

    fn value_to_text(&mut self, param_id: ClapId, value: f64, writer: &mut ParamDisplayWriter) -> std::fmt::Result {
        params::params_value_to_text(param_id, value, writer)
    }

    fn text_to_value(&mut self, param_id: ClapId, text: &CStr) -> Option<f64> {
        params::params_text_to_value(param_id, text)
    }

    fn flush(&mut self, input_parameter_changes: &InputEvents, _output_parameter_changes: &mut OutputEvents) {
        for event in input_parameter_changes {
            if let Some(CoreEventSpace::ParamValue(e)) = event.as_core_event() {
                self.shared.params.handle_event(e);
            }
        }
    }
}

impl PluginAudioProcessorParams for RobotalkAudioProcessor<'_> {
    fn flush(&mut self, input_parameter_changes: &InputEvents, _output_parameter_changes: &mut OutputEvents) {
        for event in input_parameter_changes {
            if let Some(CoreEventSpace::ParamValue(e)) = event.as_core_event() {
                self.shared.params.handle_event(e);
            }
        }
    }
}

clack_export_entry!(SinglePluginEntry<RobotalkPlugin>);
