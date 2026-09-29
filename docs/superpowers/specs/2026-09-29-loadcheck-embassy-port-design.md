# Moving `audio-loadcheck` onto the Embassy host

Status: design, for review. Part 3 of retiring the C host (`src/bsp/host`); parts 1 and 2 are on
`chore/retire-c-host` (5b26d0762, 0712ba0c5).

## 1. Why

`audio-loadcheck` is the last test on the C host BSP. It runs `sim/build/deluge_loadcheck` with no arguments, a
pr-tier gate. Once it runs on the Embassy host, the rest of the C host can go:

- `src/bsp/host` (`host_bsp.c`, `host_platform.c`, `host_audio.c`, `host_debug.cpp`, `host_loadcheck_main.cpp`,
  `host_efatfs_passthrough.cpp`);
- `sim/build_loadcheck.sh`, and the executable targets and `deluge_rust_sim` in `sim/CMakeLists.txt`, which then
  only builds the host-app libraries;
- `tests/unit/host_passthrough` (`host_passthrough_specs`, in the `unit` gate), which specifies the POSIX
  passthrough itself and goes with it.

## 2. What the gate checks today

All six checks run from `main()` without booting the app. The C host binary replaces the async loader's C ABI
(`deluge_audio_load_async_active`, `_request`, `_collect`) with a scriptable stub, and serves `/tmp` directories as
the card through `host_efatfs_passthrough.cpp`.

| # | Check | Pins | Stub | Temp card | On the Embassy host today |
|---|---|---|---|---|---|
| 1 | `checkProposeLoadCandidates` | candidate order and count, 4 cases | no | no | indirectly, by `audio_load_ab`'s outcomes; the non-`SAMPLES/` case not at all |
| 2 | `checkAudioLoadAbiAbandon` | `audio_load.h` build ownership: abandon publishes nothing, refused geometry, 20,000 begin/abandon rounds without exhausting chunks | no | no | none |
| 3 | `checkAsyncCollector` | the C++ collector in `getAudioFileFromFilename`: ~15 sub-cases (below) | heavily | yes | the real loader's happy paths, verdicts and PENDING (`audio_load_ab`, the latency knob) |
| 4 | `checkLoadFileDefaultStaysSynchronous` | `loadFile` and `getAudioFileFromFilename` default to `Blocking` | counters only | yes | none |
| 5 | `checkAsyncAlternateDirOrder` | a whole load resolves the same sync and async, including the alternate-dir slot rule | resolves against the temp card | yes | mostly, by `audio_load_ab` with the real loader |
| 6 | `checkPathAdmissionCorpus` | `admissibleAsyncLoadPath` against `path_admission_corpus.inc` | no | no | the Rust half only (`audio_loader_host.rs`) |

Check 3's sub-cases: loader inactive (async mode is `Blocking`); a wavetable never collects; UNKNOWN requests once,
with the regular path only; a refused request; PENDING does not re-request; `TOO_MANY_OPEN_STREAMS` with nothing to
reclaim, and with an unleased resident (reclaim, then exactly one re-request); a verdict surfaces as-is; READY
geometry lands on the Sample and the stream reads; inadmissible paths fall through to sync; a retry into a
non-UTF-8 alternate dir falls through; `mayReadCard=false` and `cardDisabled` never reach the loader; a Sample and
a WaveTable at one path; failed-build churn (bind failure, refused geometry, 20,000 rounds, no evictions).

## 3. Design

One new stepped scenario, `load_policy`, in golden_vt_render, on its own memory card (`fill_load_policy_card`),
replaces the binary. It runs on the booted app, as every scenario does. The gate becomes a `scenario` entry
building `golden-vt-render`, which the pr tier already builds for `golden-synth`, so it stays on the pr tier.

### A test seam in the Rust loader

Check 3 needs outcomes the real loader cannot be made to produce on demand, and exact request counts. Today the
loader's only host seam is `deluge_audio_load_set_latency_ms`. Add, in `audio_loader.rs`, gated to `host_app` like
that one, so neither the device nor `dbt sim` gains it:

- `deluge_audio_load_test_counts(uint32_t* requests, uint32_t* collects)`, and the candidate list of the last
  request;
