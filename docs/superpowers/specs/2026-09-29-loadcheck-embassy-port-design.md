# Retiring `audio-loadcheck`

Status: done 2026-09-29 (load_policy 4866e9f04, audio_load_ab 1abeeb433, retirement after them). Part 3 of retiring the C host (`src/bsp/host`); parts 1 and 2 are on
`chore/retire-c-host` (5b26d0762, 0712ba0c5).

## 1. Why

`audio-loadcheck` is the last test on the C host BSP: a pr-tier gate running `sim/build/deluge_loadcheck` with no
arguments. Its six checks run from `main()` without booting the app, against a scriptable stub in place of the
async loader, with `/tmp` directories served as the card. Once what is worth keeping runs on the Embassy host, the
rest of the C host goes.

## 2. What is kept, and where

Most of the gate is the async collector check, which scripts the stub into every state a loader could report. On
the Embassy host the real loader produces the states that matter, and `audio_load_ab`, `sample_claim`,
`preset_load` and `sample_load_tree` cover them. What nothing else covers moves; the rest is dropped.

| Check | Fate |
|---|---|
| `checkProposeLoadCandidates`: candidate order and count, 4 cases, pure | **kept**, in `load_policy` |
| `checkAudioLoadAbiAbandon`: abandoning a build publishes nothing, refused geometry, 20,000 begin/abandon rounds release their chunks | **kept**, in `load_policy`, before anything else loads |
| `checkLoadFileDefaultStaysSynchronous`: `loadFile` and `getAudioFileFromFilename` default to `Blocking` | **kept**, in `load_policy`, against the real loader: a default call loads at once, an explicit `AsyncOrBlocking` one first reports `LOAD_IN_PROGRESS` |
| `checkPathAdmissionCorpus`: the C++ admission rule against the shared corpus | **kept**, in `load_policy` |
| `checkAsyncAlternateDirOrder`: a whole load resolves the same sync and async | **covered by `audio_load_ab`**, which gains its two missing cases: garbage at the regular path with a good copy in ALT (a parse verdict must not trigger the alternate retry), and a regular-path hit that must not promote the alternate-dir status |
| `checkAsyncCollector`: ~15 scripted collector sub-cases | **dropped**. Lost: scripted coverage of rare branches (loader inactive, which only the retiring RZA1 BSP runs; a refused request; handle-table-full reclaim; the non-UTF-8 alternate-dir retry, which the Embassy card cannot hold). The Rust half of the full-table behaviour keeps its unit tests. |

`load_policy` is a golden_vt_render scenario on its own memory card, a pr-tier gate like the one it replaces (the
pr tier already builds golden_vt_render for `golden-synth`).

## 3. Then

- Delete `src/bsp/host`, `sim/build_loadcheck.sh`, the `audio-loadcheck` entry, `tests/unit/host_passthrough`
  (it specifies the C host's POSIX passthrough), and the executable targets and `deluge_rust_sim` in
  `sim/CMakeLists.txt`, which then builds only the host-app libraries.
- `sim/CMakeLists.txt`'s `-m32` default existed for the C host: make 64-bit the only configuration and drop
  `DELUGE_SIM_X64` from its callers.
- Rewrite `docs/dev/host_sim_macos.md` around building the host-app libraries.
