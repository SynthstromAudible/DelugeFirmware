# SP-stream-read — completion design

**Status:** approved design, ready for an implementation plan. Nothing here is implemented.
**Base:** `feat/rustfs-sp1b-cached-chain` @ `20af6c863` (branched from `feat/async-sd-owner-substrate`
@ `1744c232d`).
**Ladder context:** `docs/superpowers/specs/2026-07-20-async-storage-end-architecture-design.md` §5
(gitignored, on-disk). This rung is **SP-stream-read**; the next on the critical path is SP-fileio.

**Supersedes `docs/dev/rustfs_sp1b_starting_point.md` entirely.** That document states two premises that
measurement and exploration disproved — see §1. Read this instead.

---

## 1. Corrections to the record

Three things the SP1b starting-point doc gets wrong. They are recorded here because each one cost real
investigation to establish, and the wrong version is still sitting in a committed file.

### 1.1 The cached FAT chain was dropped — measurement killed its premise

SP1b was scoped around an assumed O(n) chain re-walk on backward seeks. The real cost model, measured on
branch `feat/rustfs-sp1b-cached-chain` and committed as doc comments in
`src/bsp/rust/fs_differential/tests/efatfs_core.rs`:

> A chain re-walk to cluster `k` touches `ceil(k/128)` FAT **sector** reads, not `k` reads — FAT32 packs
> 128 entries into each 512-byte sector. Against 64 data sectors per 32 KiB cluster, constant
> long-distance seeking costs about **`1 + n/16384`** for an `n`-cluster file.

| file size | clusters | overhead under *constant* seeking |
|-----------|----------|-----------------------------------|
| 8 MB      | 256      | 1.016x                            |
| 64 MB     | 2048     | **1.15x (measured)**              |
| 256 MB    | 8192     | 1.5x                              |
| 4 GB      | 131072   | 9.0x                              |

Real looping playback measures **1.03x** — a loop point is one backward seek amortized over many
sequential reads. A cached chain would have bought approximately nothing at realistic sample sizes, in
exchange for ~6 KB of static BSS and three new vendored APIs. **Do not rebuild it without new evidence.**

**Two measurement traps, both hit and caught during that work:**
- Any seek-cost test on a **≤32-cluster file is vacuous** — the whole chain fits in one FAT sector. Use
  `/SAMPLES/huge.bin` (64 MB / 2048 clusters), added to `fixtures/mk_fixture.sh`.
- A loop-shaped test that reads many clusters sequentially per wrap **amortizes its seeks to nothing**.
  It measures looping *playback*, not seek cost. Isolating seek cost needs reverse or ping-pong order.

### 1.2 `seek()` re-walked on ANY cross-cluster seek — fixed

Upstream `embedded-fatfs` had only a same-cluster fast path; every cross-cluster seek, including a
one-cluster step *forward*, restarted the walk at `first_cluster`. Fixed on this branch (commit
`20af6c863`, `VENDOR.md` entry SP1b-1): measured 1.154x → 1.060x. Backward seeks still restart, by
design — that is what the dropped cache would have addressed, and §1.1 explains why it isn't worth it.

### 1.3 The "sync-fiber read path" was already dead; the live path is different

`loader::pump`/`request_pump` early-return whenever `deluge_streaming_async_active()` is true
(`loader.cpp:86`, `:172`), and that is `cfg!(feature = "async_streaming_loader")` — a **default** feature.
On the Embassy BSP the fiber pump is unreachable.

The live C-FatFS read path is `read_cluster_data` → `make_read_source()` → `StreamReadSource` →
`deluge_stream_read_at`, reached via `CLUSTER_LOAD_IMMEDIATELY` (`sample_stream.cpp:318-345`).
`make_read_source()` (`:168-173`) is **not flag-gated at all**. Callers include the waveform renderer,
`wave_table.cpp`, `sample.cpp`, `cluster_byte_source.cpp`, `audio_engine.cpp`, and the recorder sites.

Critically: `read_cluster_data` **receives `fill.handle` but never consults it** (`:215-218`). So
`efatfs_streaming` today covers only the async prefetch drain.

---

## 2. Exit criterion

**Nothing above the storage port knows what a sector or a cluster is** — for the streaming read path.

This is the rung's real purpose, and it is a stronger and more useful statement than "efatfs serves
streaming reads." The end-state direction is that file access lives in an SDK layer with pluggable
backends: FAT on device (where the SD card genuinely is FAT-formatted), passthrough to real host files in
the sim. `sdAddress`, `sector`, `num_sectors` and the cluster→sector map are FAT implementation details
that leaked upward into the audio streaming layer. Removing that leak is what makes any backend possible.

