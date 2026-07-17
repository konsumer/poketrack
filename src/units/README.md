# Units

## Sources

Sources generate audio. Place one at the top of an instrument chain.

### OSC

Classic waveform oscillator, 4-voice polyphonic.

| Param | Range | Notes |
|-------|-------|-------|
| WAVE | SINE / SQR / SAW / TRI / NOISE | Waveform shape |
| ATK | 1ms–2s | Envelope attack |
| DCY | 1ms–2s | Envelope decay |
| SUS | 0–1.0 | Sustain level |
| REL | 1ms–4s | Envelope release |
| DET | -12st–+12st | Detune in semitones |
| PW | 0.05–0.95 | Pulse width (SQR only) |
| VOL | 0–1.0 | Output volume |

### FM

2-operator FM synthesis, 4-voice polyphonic. One modulator feeds one carrier. Covers a huge range of timbres from the same building blocks as a DX7.

Defaults land near a DX7 electric piano tone.

| Param | Range | Notes |
|-------|-------|-------|
| RATIO | 0.25–16 (14 steps) | Modulator/carrier frequency ratio |
| DEPTH | 0–12.0 | Modulation index — higher = brighter/harsher |
| ATK | 1ms–2s | Carrier envelope attack |
| DCY | 1ms–2s | Carrier envelope decay |
| SUS | 0–1.0 | Sustain level |
| REL | 1ms–4s | Carrier envelope release |
| FDBK | 0–4.0 | Modulator self-feedback — adds buzz and noise |
| VOL | 0–1.0 | Output volume |

Classic patches:

- **E-piano** — RATIO=2, DEPTH=60, short DCY, mid SUS, FDBK=0
- **Bell** — RATIO=3 or 4, DEPTH=high, short SUS, long REL
- **Bass** — RATIO=1, DEPTH=low, tight ADSR
- **Organ** — RATIO=2, DEPTH=high, full SUS, low FDBK

### DRUM

Synthesized drum, monophonic. Pitch tracks the played note (C4 = normal).

| Param | Range | Notes |
|-------|-------|-------|
| TYPE | KICK / SNARE / HIHAT / HIHAT-O | Drum voice |
| DECAY | short–long | Amplitude decay time |
| TONE | 0–1.0 | Sine (0) to noise (1) blend |
| PUNCH | soft–punchy | Initial amplitude boost |
| VOL | 0–1.0 | Output volume |

### SF2

SF2 soundfont player. Point the data field at a `.sf2` file.

| Param | Range | Notes |
|-------|-------|-------|
| PRESET | 0–127 | GM preset number |
| BANK | 0–255 | Bank select |
| VOL | 0–1.0 | Output volume |
| PAN | L–center–R | Stereo pan |
| TRANS | -24st–+24st | Transpose in semitones |
| TUNE | -100c–+100c | Fine tune in cents |

### SFZ

SFZ soundfont player. Point the data field at a `.sfz` file.

| Param | Range | Notes |
|-------|-------|-------|
| VOL | 0–1.0 | Output volume |
| PAN | L–center–R | Stereo pan |
| TRAN | -24st–+24st | Transpose in semitones |
| TUNE | -100c–+100c | Fine tune in cents |

### GRAN

Granular synthesizer. Point the data field at a WAV file. Plays overlapping short grains scattered around a position in the file. Pitch tracks the played note.

| Param | Range | Notes |
|-------|-------|-------|
| GSIZE | 5ms–500ms | Grain duration |
| POS | 0–100% | Read position in file |
| SPRAY | none–wide | Random position scatter per grain |
| PITCH | -12st–+12st | Global transpose |
| ATK | 5%–50% | Grain attack (fraction of grain length) |
| REL | 5%–50% | Grain release (fraction of grain length) |
| DENS | sparse–dense | Grain overlap (1×–8×) |
| VOL | 0–1.0 | Output volume |

### SAMPLER

Sample player. Point the data field at a WAV/MP3/OGG/FLAC file. Pitch tracks the played note.

| Param | Range | Notes |
|-------|-------|-------|
| LOOP | Off / Fwd / PingPong / Rev | Loop mode |
| LSTART | 0–100% | Loop start point |
| LEND | 0–100% | Loop end point |
| TUNE | -12st–+12st | Pitch transpose |
| STRT | 0–100% | Playback start offset |

### CLAP

