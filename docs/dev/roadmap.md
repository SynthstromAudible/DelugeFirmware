# Roadmap

Last updated 2026-11-19 (Track A step 3 plans A to D and E1 to E3 merged into `next`, the composed state last, 722430116; step 1b-i, instrument events, merged, ed5eb290a; P3d-1d / params C4 merged, 77a23d631; the R1 ratchet in place; D18 fixed; the root-view program placed in wave 3; 3d done; the synth line re-planned as Track A and Track B; the UI read interface as of 2026-11-19). Status marks come from the local specs, ledgers and estate captures as last recorded, not from a fresh code audit. The same material, with diagrams, is in
the Claude Doc "Deluge Firmware Roadmap".

Items marked **(proposed)** are placements that no recorded plan or ruling fixes yet.

## Where things stand

- The model line (P0 to P3c) and the UI line are merged into `next`; the legacy menu is deleted (3d, 06008dfab).
- `next` is 5,938 commits ahead of `main`. `main` is PR-only; local merges go into `next`.
- `ModControllableAudio` and `GlobalEffectable` are deleted (synth Track A step 1, the controls layer and
  `feat/mca-ge-deletion`): an owner's effects are a `model::EffectsOwner`.
- The scenes' direct runtime reads are held at their count by the R1 ratchet. `ShellHostUI` hosts the scenes inside
  the legacy UI stack until the root-view program (wave 3) makes the shell the root.
- Each effect param is declared once (`effects/params.h`); the host chain's table (`effects/host_chain`) and the
  instrument's effect rows are built from them (P3d-1d / params C4, merged 77a23d631).
- Instrument events (Track A step 1b-i, `feat/instrument-events`): note tails are `Voiced`'s, an expression lane's
  change reaches the clip's instrument or the row's drum by address, and a MIDI instrument sends its own CCs;
  `ModControllable` is the `effectsOwner()` bridge only.