What leaks today, all on the read path:

| Leak | Location |
|---|---|
| `.sector` / `.num_sectors` | `StreamingFillDescriptor` (`include/libdeluge/streaming_fill.h`) |
| `table_[i].sdAddress`, `sd_address_at()` | `SampleStream` (`sample_stream.h` ~:181) |
| `deluge_stream_sector_of()` read branch | `stream_io.h` — the **port itself** exposes sector addressing |
| `resolve_read_layout`, `StreamLayoutEntry` | `stream_io.cpp:11-44` |
| `BlockReadSource` | raw sectors via `deluge_block_read` (`read_source.cpp:20-21`) |

`deluge::io::Stream::read_at(byte_offset, dst)` is already the right shape, and SP1a's
`{handle, byte_offset}` descriptor is too. The problem is not a missing byte-range port — it is **two
competing mechanisms with consumers bypassing the good one**. This rung collapses to one.

---

## 3. The port

Today's symbols are named after a backend (`deluge_efatfs_open`/`_close`), which is the SDK-layer
direction expressed wrongly. Rename to backend-neutral and add the missing read:

```c
bool deluge_stream_source_open(const char* path, uint32_t* out_handle);
bool deluge_stream_source_read_at(uint32_t handle, uint32_t byte_offset, void* dst, uint32_t len);
void deluge_stream_source_close(uint32_t handle);
```

**Rejected alternatives.** Keeping the `deluge_efatfs_*` naming bakes a backend name into a port we are
explicitly making pluggable. Reusing `stream_io.h`'s `DelugeStream*` handle fails because the Rust async
fill task needs a plain `u32`, not a C++ object pointer.

### 3.1 Backends

- **Device — embedded-fatfs.** `efatfs_fs.rs`, exists. The async fill task awaits `read_at` directly. The
  synchronous path bridges via `block_on_fiber`, which is **only valid on the fiber** — see §6, which is
  why converting the synchronous UI callers is a prerequisite rather than a detail.
- **Host sim — passthrough.** ~100 lines of C in `src/bsp/host/`, opening real files under a root
  directory. No Rust, no FAT, no block device, no MBR windowing. This is the end-state shape, not
  scaffolding.
- **Weak fallback.** Returns false, as the existing stubs in `async_fill.cpp:190-213` do.

### 3.2 Scope boundary

This rung moves **only the sample streaming read path**. It does *not* re-back `stream_io.h` /
`file_io.h` wholesale — that is SP-fileio. The recorder and generic file streaming stay where they are.

---

## 4. What gets deleted, and what survives

**Deleted:**
- `.sector` / `.num_sectors` from `StreamingFillDescriptor` (and the `static_assert` layout pins at
  `async_fill.cpp:45-51` and `streaming_loader.rs:112-126` updated to match)
- `resolve_read_layout` and `StreamLayoutEntry` (`stream_io.cpp`)
- `deluge_stream_sector_of()`'s **read** branch
- The `sdAddress` seeding loop in `open_read_stream` (`sample_stream.cpp:135-142`)
- The `efatfs_streaming` feature flag itself (§5)

**Survives, documented as SP-recorder's to remove — not oversight:**
- `sd_address_at()` / `sdAddress` for `BlockReadSource` recorder read-back. `BlockReadSource` is only
  reachable when `read_stream_` is disengaged, and `open_read_stream` is only called from
  `buildAudioFileFromCard`, so a recording `Sample` never has one. The read-streaming vs recorder split
  is sharp and real.
- `deluge_stream_sector_of()`'s **write** branch and the recorder write path
  (`sample_recorder.cpp:1433`, `:1605`, seeded at `:922`).

**Needs a decision during planning:** `audio_file_manager.cpp:248`'s cold-path card-change revalidation
compares a freshly-opened stream's `sector_of(0)` against the stored `sd_address_at(0)`. With the read
map gone this needs either a minimal first-cluster resolve retained, or a different revalidation
mechanism. It is a cold path, so either is acceptable — but it must be chosen deliberately.

---

## 5. The flag cannot survive — a forcing function, not a preference

Once `resolve_read_layout` and the `sdAddress` map are deleted, **flag-off has nothing to fall back to**.
Deleting the map and keeping `efatfs_streaming` are mutually exclusive. The flag stays during
implementation as a safety rail and is deleted as the rung's final act, once the gate is green.

This also kills a live trap: today a failed `deluge_efatfs_open` silently drops to the sector path
(`sample_stream.cpp:147-161`). Under a golden diff that degrades to flag-off and shows a **false "no
change"** — the gate passing for the wrong reason. Post-rung, failure is loud.

