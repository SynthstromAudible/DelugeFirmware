# R5a Phase 2 — what the I/O-class collapse proves, and what it owes

Measured on branch `feat/r5a-phase2-collapse`, off `feat/r5a-stream-exec` (Phase 1).

Companion to `r5a_phase0_baseline.md`, which is Phase-0-scoped and explicitly holds no margin
baseline. This file records the same kind of thing for Phase 2: the evidence that exists, the
evidence that cannot exist without hardware, and the exact checks the hardware pass owes.

## What changed

Two files, one shape:

- `src/bsp/rust/src/efatfs_fs.rs` (device, `#![cfg(all(target_os = "none", …))]`) — the **27**
  `!on_fiber()` rejection gates deleted, and all 30 `block_on_fiber` waits replaced with
  `sim_block::block_on`, a non-yielding spin whose transfer completes by SDHI/DMA interrupt.
- `src/bsp/rust/src/efatfs_host_shim.rs` (host mirror) — the same 30 sites collapsed from
  `if on_fiber() { block_on_fiber } else { embassy_futures::block_on }` to the identical single
  spin, so the host gates run the shape the device runs.

Left alone deliberately: `sd.rs`'s 4 block-transfer sites (below the FS layer, dispatch already
correct) and `streaming_loader.rs`'s 2 progress-class waits (`WaitChunkLoaded`,
`WaitQueueDrained` — R5b's inheritance).

## The chain the two bug fixes rest on

The gates were device-only, so both bugs are device-only. This is the reading argument; the
hardware pass is the proof.

**Off-owner save.** `performSave` runs from plain UI handlers (`save_ui.cpp:83` and the
per-type `performSave` overrides) with no `Owner::run` under `gui/ui/save/`. Writes reach
`StorageManager::createJsonFile` → `deluge::io::File::open` (`storage_manager.cpp:109`) →
`deluge_efatfs_file_open`. That entry point previously began
`if !crate::fiber::on_fiber() { return DELUGE_ERR_BUSY; }`, so the save failed at its first
write. The overwrite prompt never appearing was a symptom of the same cause, not a second bug.
Gate count in that file is now **0** (`grep -c 'if !crate::fiber::on_fiber()' → 0`).

**Boot settings silently lost.** `deluge.cpp:741` calls
`runtimeFeatureSettings.readSettingsFromFile()`, which goes through
`StorageManager::fileExists` (`runtime_feature_settings.cpp:191`) → `deluge::io::File::open` →
`deluge_efatfs_file_open`. That call sits inside `deluge_boot`, which `deluge_app_init` invokes
**synchronously before the worker-fiber pump exists** — so it was off-fiber by construction and
was refused rather than performed. This is the `boot-settings-silently-lost-on-embassy` bug,
which the R5a spec did not connect to this rung. The boot context is single-threaded, so a spin
there is safe by construction.

**A third defect, found while doing the work rather than predicted.** The three void-returning
close entry points (`deluge_efatfs_close`, `_file_close`, `_dir_close`) had no rejection gate —
nothing to reject *with*, since they return `()` — but were wrapped in a **positive**
`if on_fiber()`, so off-fiber they skipped the close entirely and leaked the handle-table slot.
Their own comment admitted it ("the slot leaks until reuse — acceptable while this stays
flag-gated with few handles"). That was near-unreachable while off-fiber callers were rejected
everywhere else; allowing off-fiber callers would have made it live, and a full handle table
drops samples silently (see the 16-handle silent sample-drop found in U4b). All three now close
unconditionally, which a spin makes safe.

## Proven on host

| Gate | Result |
|---|---|
| BSP host suite (`cargo test` in `src/bsp/rust`) | green, 5 suites |
| Lens 1 spin selftests (all four) | PASS, including the mutex-class pair and its wedging control |
| Lens 1 full sweep | reached `SELFTEST: PASS` with **no wedge** and no "exhausted from INSIDE a non-yielding spin" — the first rung where production spins go through `sim_block::block_on` at all |
| Lens 2 TSan | `open_findings=0` across 5 runs; 1 uncatalogued hit, the known pre-existing `deluge_debug_now_frames` / `audioSampleTimer` site — **no new race** |
| `golden_embassy_diff` cordae | byte-identical, `db63b128be9748ed…` |
| `golden_embassy_diff` icoustic | byte-identical, `a565391b293e9d88…` |
| `fs-differential` | green (8 + 21) |
| `deluge_app` C++ build | 0 errors |
| `./dbt test` | 39/39 |
| `cargo device --release` | links, 0 errors |

`CONTROL_A: FAIL — fast=0 slow=0` in the sweep is expected and is **not** a regression: it is the
documented structural vacuity of Lens 1's margin arm. `audio_host.rs:163-171`'s
`should_skip_render()` returns true for any off-fiber caller not on a thread named
`"deluge-audio"`, and Lens 1 is single-threaded with no such thread, so the underrun counters are
unreachable at any throughput. Lens 1's charter is wedge detection; that is what it delivered.

## NOT proven on host, and why

`efatfs_fs.rs` is `#![cfg(target_os = "none")]`. **No host build compiles a single line of the
file that actually changed.** Every host result above covers the collapsed *shape*, via the
symmetric host-shim collapse — which is precisely why that symmetry was made a first-class part
of the work rather than tidying. It does not cover the device file, and no host gate can.

So: this rung is **shape-verified and bug-unverified**. Do not describe either bug as fixed until
it has been flashed.

## Owed to hardware

1. **Off-owner save.** Save a song under a name that already exists. Expect the overwrite
   confirmation prompt to appear (it was being bypassed), and the save to complete. Note the
   prior recorded correction: this was never data loss — the destructive `unlink` is doubly
   unreachable — the failure was loud, not silent.
2. **Settings persist a power cycle.** Change a runtime feature setting, power-cycle, confirm it
   survived. This fails today because the boot read is refused.
3. **Handle exhaustion no longer leaks.** Open and close many samples/directories from
   interaction contexts (browse a large folder repeatedly) and confirm no silent sample-drop —
   the close-leak fix above.
4. **The accepted responsiveness cost, measured by ear.** Thread mode is now HELD for the
   duration of every task-context transfer instead of yielded, so PIC/OLED/UI tasks do not run
   during a save, load or directory read. Expect visible stalling. "Expected" is not "acceptable
   at any magnitude": if it feels broken rather than brief, that is worth knowing before R5b,
   whose `Owner::await` is the actual fix. Do **not** respond by reinstating the yield — that
   would undo the rung.
5. Still outstanding from Phase 1: SGI 9 priority ordering, and nested IRQ-stack depth (audio
   nesting on a streaming frame, 8 KB total).

## Two pre-existing blockers this branch works around, not fixes

- **The device image does not build from a clean checkout.** `src/bsp/rust/Cargo.toml` pins
  `deluge-bsp` to a git rev predating the pad-scroll opcodes `control.rs` calls, so
  `cargo device --release` cannot compile without the local `[patch]` added in `26bd2b98f`
  (which must also patch `rza1l-hal`, or two copies collide on `_embassy_time_now` at the LTO
  link). Needs a real resolution before this goes anywhere shared.
- **`./dbt build Debug`** fails its device link on a pre-existing `.ARM.exidx` /
  `.rtt_cached_reserve` overlap, A/B-proven against a stashed tree.