- `deluge_audio_load_test_set_active(bool)`: the switch the C++ side's inactive branch needs. That branch is live:
  the legacy RZA1 BSP runs on the weak fallbacks, and after this port no host build does;
- `deluge_audio_load_test_script(path, status, result)`: the next collect for `path` answers `status` (UNKNOWN,
  PENDING or READY with `result`) instead of the real loader, once;
- `deluge_audio_load_test_refuse_next()`: the next request is refused.

With no test call made, the loader behaves exactly as now. These are real C symbols in the Rust loader rather
than C++ stubs, because stubs would collide with the loader's strong definitions at link time.

### Where each check goes

1. **`checkProposeLoadCandidates`:** unchanged, as a step. It is a pure function.
2. **`checkAudioLoadAbiAbandon`:** unchanged, as a step, before anything else in the scenario loads, so
   `evictions == 0` measures only its own churn.
3. **`checkAsyncCollector`:**
   - Scripted through the seam: inactive equivalence, UNKNOWN (one request, regular path only), refused request,
     PENDING, a scripted verdict, scripted geometry, the scripted `TOO_MANY_OPEN_STREAMS` cases (reclaim, then one
     re-request), refused-geometry churn.
   - Real: the READY geometry case with the stream read back, bind-failure churn (the file removed through the
     app's own FS), inadmissible paths, `mayReadCard=false`, `cardDisabled`, and the Sample/WaveTable pair. Booted,
     the WaveTable no longer needs the placement-new workaround for FFT setup.
   - **Dropped:** the non-UTF-8 alternate-dir retry. The Embassy card cannot hold a non-UTF-8 name: its FAT layer
     rejects one on every entry point, and the memory card cannot be filled with one. The admission rule that
     keeps such paths off the loader stays pinned by check 6, and the fall-through for inadmissible paths by the
     inadmissible sub-case.
4. **`checkLoadFileDefaultStaysSynchronous`:** unchanged in substance, on the card, with the seam's counters.
5. **`checkAsyncAlternateDirOrder`:** into `audio_load_ab`, which already runs this shape with the real loader,
   rather than duplicated. Add its two missing cases there: garbage at the regular path with a good copy in ALT
   (a parse verdict must not trigger the alternate retry), and a regular-path hit that must not promote the
   alternate-dir status.
6. **`checkPathAdmissionCorpus`:** unchanged, as a step.

`audio_load_ab` is nightly; the two cases it gains are then nightly too, where check 5 was pr. Either
`audio_load_ab` moves to pr (it runs in about 6 s once built, though the registry estimates ~5m) or check 5
stays whole in `load_policy`.
Recommendation: move `audio_load_ab` to pr.

### Retiring the C host afterwards

Once `load_policy` passes and has been made to fail by neutering each check's subject once:

- delete `src/bsp/host`, `sim/build_loadcheck.sh`, `tests/unit/host_passthrough`, the `audio-loadcheck` entry,
  and the executables, `deluge_rust_sim` and its cargo build in `sim/CMakeLists.txt`;
- `sim/CMakeLists.txt`'s `-m32` default existed for the C host; make 64-bit the only configuration and drop
  `DELUGE_SIM_X64` from its callers (`test_builds.py`, `preemptive_race_tsan/run.sh`, the docs);
- rewrite `docs/dev/host_sim_macos.md` around what remains: building the host-app libraries on macOS.

## 4. Steps

1. The seam in `audio_loader.rs`, with Rust tests for each call.
2. `load_policy` with checks 1, 2, 4 and 6, and its card; the registry entry, alongside `audio-loadcheck`.
3. Check 3 in `load_policy`.
4. Check 5's two cases in `audio_load_ab`, and its tier.
5. Neuter each check once and see it fail; then retire the C host as in §3.

## 5. Open questions

- **The seam:** test-only C ABI in the Rust loader, gated to `host_app`. The alternative is keeping check 3's
  scripted cases C++-side with a link-time switch, which is more machinery for the same result.
- **Dropping the non-UTF-8 retry sub-case** (§3, check 3).
- **Check 5's home and tier** (§3).
