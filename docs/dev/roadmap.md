# Roadmap

Last updated 2026-10-25. Status marks come from the specs and ledgers under `docs/superpowers/` and
`.superpowers/estate/` as last recorded, not from a fresh code audit. The same material, with diagrams, is in
the Claude Doc "Deluge Firmware Roadmap".

Items marked **(proposed)** are placements that no recorded plan or ruling fixes yet.

## Where things stand

- `deluge-project-param` (the model line, P0 to P3c) is merged into `next`.
- `feat/ui-integration` is the only unmerged line: 948 commits ahead of `next`, none behind.
- `next` is 3,515 commits ahead of `main`. `main` is PR-only; local merges go into `next`.
- Next action: P3d-1c step 3b, closing about 16 release blockers so the new menu can ship on device.

## Target architecture

Every effort is one move: split the monolith into components behind stable, language-neutral interfaces (a C ABI
at language seams). That unlocks per-component language choice, CLAP, fault isolation and restartability.

| Layer | Contents |
| --- | --- |
| Interface (P6) | Menu scenes bound to model addresses; views, learn, param backup follow |
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
| P3d | Host-chain flip, UI on addresses, param tables, delete MCA/GE | 1b done, 1c in progress |
| P4 | Clip player (live input is one mode) | Not started |
| P5 | Topology + reconciler: track = chain, kit = rack of lanes, clip-scoped values, one mapping table | Step 1 records done |
| P6 | Interface | Not started |

### P3d-1c execution tree

1. Song-state model completeness: done.
2. Scenes bind to schema rows: done.
3. Scenes ship on device:
   - 3a, SDRAM placement and device link with ShellMenu off: done; device flash-test pending.
   - **3b, close ~16 release blockers: next.** Shift+pad entry, DX7 menu, menu MIDI learn (CC and bend),
     multi-range editing, patch-source pads, browser audition and waveform, slicer commit, grid greyout.
   - 3c, ShellMenu on by default.
   - 3d, delete SoundEditor, menus and menu_item (~29k lines) after a device flash-test; freeze the 21
     legacy-oracle specs as golden data first.
4. Delete `ModControllableAudio` and `GlobalEffectable`, then the P3d-1d per-effect param tables.

## DSP still in the app

| Area | Where | Size (lines) | State |
| --- | --- | --- | --- |
| FM and DX7 core | `src/deluge/dsp/dx` | 2.4k | `DX7Source` wrapper exists; kernels app-side |
| Wavetable | `src/deluge/storage/wave_table` + oscillator adapter | 1.2k | Sinc kernel in dsp-next; render and sync still behind the adapter |
| Reverb | `src/deluge/dsp/reverb` | 1.4k | App-side; not in P3a to c |
| Time-stretch, sample playback | `src/deluge/dsp/timestretch`, `voice_sample` | 1.4k+ | Waits on the deferred SampleSource port; belongs with P4 |
| Pitch detection | `model/sample/pitch_scan`, `synth/source/live/live_pitch_shifter` | ~0.3k + shifter | App-side; no plan entry names it |
| Voice kernel | `src/deluge/model/voice`, `processing/sound` | 5.7k+ | Phase 4 approach decided (decouple in place, lift to `synth::Voice` last); not started |
| Oscillator tail | VA shapes, VCA and wave-scale duality | n/a | Engines done and bit-exact; unifying is audible and needs a re-baseline plus ear-check |

## Long-term ordering

Four waves across five lanes. Shipping the UI on device is the one hard gate: the model deletions cannot start
until the legacy menus are gone, because the shipping SoundEditor holds `ModControllableAudio` pointers.

| Lane | 1 Ship the UI | 2 Retire legacy | 3 Topology, clips | 4 Long term |
| --- | --- | --- | --- | --- |
| UI shell | 3b blockers, flash-test 3a, 3c flip | 3d delete legacy menus, freeze oracle specs | Kit lanes and mapping-table UI | P6 interface |
| Model | ModelStack plan, inventory of readers | Delete MCA/GE, P3d-1d, stack out of param and undo paths | P5 topology + reconciler, stack out of clip and sequencer | ModelStack deleted |
| Synth | FM/DX7 kernel, wavetable band port, reverb component (3 worktrees) | SampleSource port, pitch detection home, voice kernel a to d | Voice kernel e, f; P4 clip player; patcher replacement; audible re-baseline | L3 C ABI, CLAP and foreign engines, DX7 as engine |
| Platform, storage | Device flash-tests, USB-MIDI fix push, live-bug triage | Tier B sync callers, tier D comments, FS-lock queue wait | Allocator redesign, libdeluge hard wall | Supervisor + MMU, warm restart |
| Release, features | UI branch into `next`, nightly with both corpora | `next` to `main` PR, port #5049 follow-ups | Merge each phase, merge `main` into `next` often | Faster-than-real-time export |

