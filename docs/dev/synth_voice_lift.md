# The voice kernel: `synth::Voice`

`deluge::synth::Voice` (`src/synth/voice/voice.h`) plays one voice of a synth sound — its sources in each unison
part, its envelopes, local LFOs, modulation, filters and output stage — from what it is handed. It names nothing of
the app: it lives in the synth library, builds with the library's flags and warnings, and `scripts/check_layers.py`'s transitive `synth-boundary` rule
holds it to the library's boundary. A host spec plays it with nothing of the app linked.

The app holds each voice as a `SoundVoice` (`src/deluge/model/voice/voice_part.h`): the instrument's slot
(`instruments::classic::VoiceSlot`) over one kernel and the app's part — the firmware ports its sources play through and the
playback guides of the files its note plays. It has no back-reference: its `Sound` builds what the kernel reads from
the sound and hands it over (`src/deluge/processing/sound/sound_voices.cpp`). The sound's `instruments::classic::VoicePool`
(`src/instruments/classic/voice_pool.h`, held by its `DelugeSynth::Instance`) is the only code that edits its voice
list: it runs note on and off, the render loop and the culls, drawing on the app through `SoundVoiceHost`, which
implements the seam in `src/synth/voice/voice_host.h`.

## What the kernel is handed

| When | Input | Built by |
|---|---|---|
| Each block, per sound | `BlockValues` (`synth/voice/block_values.h`): the filter slots and route, CPU direness, the synced LFO rates, the sound's MPE values, the osc sync and the slot settings the runtime holds | `DelugeSynth::begin_block`, once for all its voices, from the engine's `synth::HostBlock` and the `instruments::classic::SoundBlockFacts` `Sound::render` gathers |
| Each block, per sound | `MatrixBlock` (`synth/matrix/matrix_block.h`): the compiled matrix program, preset values and cable depths the matrix evaluator reads (see `synth_mod_matrix.md`) | `Sound::matrixBlock` |
| Each block, per voice | `VoiceBlock` (`voice_inputs.h`): each wavetable source's band data, whether a play-once sample may auto-release, which sources render in stereo, the sound's voice count, the voice clock | `Sound::voiceBlock` |
| Each block, per voice | `VoiceRenderArgs`: the sound's buffer and sample count, stereo and panning flags, the changed sources, the pitch adjust, and the shared scratch (spare buffers, filter scratch, osc-sync scratch) | `Sound::render`, for the pool's `render` |
| Note-on and note change | `NoteSetup`: each source's type, file pitch and DX7 patch; portamento and the previous note; the sound's MPE and global source values; which attacks are instant; which FM modulators sound; the initial-patching block. `NoteOnEvent`: the note, velocity, lateness, MPE | `note_setup_for` (`src/deluge/model/voice/voice_setup.h`) |
| Note-off | `ReleaseInfo`: which envelopes sustain, whether note tails are allowed, the sources' types, the voice count and priority | `release_info_for` |
| Always | `SoundConfig` (`synth/voice/sound_config.h`): the voicing (priority, polyphony, transpose, unison), the engine settings (synth mode, retrigger phases, FM's modulators), the LFOs' states, each source's playback | `DelugeSynth::derive`, from the sound's record, held by its `DelugeSynth::Instance` (`Sound::deriveConfig`) |
| Bound once | The ports behind each unison part's sample, DX7 and input sources | `Sound::bindVoice` |
| Each call | The synth's one noise stream: note-on, phase randomizing and each render (`VoiceRenderArgs::noise`) | `Sound` (`sound_voices.cpp`, `Sound::render`) |

The voice clock (`VoiceClock`, the engine's `nextVoiceState`) stamps the amplitude envelope's stage changes, which
the cull order ranks by. The kernel keeps the stamp and computes its rating, `priority_rating(priority, voice count)`,
itself: the sample reader reads the rating partway through a note-on and a render.

## What stays in the app

- **`VoicePart`**: the ports (`UnisonPartPorts`: the `VoiceSample` reader and pool behind `SamplePlaybackPort`, the
  pooled DX7 voice, the live input) and the playback guides. The slot beside it holds the note's order, the
  first-render spread (`just_created`) and whether it is unassigned.
- **`voice_setup.cpp`**: resolving each note's sample and wavetable range into the guides (with a time-stretched
  sample's sync and a sample's bounds), FM's modulator levels, porta, the initial-patching block and the note-off's
  sustain test.
- **`Sound`**: gathering the block's facts and the render arguments, the pool's host (`SoundVoiceHost`), the zone
  change the sample marker editor makes (`Sound::voiceSampleZoneChanged`) and a voice's lifecycle steps
  (`sound_voices.cpp`). The pool takes a note-off a voice ignored once note tails are allowed again, steals and culls
  its voices; `AudioEngine` keeps the cull policy (the voice budget) and reaches voices only through each sound's cull
  queries.

## Tests

- **The golden masters** (`golden-synth`, `golden-synth-arm`): bit-exact through the lift. `voice/` cases pin the
  voice's own paths — portamento on a note change, the Nyquist cutoff, free-running phases kept on a re-trigger, a
  play-once sample's auto-release and its read-rate scaling, loop points released at the note-off, the sample cache's
  pitch-modulation test, and a sample zone moved mid-note.
- **`tests/unit/dsp/synth_voice_spec.cpp`** plays VA, FM and sample notes through fake ports
  (`tests/unit/dsp/fake_voice_ports.h`): a release to silence, a play-once sample releasing before its end, a fast release,
  a ring-mod note refused for an empty source, and the cull order by voice count. It links only the synth library,
  the DSP library, the param tables and the noise stream.

The cull order's voice-count term reorders voices only across sounds, which no host gate culls; the spec pins it on
the kernel.

## What is next

The engines and sources below the kernel are `synth_l1_engine_architecture.md`. Next is Track A step 4 part 2
(`roadmap.md`, its own spec): an engine registry in place of the note-on `OscType` dispatch, and DX7 as a
sound-level engine.
