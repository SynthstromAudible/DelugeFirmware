# The voice kernel: `synth::Voice`

`deluge::synth::Voice` (`src/synth/voice/voice.h`) plays one voice of a synth sound — its sources in each unison
part, its envelopes, local LFOs, modulation, filters and output stage — from what it is handed. It names nothing of
the app: it lives in the synth library, builds with the library's flags and warnings, and `check_synth_boundary.py`
holds it to the library's boundary. A host spec plays it with nothing of the app linked.

The app holds each voice as a `VoiceSlot` (`src/deluge/model/voice/voice_slot.h`): one kernel, the firmware ports its
sources play through, the playback guides of the files its note plays, and the `Sound` it belongs to. The slot builds
what the kernel reads from the sound and hands it over.

## What the kernel is handed

| When | Input | Built by |
|---|---|---|
| Each block, per sound | `VoiceConfig` (`voice_config.h`): synth mode, filter slots and route, unison layout, each source's type, the LFO configs, CPU direness, voice priority, portamento, … | `Sound::render`, once for all its voices |
| Each block, per sound | `MatrixBlock` (`synth/matrix/matrix_block.h`): the compiled matrix program, preset values and cable depths the matrix evaluator reads (see `synth_mod_matrix.md`) | `Sound::matrixBlock` |
| Each block, per voice | `VoiceBlock` (`voice_inputs.h`): each wavetable source's band data, whether a play-once sample may auto-release, which sources render in stereo, the sound's voice count, the voice clock | `VoiceSlot::render` |
| Each block, per voice | `VoiceRenderArgs`: the sound's buffer and sample count, stereo and panning flags, the changed sources, the pitch adjust, and the shared scratch (spare buffers, filter scratch, osc-sync scratch) | `Sound::render` |
| Note-on and note change | `NoteSetup`: each source's type, file pitch and DX7 patch; portamento and the previous note; the sound's MPE and global source values; which attacks are instant; which FM modulators sound; the initial-patching block. `NoteOnEvent`: the note, velocity, lateness, MPE | `note_setup_for` (`src/deluge/model/voice/voice_setup.h`) |
| Note-off | `ReleaseInfo`: which envelopes sustain, whether note tails are allowed, the sources' types, the voice count and priority | `release_info_for` |
| Always | `SoundSnapshot` (`sound_snapshot.h`): the sound's hot render fields — transpose, unison, synth mode, retrigger phases, LFO configs | the `Sound`, projected from its record |
| Bound once | The ports behind each unison part's sample, DX7 and input sources | `VoiceSlot`'s constructor |
| Each call | The synth's one noise stream: note-on, phase randomizing and each render (`VoiceRenderArgs::noise`) | `VoiceSlot`, `Sound::render` |

The voice clock (`VoiceClock`, the engine's `nextVoiceState`) stamps the amplitude envelope's stage changes, which
the cull order ranks by. The kernel keeps the stamp and computes its rating, `priority_rating(priority, voice count)`,
itself: the sample reader reads the rating partway through a note-on and a render.

## What stays in the app

- **`VoiceSlot`**: the ports (`UnisonPartPorts`: the `VoiceSample` reader and pool behind `SamplePlaybackPort`, the
  pooled DX7 voice, the live input), the playback guides, the note's order, the first-render spread (`just_created`),
  unassigning, and the releases stamped by the engine clock.
- **`voice_setup.cpp`**: resolving each note's sample and wavetable range into the guides (with a time-stretched
  sample's sync and a sample's bounds), FM's modulator levels, porta, the initial-patching block and the note-off's
  sustain test.
- **`Sound`**: building the config and render arguments, taking a note-off the voice ignored once note tails are
  allowed again, culling and stealing voices, and the zone change the sample marker editor makes
  (`VoiceSlot::sample_zone_changed`).

## Tests

- **The golden masters** (`golden-synth`, `golden-synth-arm`): bit-exact through the lift. `voice/` cases pin the
  voice's own paths — portamento on a note change, the Nyquist cutoff, free-running phases kept on a re-trigger, a
  play-once sample's auto-release and its read-rate scaling, loop points released at the note-off, the sample cache's
  pitch-modulation test, and a sample zone moved mid-note.
- **`tests/dsp/synth_voice_spec.cpp`** plays VA, FM and sample notes through fake ports
  (`tests/dsp/fake_voice_ports.h`): a release to silence, a play-once sample releasing before its end, a fast release,
  a ring-mod note refused for an empty source, and the cull order by voice count. It links only the synth library,
  dsp-next, the param tables and the noise stream.

The cull order's voice-count term reorders voices only across sounds, which no host gate culls; the spec pins it on
the kernel.

## What is next

The engines and sources below the kernel are `synth_l1_engine_architecture.md`. Next is the mod matrix's runtime (M3),
whose descriptor-published IDs take the synth's vocabulary out of `definitions_cxx.hpp` (`TODO.md`).