- Track A step 3 (the Deluge synth's state) runs as five plans, A to E. A to D and E1 to E3 are merged: a sound's
  record (`instrument::SoundState`) is composed of its components' states, with a typed `Recall` for a switch back;
  one derived `Config` and one apply path replace the setters; `BlockValues` and `ParamView` replace `VoiceConfig`'s
  settings; an oscillator's runtime is its `SourceResources`. The codec, settings and rules work on the record per
  component; the file's flat layout stays inside the codec.
- Next actions: step 3 E4 (the host fields leave the state; the tests stop building flat states) and E5 (`FxSection`,
  `Lfo::State`, docs); the ModelStack
  stage 1 spec (it ends Track A step 1b-ii); the arpeggiator's params vs settings (D7) spec; the C2 spec and
  R2 (the scenes' reads behind `bind`); the float instrument's voice pool (Track B).

## Target architecture

Every effort is one move: split the monolith into components behind stable, language-neutral interfaces (a C ABI
at language seams). That unlocks per-component language choice, CLAP, fault isolation and restartability.

| Layer | Contents |
| --- | --- |
| Interface (P6) | Menu scenes bound to model addresses; views, learn, param backup follow. The UI's whole contract with the app is `UiEvent` in, reads by address through `scenes/bind`, and writes out as commands through `WriteSink` |
| Project model, engines, effects | Model owns persistent structure, writes are commands, engines play snapshots. Engines are CLAP-shaped (Deluge instrument, clip player); the synth stays C++. Effects are host chains |
| libdeluge | The stable C ABI every component crosses |
| Rust platform | Embassy scheduler, audio/storage/interaction tiers, HAL + BSP in deluge-sdk, async SD stack |
| Supervisor + MMU | Last step, planned |

## Already on next

| Date | Milestone |
| --- | --- |
| 2026-10-19 | SD streaming speed-ups: direct cluster-map reads, read-ahead and directory index, no table rescans |
| 2026-10-18 | Device streaming benchmark, fill trace, loader queue visiting only queued slots |
| 2026-10-15 | SDK ownership: typed SGI/USB0 bindings, SD slot init in Rust |
| 2026-10-13 | Domain split P0 to P3c: standalone model, Deluge instrument boundary, effect components |
| 2026-10-09 | Fault reporting on all vectors, encoder quadrature, Rust panic pattern, dynamic patch cables |
| 2026-09-30 | libdeluge ABI overhaul; GCC device path retired |
| 2026-09-29 | Legacy BSP and C host retired; Embassy emulator; synth golden masters |
| 2026-09-28 | Fibers gone; tier meter and cross-tier ownership; browser listing on the storage tier |
| 2026-08-12 | Clang/LTO device image |
| 2026-08-06 | C-FatFS deleted (Rust filesystem R0 to R4) |
| 2026-07-17 | Embassy host harness: the whole C++ app boots on the host |
| 2026-07-14 | libdeluge file I/O and stream boundary |
| 2026-07-04 | 64-bit clang host sim |
| 2026-06-24 | Resource manager |
| 2026-06-22 | Embassy scheduler |
| 2026-06-21 | Container migration (std containers, ETL strings, ResizeableArray removed) |

## Domain split (model line)

| Phase | Delivers | State |
| --- | --- | --- |
| P0 | Owned words | Done |
| P1 | Standalone model, JSON default save format | Done |
| P2 | Engine boundary: the Deluge instrument | Done |
| P3a to c | Effect components: filters, compressor, sidechain, delay, mod FX, granular, stutter | Done |
| P3d | Host-chain flip, UI on addresses, param tables, delete MCA/GE | 1b done; 1c done (MCA/GE deleted); 1d done (per-effect param tables) |
| P4 | Clip player (live input is one mode) | Not started |
| P5 | Topology + reconciler: track = chain, kit = rack of lanes, clip-scoped values, one mapping table | Step 1 records done |
| P6 | Interface | Read half begun: `scenes/bind` readers (P3d-1c step 2); see the UI read interface below |

### P3d-1c execution tree

1. Song-state model completeness: done.
2. Scenes bind to schema rows: done.
3. Scenes ship on device:
   - 3a, SDRAM placement and the device link: done; device flash-test pending.
   - 3b, close the release blockers: done. Every difference from `main` the emulator shows is fixed or accepted
     (`tests/ui-scripts/parity/DIFFERENCES.md`).
   - 3c, ShellMenu on by default: done.
   - 3d, delete the legacy menu: done. The legacy oracles were frozen to data first; the scenes menu is the only
     menu.
4. Delete `ModControllableAudio` and `GlobalEffectable`: done. The P3d-1d per-effect param tables: done.

### UI read interface

The scenes' read side of P6: everything a scene shows comes from a reader that takes an address, never from a
legacy runtime object. Writes already leave as requests through `bind::WriteSink`, and input arrives as `UiEvent`,
so once reads go the same way, the UI's contract with the app has three parts. That makes scenes testable against
a fake reader with no `Song` and keeps the UI unaffected by changes to the model's storage (P5, ModelStack). It also
keeps the option open to put the UI behind a C ABI.

**Exists.** `scenes/bind` reads the project model by address (`ModelReader`, `SlotReader`, `ChainReader`) and takes
what the model does not hold from `RuntimeFacts`. Layer rules already keep the model stack, Edits, engine pointers
and `currentSong` out of the layer.

**Gap (measured 2026-11-19, the ratchet's baseline).** 54 scene files outside `bind/` include 159 legacy runtime
headers: `model/` 71, `modulation/` 25, `io/` 21, `processing/` 20, `storage/` 15, `playback/` 7. The menu holds 28 of
those files, `services/` 8, `browser/` and `overlay/` 4 each. The common reads are the legacy `Song`, flash settings,
the audio engine, the playback handler and the MIDI device manager. `python3 scripts/check_layer_rules.py --list`
prints today's per-file counts (the `scenes-reads-outside-bind` rows).

| Stage | Wave | Delivers | Needs first |
| --- | --- | --- | --- |
| R1 | 1 | Done: the inventory of the direct reads, shared with the Model lane's ModelStack reader inventory, and the `scenes-reads-outside-bind` layer rule (`scripts/check_layer_rules.py`), whose per-file baseline in `scripts/layer_rules_baseline.txt` may only fall, so later work adds no new reads | n/a |
| R2 | 2 | Every read moved behind `bind`: model state by address, runtime state (transport, MIDI devices, card, settings) through fact sources the host fills. The ratchet goes to zero; the menu's reads go first, since their legacy readers are gone | 3d (done) |
| R3 | 3 | Readers re-keyed onto P5's ids (track = chain, kit = rack of lanes). The scenes' calls do not change, because addresses are already the API | P5 topology |
| R4 | 4 | P6 closes: `UiEvent`, the readers and `WriteSink` are the UI's whole interface. The UI stays C++; a C-ABI form is built only if a non-C++ consumer appears (the model moving to Rust, or an SDK app hosting scenes) | R2, P6 |

Stage placement is **(proposed)**. Exit for R2: the scene specs run against a fake reader, and no file in
`src/scenes` outside `bind/` includes `model/`, `modulation/`, `io/`, `processing/`, `storage/` or `playback/`.

### Root-view program

The scenes run inside the legacy UI stack. `gui::ui::ShellHostUI` is a legacy `UI` holding the `Shell`: every scene
opens over a legacy root view, and the host hands that view whatever no scene claims. The program inverts that: the
root views become scenes one at a time, and once none is left the shell is the root and `ShellHostUI` is deleted.

**Exists.** The menu, the sample, MIDI-definition and `.syx` browsers, the recorder, the marker editor, the slicer,
the rename screen and the context menu the menu opens, all hosted by `ShellHostUI`; `AudioClipScene`, a root scene
with its spec that the firmware does not push yet; `GlobalControls`' root-view arms and its `UnportedOpen` counters.

| Stage | Wave | Delivers | Needs first |
| --- | --- | --- | --- |
| Small UIs | 3 | Scenes for the audio clip view (`AudioClipScene` shipped, its not-yet-ported arms closed), the keyboard view, the performance view, the song, preset, pattern and kit-row load and save browsers, the qwerty and rename screens, the song context menus and the clip view's MIDI-definition browser | R2 |
| Big views | 3 | Scenes for the instrument clip view, the session view, the arranger and the automation view, built on P5's topology (track = chain, kit = rack of lanes) beside R3's re-keyed readers | Small UIs; P5 topology |
| Shell as root | 3 | The shell boots into the root scenes; `ShellHostUI` and the glue that forwards input to legacy root views are deleted | Big views |

Exit: the shell is root; `ShellHostUI` deleted.

## Synth line

Two instruments (decided 2026-10-31). The **q31 Deluge instrument** is frozen in sound: legacy patches load and sound
as they always did, and only accepted bug fixes change it, so its remaining work is structural and bit-exact. The
**float instrument** is new: the uniform matrix rules and float arithmetic belong to it from day one.

**Done:** dsp-next L0; wave 1 (reverb, DX7, wavetable; NE10 gone); effects as components; the L1 engines and sources;
the L2 voice lifted to `synth::Voice`; the q31 mod matrix (M3) in place of the Patcher, with cables owned by the
instrument (M1); params A, B1, B2, C1a, C1b; the float poly kernels, voice slice, and ladder with drift; the voice
budget.

### Track A: the q31 instrument to the L3 boundary

| Step | Delivers | Needs first |
| --- | --- | --- |
| 1 | The controls layer, then the `ModControllableAudio` / `GlobalEffectable` deletion: `src/controls` (one resolver for knobs, presses, mod buttons, learned MIDI and MIDI follow; owners publish CLAP remote-controls pages through `plugin-api`; board profiles), then the classes go. Branches: `controls-library`, `controls-root-views` (the root views' knobs, presses and mod buttons go through the library; the legacy knob path is gone), `controls-shape` (owners publish their pages, the library assembles them for any board, CV tracks and MIDI and gate drums have an empty layout, and the instrument's layouts are data) and `controls-learn` (learned MIDI knobs and MIDI follow reach params by address through the library, with takeover state kept by table entry; learned knobs load through the model's codec and the engine plays none) and `mca-ge-deletion` (the classes deleted; an owner's effects are a `model::EffectsOwner`, reached from a ModelStack through the `ModControllable::effectsOwner()` bridge until ModelStack stage 1) are done. P3d-1d / C4 (per-effect tables) is done: each effect param is declared once and both tables' effect rows are built from it; the host chain keeps its flat ids until effect slots (P3d-2) | 3d (done) |
| 1b-i | Instrument events: note tails are `Voiced`'s; an expression lane's change reaches the clip's instrument or the row's drum by address (`model::expressionReceiverOf`); a MIDI instrument sends its own CCs; `ModControllable` is the `effectsOwner()` bridge only, held there by the `mod-controllable-bridge-only` layer rule (done on `feat/instrument-events`) | 1 |
| 1b-ii | The ModelStack owner slot goes; the `effectsOwner()` bridge and `ModControllable` are deleted | 1b-i; ModelStack stage 1 |
| 2 | Params C2 (UI param reads by address, on the R1 inventory and ratchet: spec, conversion), C3 (one store per instance; `ParamBackup` retires), C5 (`Kind` and the enums stop being identity); the synth vocabulary leaves `definitions_cxx.hpp` and `check_synth_boundary.py` forbids `instruments/` | 1 for C3 to C5 |
| 3 | The Deluge synth's state (`DelugeSynth::State`, the state L3 serializes): one state composed of its components' states, one derived render config in place of `SoundSnapshot` and `VoiceConfig`'s settings, a per-block pack, the loaded resources, and one apply path. The effect settings ("5b") are already value types. Five plans: A (query surface), B (config and apply), C (per-block pack), D (resources) and E1 to E3 (the composed state) merged; E4 host fields and E5 names left | 1b-i |
| 4 | The instrument owns its voice pool; the app keeps the cull policy (the voice budget is its first form). The note-on `OscType` dispatch becomes an engine registry; DX7 moves from Source to Engine | 3 |
| 5 | Layout arc 2 (every library directly under `src/`), then the CLAP alignment arc (vendor the headers, `plugin-api` on CLAP's types; TODO.md) | can run beside 2 to 4 |
| 6 | Params D (PARAM_MOD, display names, CLAP's double values) and the L3 adapter: a C-ABI `process()`, the reverb send as a second output, the instrument exporting a `clap_plugin`; stutter decided (the synth carries it, or the host stutters after the compressor) | 1 to 5 |

### Track B: the float instrument

1. The voice pool: banks per sound, stage-major render, and a part-level configure (a whole-chain reconfigure costs
   about 3,400 cycles per lane per block).
2. Ladder oversampling: Kate's call (aliasing measured at −26 to −27 dB at resonance 3.5).
3. Its rule set: the uniform matrix rules parked on `window/matrix` (Tasks 3 to 5), and a float matrix evaluator.
4. The instrument itself, built against the L3 interface from the start **(proposed)**: two different instruments
   behind one boundary prove it better than one foreign engine, so it takes Plaits' place as the proof.

### Side lanes

- The SampleSource port and the P4 clip player (time-stretch, pitch detection home).
- Voice budget follow-ons: Long 19's audio clips, the audio tier's eviction reserve.
- The DSP performance items in TODO.md.

## DSP still in the app

| Area | Where | Size (lines) | State |
| --- | --- | --- | --- |
| DX7 app glue | `src/deluge/processing/dx7` | small | Kernels in `dsp-next/fixed/dx7`; the device keeps `neon_fm_kernel.s` for modern no-input and bus-input ops |
| Wavetable loading | `src/deluge/storage/wave_table` | 1.2k | Render, hard sync, FFT and band building in dsp-next; loading and the holder app-side |
| Time-stretch, sample playback | `src/deluge/dsp/timestretch`, `model/voice/voice_sample` | 1.4k+ | Waits on the SampleSource port; belongs with P4 |
| Pitch detection | `model/sample/pitch_scan`, `synth/source/live/live_pitch_shifter` | ~0.3k + shifter | App-side; no plan entry names it |
| Voice adapter | `src/deluge/model/voice` (`SoundVoice`, `voice_setup`) | n/a | The kernel is `synth::Voice` in `src/synth/voice`; note-on setup and the sample adapter stay app-side until Track A step 4 |
| Oscillator tail | VA shapes, VCA and wave-scale duality | n/a | Frozen in the q31 instrument; the unified forms belong to the float instrument |

## Long-term ordering

Four waves across five lanes. Shipping the UI on device is the one hard gate: the model deletions cannot start
until the legacy menus are gone; they are deleted, so the model deletions are unblocked.

| Lane | 1 Ship the UI | 2 Retire legacy | 3 Topology, clips | 4 Long term |
| --- | --- | --- | --- | --- |
| UI shell | Done: 3b blockers, 3c flip, R1 read ratchet; flash-test 3a | Done: 3d legacy menus deleted, oracle specs frozen; R2 reads behind `bind` | Root-view program (small UIs, the big views with P5, then the shell as root and `ShellHostUI` deleted); kit lanes and mapping-table UI; R3 readers on P5 ids | P6 interface (R4) |
| Model | ModelStack plan, inventory of readers (shared with R1) | Delete MCA/GE, P3d-1d, stack out of param and undo paths | P5 topology + reconciler, stack out of clip and sequencer | ModelStack deleted |
| Synth | Done: FM/DX7, wavetable, reverb, voice lift, M3 matrix | Track A 1 to 2 (MCA/GE and C4 done; D7, C2, C3, C5); Track B voice pool | Track A 3 to 5 (`Patch`, voice ownership, layout and CLAP arcs); SampleSource and P4 clip player; float instrument | Track A 6: L3 adapter, both instruments behind it; external `.clap` hosting |
| Platform, storage | Device flash-tests, USB-MIDI fix push, live-bug triage | Tier B sync callers, tier D comments, FS-lock queue wait | Allocator redesign, libdeluge hard wall | Supervisor + MMU, warm restart |
| Release, features | UI branch into `next`, nightly with both corpora | `next` to `main` PR, port #5049 follow-ups | Merge each phase, merge `main` into `next` often | Faster-than-real-time export |

### Ordering rules

1. **Ship before delete.** The new menu is flash-tested on device before the legacy one is removed.
2. **Plan before retiring the ModelStack.** Every plan touching a stack-taking path says what it adds to or
   removes from the retirement.
3. **The q31 instrument keeps its sound.** Only bug fixes Kate accepts change it; sound changes (uniform matrix
   rules, VCA and wave-scale unification) go to the float instrument.
4. **Lanes own disjoint directories** so worktrees merge cleanly. Golden baselines, the test registry and CMake
   lists are touched by one lane at a time.
5. **Scenes read through `bind`.** New scene code reads by address through `scenes/bind`; code a plan already
   touches moves its reads there as it goes. The R1 ratchet (`scenes-reads-outside-bind`) enforces this.

### When the big replacements land

| Replacement | Wave | Needs first | State |
| --- | --- | --- | --- |
| Legacy menus (SoundEditor, menu_item) | 2 | 3b closed, 3a flash-tested, oracle specs frozen | Deleted |
| `ModControllableAudio`, `GlobalEffectable` | 2 | Legacy menus gone | Deleted |
| ModelStack stage 1: params and undo by address | 2 | MCA/GE gone; `Consequence::revert`, automation recording and AutoParam projection re-keyed | `bridge::paramStackAt` bridge exists; stage placement (proposed) |
| ModelStack stage 2: clip, note-row, sequencer chain on ids | 3 | P5 topology and reconciler | (proposed) |
| Scenes' direct runtime reads (54 files) | 2 | 3d (done); R1 ratchet (done) | Readers exist for settings, slots and chains; (proposed) |
| `ShellHostUI`, the legacy UI hosting the shell | 3 | R2; P5 topology for the big views | The root-view program |
| ModelStack deleted | 4 | P6; the wave-1 inventory shows no readers left | (proposed) |
| Patch cable data | Done | n/a | In instrument state, per slot and clip (P2c) |
| Patch cable runtime (Patcher to a replacement mod matrix) | Done | n/a | M3: the q31 matrix program and evaluator, bit-exact (abd530faa) |
| `OscType` switch to persistent per-voice engines | 3 | Track A step 3; bit-exact, since the q31 sound is frozen **(proposed)** | `renderOsc` deleted; VA and wavetable engines persist per voice-source; the note-on `OscType` dispatch remains |
| `Voice`, `Sound::render` to `synth::Voice` | Done | n/a | Slices a to f done; the kernel is `deluge::synth::Voice`, held by `SoundVoice` |

### Worktrees

| Lane | Worktrees | Directories owned | Collision risk |
| --- | --- | --- | --- |
| UI shell | Off `next`; R2, then the root-view program | `src/scenes`, `src/shell`, `gui/ui` | Scene binding tables |
| Model | One worktree off `next`: the params C line (MCA/GE deletion done) | `src/deluge-project`, model bridges, `processing/sound` | Shares Song code with every lane; merge first |
| Synth | The float instrument (Track B), beside Track A | `src/synth`, `dsp-next/float` | Golden baselines and the test registry |
| Platform, storage | Two: tier B sync callers; FS-lock and streaming | Storage, Rust BSP crates | Low; the device flash-test queue is the bottleneck |
| Release | The main checkout, merges only | n/a | Local merges go into `next`, never `main` |

Each worktree needs `DBT_TOOLCHAIN_PATH` set, not a symlinked `toolchain/`. Verify before a merge with the
nightly tier and both `DELUGE_SONG_CORPUS` and `DELUGE_LEGACY_CORPUS` set.

## Parallel lines

| Line | State | Open |
| --- | --- | --- |
| UI parity vs `main` | `tools/emu_parity.py`; D7, D14, D17, D18 fixed | Every remaining diff needs a fix or Kate's acceptance |
| Tier invariants | A and C done and merged; cooperative cluster-loading pump removed; `audioRoutineLocked` retired (a tick hold, a re-entry guard and a tier check; 3 `races.txt` rows closed) | B (convert sync callers, then enforce); D (~130 "storage owner" comments); `changeInstrument`'s tick hold waits for M4 |
| SD streaming | Rust async loader on; loader table-scan dropouts fixed | 12x queue wait under load from the FS lock; hardware ear-check |
| Rust BSP, hardware | M5 code-complete | Hardware validation; flash-test async-SD and the libdeluge relocations |
| Live bugs | USB-MIDI misdecode on device (fixed locally, unpushed) | Empty-preset heap corruption; `voices_` cross-tier race; E199 preview cluster; voice-output 2-tap low-pass |
| Deferred | Allocator redesign, supervisor and MMU split | Gap docs: libdeluge boundary spec, component catalog, CLAP integration, SVC ABI. Backlog: immediate-mode UI |

## Merge train

1. The UI line is in `next` (3d merged 2026-11-12).
2. Open the `next` to `main` PR.
3. Before each merge, run the nightly tier with both corpus variables set; without them the preset and upgrade
   gates fail or go absent.
4. Port the #5049 SD-perf follow-ups from `main`.
5. Each merge of `main` into `next` writes the merged `main` commit to `scripts/main_citation_ref` and runs
   `python3 scripts/check_doc_hygiene.py --strict`; an `origin/main:` citation whose file or symbol that commit lacks
   is rewritten in the merge.

Biggest risk: the gap to `main` grows with every `next` merge, and the device flash-tests (3a, async-SD,
relocations) are still outstanding.

## Tracking

This file is the roadmap of record. Update the status columns when a step lands, and link each phase to its spec
and ledger. Use GitHub issues only for items that ship independently, such as the live bugs above.