---

## 6. Prerequisite — the synchronous UI callers must stop blocking the executor

**Investigated 2026-07-21. Verdict was RED, and the resolution is a prerequisite step, not a redesign.**

On device, `deluge_stream_source_read_at` wraps `efatfs_fs::read_at` via `block_on_fiber`, valid only
while `crate::fiber::on_fiber()`. The investigation found that `CLUSTER_LOAD_IMMEDIATELY` bypasses every
piece of fiber-dispatch machinery: `get_cluster` → `deluge_resource_acquire` → `cluster_materialize` →
`read_cluster_data` runs synchronously in the caller's context, and several callers are plain UI handlers
with no `Owner::run` wrap — waveform redraw, marker drag, clip shift, bulk import, file-selection commit.
`wave_table.cpp` and `cluster_byte_source.cpp` are reached from **both** contexts.

**This is a pre-existing defect, not one this rung introduces** — it is recorded as **B6** in
`docs/dev/known-concurrency-bugs.md` with the full caller trace. Those paths already perform synchronous
card transfers off the storage owner; `sd.rs:398` silently absorbs it with a parking `block_on`, and the
assert that would catch it (`sd.rs:386-390`) is behind the non-default `storage-owner-audit` feature.

**Two findings that narrow the problem:**
- The **RT audio path is safe by construction** — `sample_low_level_reader`, `voice_sample` and
  `time_stretcher` use `CLUSTER_ENQUEUE` only, never a synchronous read.
- **`audio_engine.cpp:1391` is fine** — `previewSample`'s only caller is `sample_browser.cpp:632`,
  dispatched via `Owner::run` at `:551/564`. It is not on the RT render path.

### 6.1 Two resolutions were investigated and rejected

**Rejected — "dispatch the read onto the owner."** `Owner::run` is fire-and-forget; `owner.h` states it
explicitly and adds *"`await` (a blocking, result-returning variant) is a later rung; this seam is
`run`-only."* A synchronous cluster load needs the **bytes back**, so `run` cannot serve it. `request_pump`
gets away with `run` only because draining a queue is fire-and-forget.

**Rejected — "efatfs self-serializes, so off-fiber callers become safe."** Verified UNSAFE (analysis
2026-07-21). efatfs's own `Mutex` does make off-fiber callers *memory*-safe — no FS-state race, no
`RefCell` panic — but it converts a corruption hazard into a **liveness** hazard:

> `embassy_futures::block_on` is a tight `loop { fut.poll() }` with a **no-op waker**. If
> `streaming_fill_task` suspends holding the `FS` guard, and a UI task on the same thread-mode executor
> enters `block_on` on an efatfs read, that future's `FS.lock()` returns `Pending`, registers the no-op
> waker, and the busy loop spins forever. The SDHI IRQ marks the fill task ready, but the CPU never
> returns to `Executor::run` — it *is* the spinning stack. **Permanent deadlock**, and if the caller sits
> on the audio `InterruptExecutor`, thread mode can never resume and audio dies with it.

**This also kills `Owner::await` generally.** The hazard is structural, not specific to `block_on`: you
cannot block a cooperative task on a single-core executor while waiting for another task *on that
executor* to progress. `block_on_fiber` works only because the fiber is a **separate stack** that can
genuinely suspend and hand control back (`fiber.rs:755-774` → `switch_to_main`).

### 6.2 Decision — convert the synchronous UI callers

The synchronous loads must stop being synchronous-on-the-executor. Each site becomes either
**fiber-resident** (the whole operation dispatched via `Owner::run`, completing with a redraw/refresh) or
**enqueue-and-redraw** (request the cluster, return, repaint on arrival — the pattern the RT path already
uses via `CLUSTER_ENQUEUE`).

Sites to convert (full trace in B6): `waveform_renderer.cpp:404,433`; `sample.cpp:1395,1420` via
`loadAllSamplesInFolder`; `sample_holder_for_voice.cpp:139` via `claimCurrentFile`;
`sample_marker_editor.cpp:200`; `audio_clip.cpp:1396`. Plus the two **dual-context** paths —
`wave_table.cpp:422` and `cluster_byte_source.cpp:51` — which need care precisely because they are
already reached correctly from song-load/preview and incorrectly from the UI.

This is **UI-behaviour work with its own risk profile**, distinct from the storage work around it: an
operation that used to block until data arrived now completes later. Each site needs its own answer to
"what does the user see in between?" — and that is a design question per site, not a mechanical
transform.

