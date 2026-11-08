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
| `Kit` | a `Track` whose instrument is a rack of lanes | P5 | its channel is already a `TrackChannel` |
| `Instrument`, `MelodicInstrument` | the instrument device | P5 | |
| `Drum`, `SoundDrum` | a kit lane, a full nested chain | P5 | |
| `NonAudioInstrument`, `MIDIInstrument`, `CVInstrument` | MIDI-out and CV-out instrument devices | P5 | |
| `GlobalEffectable`, `GlobalEffectableForClip`, `GlobalEffectableForSong` | deleted | P3d-1c step 4 | the render side lives in `TrackChannel` / `MasterChannel` |
| `ModControllableAudio` | deleted | P3d-1c step 4 | the settings binding is `EffectsBinding` |
| `ModControllable` | stays | — | the knob plumbing MIDI and CV use |
| `SongLive::global_effectable` | deleted with `GlobalEffectableForSong` | P3d-1c step 4 | the master is `SongLive::master_channel` |
| `effects::ClipChain`, `ClipChainInputs`, `ClipBlock`, `Home::CLIP`, `ChainHome::CLIP` | `TrackChain`, `TrackChainInputs`, `TrackBlock`, `Home::TRACK`, `ChainHome::TRACK` | done | a track's insert chain |
| `GlobalEffectableForClip::renderOutput` | `TrackChannel::render` | done | |
| `Kit::renderGlobalEffectableForClip`, `AudioOutput::renderGlobalEffectableForClip` | `renderContent` | done | the content a track's channel renders around |
| `getThingWithMostReverb` | `Output::reverbCandidate` | done | |

## Slot

A **slot** is a named position in a fixed layout: source A or B of a voice (`SourceSlot`), the filter set's low-pass
and high-pass positions (`LowPassSlot`, `HighPassSlot`), the instrument a track or kit row plays (`SlotAddress`,
`InstrumentSlot`), a menu position (`SlotPosition`), a one-entry mailbox (the stream channel's `AnswerSlot`). A slot
is never one of N parallel copies (that is a lane of `Poly`, `Bank` or `Linked`) and never a deduplicated read (the
matrix's source reads).
