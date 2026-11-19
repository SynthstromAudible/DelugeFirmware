# Naming map

A class is renamed when it becomes the thing its new name says. This map lists every pending rename, the name it
takes, and the step that makes the new name true. New code takes the target names. Save-file element names (`sound`,
`kit`, `audioTrack` and the rest) are file format and do not change.

"Channel" is a node of the signal graph: a track's strip, the master, and later returns and groups. "Bus" is reserved
for routing edges and summing buffers.

| Current | Target | Becomes true at | Notes |
|---|---|---|---|
| `Output` | `Track`, plus the instrument device it holds | P5 topology | today one object is a track and its instrument |
| `SoundInstrument` | a `Track` whose instrument is the Deluge synth | P5 | |
| `Kit` | a `Track` whose instrument is a `Kit` of `KitRow`s | P5 | a kit routes each note to one row (the Drum Rack kind); its channel is already a `TrackChannel`. `Rack` stays free for parallel chains that share an input |
| `Instrument`, `MelodicInstrument` | the instrument device | P5 | |
| `Drum`, `SoundDrum` | a `KitRow`: a trigger and its own full chain | P5 | |
| `NonAudioInstrument`, `MIDIInstrument`, `CVInstrument` | MIDI-out and CV-out instrument devices | P5 | |
| `GlobalEffectable`, `GlobalEffectableForClip`, `GlobalEffectableForSong` | deleted | done | the render side lives in `TrackChannel` / `MasterChannel` |
| `ModControllableAudio` | deleted | done | the settings binding is `EffectsBinding`; an owner's effects are `model::EffectsOwner` |
| `ModControllable` | deleted | Track A 1b-ii (with ModelStack stage 1) | today only the `effectsOwner()` bridge |
| `SongLive::global_effectable` | deleted with `GlobalEffectableForSong` | done | the master is `SongLive::master_channel` |
| `effects::ClipChain`, `ClipChainInputs`, `ClipBlock`, `Home::CLIP`, `ChainHome::CLIP` | `TrackChain`, `TrackChainInputs`, `TrackBlock`, `Home::TRACK`, `ChainHome::TRACK` | done | a track's insert chain |
| `GlobalEffectableForClip::renderOutput` | `TrackChannel::render` | done | |
| `Kit::renderGlobalEffectableForClip`, `AudioOutput::renderGlobalEffectableForClip` | `renderContent` | done | the content a track's channel renders around |
| `getThingWithMostReverb` | `Output::reverbCandidate` | done | |
| `synth::SoundSnapshot` | `synth::SoundConfig` (`Voicing`, `EngineSettings`, `lfos`) | done | derived by `DelugeSynth::derive` |
| `synth::VoiceConfig` | the sound's `SoundConfig` plus `synth::BlockValues` | done | the block values are what changes each block: host facts, the transport's, what the params make of it |
| a sound render's param reads | `synth::param_view` (concept) over `ParamManagerView` | done | |
| `instrument::DelugeInstrument` | `instrument::DelugeSynth` | done | the plugin id `kDelugeInstrument` is a file word and stays |
| `PhaseIncrementFineTuner` | `dsp::fixed::FineTune` | the synth library: done; the app's sample holders: DelugeSynth state plan D | |
| `instrument::SoundState` | `DelugeSynth::State`, composed of its components' states | DelugeSynth state plan E | |
| app `Source` (`processing/source.h`) | dissolved into the state, the config and `Resources` | DelugeSynth state plan D | `synth::Source` keeps its name |
| `effects::SoundChain` | `effects::FxSection` | DelugeSynth state plan E | "chain" means a host list of devices |
| `Sound` | deleted | after Track A step 4 and P5 | it shrinks to a wrapper around `DelugeSynth`, then goes |

## Slot

A **slot** is a named position in a fixed layout: source A or B of a voice (`SourceSlot`), the filter set's low-pass
and high-pass positions (`LowPassSlot`, `HighPassSlot`), the instrument a track or kit row plays (`SlotAddress`,
`InstrumentSlot`), a menu position (`SlotPosition`), a one-entry mailbox (the stream channel's `AnswerSlot`). A slot
is never one of N parallel copies (that is a lane of `Poly`, `Bank` or `Linked`) and never a deduplicated read (the
matrix's source reads).

## Lane

In DSP code a **lane** is one element of a SIMD vector. A name may say "lane" only in that sense: the vector holding
the lanes (`detail::F32Lanes`), a mask over them (`Poly::LaneMask`), the per-lane pointers a loop steps
(`Poly::LanePointers`), the lane an index picks (`set_lane`, `get_lane`). An adaptor or a component is named for how
its copies relate (`Mono`, `Linked`, `Stereo`, `Poly`, `Bank`, `Taps`, `Copies`), never "lanes". Outside DSP, a lane is
an automation or param lane (`LaneSnapshot`, `PublishedLane`, `LaneView`); a kit's row is a `KitRow`.