### Ordering rules

1. **Ship before delete.** The new menu is flash-tested on device before the legacy one is removed.
2. **Plan before retiring the ModelStack.** Every plan touching a stack-taking path says what it adds to or
   removes from the retirement.
3. **One audible window.** Changes that alter sound (VCA and wave-scale unification, the patcher
   replacement) land together, with one golden re-baseline and one hardware ear-check.
4. **Lanes own disjoint directories** so worktrees merge cleanly. Golden baselines, the test registry and CMake
   lists are touched by one lane at a time.

### When the big replacements land

| Replacement | Wave | Needs first | State |
| --- | --- | --- | --- |
| Legacy menus (SoundEditor, menu_item) | 2 | 3b closed, 3a flash-tested, oracle specs frozen | 3a done |
| `ModControllableAudio`, `GlobalEffectable` | 2 | Legacy menus gone | Recorded dependency |
| ModelStack stage 1: params and undo by address | 2 | MCA/GE gone; `Consequence::revert`, automation recording and AutoParam projection re-keyed | `bridge::paramStackAt` bridge exists; stage placement (proposed) |
| ModelStack stage 2: clip, note-row, sequencer chain on ids | 3 | P5 topology and reconciler | (proposed) |
| ModelStack deleted | 4 | P6; the wave-1 inventory shows no readers left | (proposed) |
| Patch cable data | Done | n/a | In instrument state, per slot and clip (P2c) |
| Patch cable runtime (Patcher to a replacement mod matrix) | 3 | Voice kernel lifted; the audible window | Decided 2026-10-25: after the voice kernel lift |
| `OscType` switch to persistent per-voice engines | 3 | VCA and wave-scale unification | Engines exist; the switch remains |
| `Voice`, `Sound::render` to `synth::Voice` | 3 | Voice kernel slices a to d | Approach decided, not started |

### Worktrees

| Lane | Worktrees | Directories owned | Collision risk |
| --- | --- | --- | --- |
| UI shell | `feat/ui-integration`; the 3b blockers split into up to 3 sub-worktrees (entry and learn, DX7 and browser, slicer and grid) | `src/scenes`, `src/shell`, `gui/ui` | Scene binding tables; rebase often |
| Model | One worktree off the UI branch after 3d; plan and inventory can start now | `src/deluge-project`, model bridges | Shares Song code with every lane; merge first |
| Synth | Three at once (FM/DX7, wavetable, reverb); the voice kernel after is serial | `dsp/dx`, `storage/wave_table`, `dsp/reverb` | Golden baselines and the test registry |
| Platform, storage | Two: tier B sync callers; FS-lock and streaming | Storage, Rust BSP crates | Low; the device flash-test queue is the bottleneck |
| Release | The main checkout, merges only | n/a | Local merges go into `next`, never `main` |

The synth lane is cleared to start now (decided 2026-10-25). No synth work has begun.

Each worktree needs `DBT_TOOLCHAIN_PATH` set, not a symlinked `toolchain/`. Verify before a merge with the
nightly tier and both `DELUGE_SONG_CORPUS` and `DELUGE_LEGACY_CORPUS` set.

## Parallel lines

| Line | State | Open |
| --- | --- | --- |
| UI parity vs `main` | `tools/emu_parity.py`; D7, D14, D17 fixed | D18 (ShellMenu gaps); every remaining diff needs a fix or Kate's acceptance |
| Tier invariants | A and C done and merged | B (convert sync callers, then enforce); D (~130 "storage owner" comments) |
| SD streaming | Rust async loader on; loader table-scan dropouts fixed | 12x queue wait under load from the FS lock; hardware ear-check |
| Rust BSP, hardware | M5 code-complete | Hardware validation; flash-test async-SD and the libdeluge relocations |
| Live bugs | USB-MIDI misdecode on device (fixed locally, unpushed) | Empty-preset heap corruption; `voices_` cross-tier race; E199 preview cluster; voice-output 2-tap low-pass |
| Deferred | Allocator redesign, supervisor and MMU split, CLAP at L3 | Gap docs: libdeluge boundary spec, component catalog, CLAP integration, SVC ABI. Backlog: immediate-mode UI |

## Merge train

1. Merge `feat/ui-integration` into `next` once step 3 lands. It is zero commits behind, so no catch-up merge is
   needed first.
2. Open the `next` to `main` PR.
3. Before each merge, run the nightly tier with both corpus variables set; without them the preset and upgrade
   gates fail or go absent.
4. Port the #5049 SD-perf follow-ups from `main`.

Biggest risk: the gap to `main` grows with every `next` merge, and the device flash-tests (3a, async-SD,
relocations) are still outstanding.

## Tracking

This file is the roadmap of record. Update the status columns when a step lands, and link each phase to its spec
and ledger. Use GitHub issues only for items that ship independently, such as the live bugs above.