**It is independently correct.** It fixes B6 — a defect in shipped firmware — whether or not the efatfs
routing ever happens. It must land and be verified before the map deletion, because once the map is gone
there is no non-fiber fallback left.

**Acceptance gate:** build with `storage-owner-audit` enabled, exercise the waveform renderer, marker
editor, clip shift and bulk sample import, and require the `sd.rs` single-owner assert
(`sd.rs:386-390`) to stay silent. That test fails today, which is what makes it a real gate.

### 6.3 B7 blocks the flag flip

`SdBlockDevice` bypasses `SD_BUS` (`fat_block_device.rs:64,78`), so an efatfs transfer can interleave with
a fiber-side C-FatFS transfer on the one SDHI controller. Recorded as **B7**. It is reachable only while
`efatfs_streaming` is enabled — so **the flip in §5 must not land until B7 is fixed.** Fixing it is
cheap and unblocked; it should land early rather than as part of the flip.

---

## 7. Verification

No single instrument gates this rung; the coverage decomposes.

| Instrument | Proves | Status |
|---|---|---|
| `fs_differential` | embedded-fatfs ≡ C FatFS byte-for-byte, real FAT16/32, through the real block stack | exists |
| **golden + passthrough** | streaming *integration* end-to-end: handle lifecycle, descriptor plumbing, offset arithmetic, short reads at the last cluster, the C++ wiring | needs §3.1 passthrough |
| **streaming-sequence differential** (new) | efatfs serving the *actual playback access pattern* — open, cluster-aligned reads in playback order including loop points, close — vs C FatFS whole-file bytes | new, ~an afternoon |
| Lens-1 (`lens1_vt_sim`) + Lens-2 (`preemptive_race_tsan`) | margin, and audio-preempts-streaming races | exist; **re-run with the new path live** |
| on-device | the composition | Kate's gate |

**Why golden cannot gate the FAT layer, and why that is fine.** Under passthrough the sim never sees
FAT, so correct bytes yield identical audio however they were read. Golden therefore verifies the
streaming *integration*, which is exactly where SP1a-era bugs live. FS correctness is `fs_differential`'s
job and always was.

**Why the streaming-sequence differential is worth adding.** Golden-with-passthrough leaves one gap:
efatfs is never exercised *in the streaming access pattern* on host. This closes most of it cheaply by
reusing existing `fs_differential` machinery.

**Harness wiring is nearly free:** `tests/golden/run.py` already builds its FAT image from a staging
directory, so the passthrough root can point at that same directory. Note `run.py` lives on
`feat/synth-golden-masters` and must be brought over.

**Host test invocation (mandatory):** `SP0_FAT32=/tmp/fat32.img SP0_FAT16=/tmp/fat16.img` and
`--test-threads=1`. Each binary loads a 2.5 GB image; parallel runs OOM (observed SIGKILL). Rebuild
fixtures with `fixtures/mk_fixture.sh /tmp`.

---

## 8. Why the C++ HAL is not retired in this rung

It is currently the **oracle**, not merely legacy:

1. The sim is built on it (`src/bsp/host/*.c`), and golden renders through the sim. Retiring it means the
   sim runs on the Rust BSP via `host_app` — Embassy executor, fibers, bindgen, two BSPs with overlapping
   symbols, inverted link direction. `host_app` also uses a preemptive OS-thread executor while golden
   needs deterministic frame-clock rendering. Retiring the HAL now removes the gate at the moment it is
   most needed. *(Whether `host_app` could ever render deterministically enough for golden is unverified
   — if it could, this calculus changes.)*
2. C FatFS is the differential oracle. `fs_differential` found and fixed two real write bugs **in the
   vendored crate** (`Dir::rename` dropping a multi-component path; FAT32 `create_dir` writing the root
   cluster into `..`). Deleting C FatFS deletes the instrument that catches that class of bug.

This does not contradict the committed "Embassy/Rust is the only backend" constraint. What that dropped
was carrying two backends as a *shipped product decision*. Carrying a reference implementation during a
migration whose every rung is gated on bit-exactness is different — temporary scaffolding with a
scheduled demolition date, and the ladder already sets it: **SP-delete**, gated on P1.

---

## 9. Out of scope

- Re-backing `file_io.h` / `stream_io.h` wholesale → **SP-fileio**
- `sd_address_at` / `sdAddress` for recorder read-back and the write path → **SP-recorder**
- Deleting C FatFS and retiring the fiber → **SP-delete** (needs P1)
- Rebuilding a cached FAT chain → **do not**, see §1.1
- Retiring the C host BSP → not this rung; see §8