Loads a WCLAP plugin — CLAP compiled to wasm32 (see [WebCLAP](https://github.com/WebCLAP)). Point the data field at a `.wasm` file. Same sandboxed format runs on every target (desktop and web), so it can be bundled alongside a song using a relative path, same as sf2/sfz. Use the ADD row to map up to 8 plugin parameters to tracker-controllable slots.

### MIDI

Sends MIDI note and CC data to an external device or port. Use the DATA picker to select the output device, and ADD to map CC numbers to parameter slots.

| Param | Range | Notes |
|-------|-------|-------|
| CH | 0–F | MIDI channel |
| CC… | 00–7F | Up to 7 user-assigned CC values |

---

## Effects

Effects process audio from the slot(s) above them in the chain.

### DELAY

Tape-style delay with stereo spread.

| Param | Range | Notes |
|-------|-------|-------|
| TIME | 4ms–1s | Delay time |
| FEEDBACK | 0–95% | Echo repeat amount |
| MIX | dry–wet | Blend with dry signal |
| SPREAD | mono–ping-pong | L/R offset for stereo effect |

### DIST

Waveshaper distortion with tone control.

| Param | Range | Notes |
|-------|-------|-------|
| DRIVE | clean–hard clip | Distortion amount |
| TONE | dark–bright | Post-distortion low-pass cutoff |
| MIX | dry–wet | Blend with dry signal |

### REVERB

Freeverb stereo reverb.

| Param | Range | Notes |
|-------|-------|-------|
| ROOM | tight–huge | Room size |
| DAMP | bright–dark | High-frequency damping |
| WIDTH | mono–stereo | Stereo spread |
| MIX | dry–wet | Blend with dry signal |

### CHORUS

Stereo chorus with modulated delay.

| Param | Range | Notes |
|-------|-------|-------|
| RATE | 0.1Hz–5Hz | LFO speed |
| DEPTH | 0ms–5ms | Modulation depth |
| DELAY | 5ms–40ms | Base delay time |
| MIX | dry–wet | Blend with dry signal |

### FLANGER

Short modulated delay with feedback. Produces jet-sweep or metallic comb effects.

| Param | Range | Notes |
|-------|-------|-------|
| RATE | 0.05Hz–4Hz | LFO speed |
| DEPTH | 0–100% | Modulation depth |
| FDBK | none–strong | Feedback resonance |
| MIX | dry–wet | Blend with dry signal |

### PHASER

Allpass cascade phaser with LFO sweep.

| Param | Range | Notes |
|-------|-------|-------|
| RATE | 0.1Hz–6Hz | LFO speed |
| DEPTH | narrow–wide | Sweep range |
| STAGES | 2–8 | Number of allpass stages (more = denser sweep) |
| FDBK | none–strong | Resonance |
| MIX | dry–wet | Blend with dry signal |

### FILTER

Stereo state-variable filter (SVF).

| Param | Range | Notes |
|-------|-------|-------|
| MODE | LP / HP / BP / Notch | Filter type |
| CUTF | 20Hz–20kHz | Cutoff frequency (log scale) |
| RESO | Q0.5–Q8.0 | Resonance |

### BITCRUSH

Bit depth and sample rate reducer.

| Param | Range | Notes |
|-------|-------|-------|
| BITS | 1bit–16bit | Bit depth — lower = grittier |
| RATE | 1×–32× | Downsample step — lower = crunchier |

### TREMOLO

Amplitude modulation (tremolo) or auto-pan.

| Param | Range | Notes |
|-------|-------|-------|
| RATE | 0.1Hz–20Hz | LFO speed |
| DEPTH | 0–1.0 | Modulation depth |
| SHAPE | Sine / Square / Saw / Tri | LFO waveform |
| MODE | Trem / Pan / Both | Tremolo, auto-pan, or both |

### PAN/GAIN

Simple stereo pan and gain trim. 0x80 is "no change" on both params.

| Param | Range | Notes |
|-------|-------|-------|
| PAN | L–80(center)–R | Stereo pan |
| GAIN | mute–80(unity)–+6dB | Volume trim |

### COMPRESSOR

RMS compressor with makeup gain.

| Param | Range | Notes |
|-------|-------|-------|
| THRS | -60dB–0dB | Compression threshold |
| RATO | 1:1–20:1 | Compression ratio |
| ATK | 0.1ms–100ms | Attack time |
| REL | 10ms–2000ms | Release time |
| GAIN | 0dB–+24dB | Makeup gain |

### DUCKER

Sidechain envelope modulator. Watches the level of a source instrument and writes a ducked value into any param on any instrument every render block — same targeting model as LFO. Passes its own chain's audio through unchanged.

To reproduce the classic "duck this chain's volume" behavior, pair it with [PAN/GAIN](#pangain): point DUCKER's INST/PARAM at that unit's GAIN param (CNTR=80 for unity) so it pulls GAIN down while the source plays. Since it can target *any* param, it can just as easily drive a filter cutoff, a delay mix, or anything else in a chain from the sound of an instrument.

ON defaults to Off, same as LFO — leave it off while you set INST/PARAM/AMNT so browsing candidate targets doesn't stomp on unrelated params, then switch it on once configured.

| Param | Range | Notes |
|-------|-------|-------|
| SRC | 00–FF | Source instrument index to sidechain from |
| THRESH | 00–FF (0–0.5 RMS) | Sidechain level below which SRC is ignored — tune above a hit's decay tail so only its attack triggers the duck |
| REL | 10ms–500ms | Release time |
| AMNT | 0–full | Duck depth (fraction pulled down from CNTR) |
| CNTR | 00–FF | Center/unduck value for the target param (default 80) |
| INV | 0 / 1 | Invert: duck when source is *silent* instead |
| INST | 00–FF | Target instrument index (defaults to current) |
| PARAM | 00–FF | Target param (global index across chain slots) |
| ON | Off / On | Enable/disable |

### LFO

Modulates a parameter on any instrument every render block.

| Param | Range | Notes |
|-------|-------|-------|
| RATE | 0.1Hz–20Hz | LFO speed |
| SHPE | Sine / Square / Saw / Tri | LFO waveform |
| INST | 00–FF | Target instrument index (defaults to current) |
| PARAM | 00–FF | Target param (global index across chain slots) |
| CNTR | 00–FF | Center value for modulation (default 80) |
| DPTH | 00–FF | Modulation depth added/subtracted from center |
| ON | Off / On | Enable/disable |
