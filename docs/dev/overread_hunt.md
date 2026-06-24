# Hunting the layout-dependent sample overread

## Goal

Find (and then fix) the latent **overread** in the sample read/interpolation path that makes the offline
golden render depend on heap **allocation layout**. Right now correct-looking audio silently depends on
what bytes sit *past* a buffer — a real bug (reads garbage past valid samples; exists on hardware too, it's
just usually masked). The proper fix is to stop reading past valid data; the current `ChunkSlot.cost` u8
packing only keeps the struct byte-stable so the golden doesn't notice.

## What we already know (don't re-derive)

- **Symptom:** growing `ChunkSlot` 44→48 bytes (adding `size: u32`) flipped the **highsiderr** golden
  deterministically — first diff at ~sample 37 (byte 192), `rms 159180` — with **0 evictions**. Bisected:
  vtable-innocent, `COST_OBJECT`-value-innocent, eviction logic never runs. Pure layout.
- **Mechanism class is confirmed by the code itself.** `src/deluge/memory/general_memory_allocator.cpp:64-68`
  documents it: the slab zeroes each cluster slot on acquire *specifically* because "a SampleCache reading
  interpolation-overhang bytes **past its write position**" would otherwise read layout-dependent recycled
  content. So the overhang-overread is known; the slab case was masked by `deluge_slab_set_zero_on_acquire`.
- **Why the deterministic gate doesn't fully mask it.** The gate zeroes memory at *hand-out*
  (`finalizeAlloc` GMA:40, `operator new` `memory/operators.cpp:14`, `ObjectPool::acquire`
  `memory/object_pool.h:113`, slab zero-on-acquire GMA:68). It does **not** zero (a) **free** heap that was
  never handed out (TLSF keeps stale bytes), nor (b) a **different neighbor** when the overhang crosses a
  buffer boundary. So an overhang that reads past a *heap* buffer (or past the slab's end) lands in
  layout-dependent memory the gate never touched. Growing the chunk table shifted every later allocation →
  changed those neighbors → changed the overhang samples.
- **The masked path is slab/cluster; the tripped path is NOT.** Growing the chunk table shifts the slab's
  base address but not its internal slot order, so slab-internal neighbours are unchanged. The flip
  therefore comes from a **heap-backed** buffer's overhang (perc cache / a heap cache / a sample tail), or
  the overhang crossing **past the slab's end** into the shifted heap.
- **Tools that WON'T work:** ASAN / valgrind only trap reads into *their* redzones. Our buffers come from a
  **custom** allocator (TLSF over a fixed SDRAM region) with no redzones, and an overread into an adjacent
  *valid* allocation isn't a fault. Electric Fence / MALLOC_CHECK target `malloc`, not our SDRAM heap. So a
  **guard page on the custom heap** is the only reliable read-trap (technique A).

## Step 0 — a fast, tunable reproducer

The full highsiderr render is ~120 s; we don't need it — the divergence is in the **first render block**.

1. **Known trigger (no new code):** revert the band-aid so `ChunkSlot` is 48 bytes again — change
   `ChunkSlot.cost` back to `u32` in `crates/deluge_resource/src/manager.rs` (and the two casts:
   `evict_lowest` `(s.cost as u32, 0)` → `(s.cost, 0)`, `evict_slot` drop the `as u32`, adopt `cost: cost as u8`
   → `cost`). Rebuild the i686 lib (`cargo build -p deluge_rust --release --target i686-unknown-linux-gnu`)
   + `cmake --build build-sim-cpp --target deluge_render`. `FIXTURE=highsiderr scripts/golden_mixdown.sh check`
   now FAILs at byte 192. This is the oracle: any real fix must make highsiderr pass **with the 48-byte
   struct**.
2. **Better, tunable trigger (recommended):** add a sim-only boot pad so you can shift layout by an arbitrary
   amount *without* touching the struct. In the host BSP / GMA init, once, gated on an env var:
   `if (getenv("DELUGE_SIM_HEAP_PAD")) deluge::memory::alloc_external(atoi(...), 16); // leaked on purpose`.
   Then `DELUGE_SIM_HEAP_PAD=4 … check` should reproduce, `=0` should pass. Sweeping the pad size maps the
   sensitivity and, later, proves the fix (golden stays green for *all* pad sizes).
3. **Shrink the render** so iterations are seconds, not minutes: cap the export to the first ~2048 frames
   (a `--max-frames` flag on `deluge_render`, or temporarily clamp the stem-export length) and diff only
   that. The bug shows in the first block, so this loses nothing.

## Technique A — guard-page the custom heap (decisive; do this first)

Goal: turn the silent over-end read into a SIGSEGV with an exact backtrace.

1. Add a sim-only allocator mode (`DELUGE_SIM_GUARD_HEAP`) to the **external/SDRAM** allocation path
   (`alloc_external` / the `GeneralMemoryAllocator` external region — the allocations that shift; start with
   heap-backed, since slab is already masked). In guard mode, bypass TLSF and back each allocation with
   `mmap`: round the request up to a page, append one `PROT_NONE` guard page, and **place the block so its
   end abuts the guard page** (right-align within the rounded region). `free` → `munmap`.
2. Run the reproducer. The first read ≥1 byte past a block faults; the backtrace names the exact buffer and
   read site. That's the overread.
3. Notes: right-alignment is essential (catches over-*end* reads, the bug; left-alignment would only catch
   under-reads). Expect this mode to be slow and memory-hungry (a page per alloc) — fine for one render.
   If nothing faults on the heap path, extend the guard to the **slab** (over-allocate each slot by a page
   and guard it) to catch an overhang crossing the slab's end / a slot boundary.

## Technique B — free-poison differential (cheap; corroborates + localizes)

The gate zeroes on hand-out but leaves **freed** memory stale. Add a sim-only **poison on free** (memset the
block to a recognizable sentinel, e.g. `0xA5`, in the `free`/`dealloc` path under `DELUGE_DETERMINISTIC_ALLOC`).

- If the render **changes** vs zero-on-free (or the divergent output samples now carry a recognizable
  `0xA5`-derived value) → the overread reads **free** memory → confirmed, and the leaked value's magnitude
  /position helps localize the source buffer.
- If the render is **unchanged** by the free-poison → the overhang reads a **live neighbor** instead; switch
  focus to which live allocation sits past the suspect buffer.

This needs no guard pages and reuses the existing deterministic-alloc plumbing.

## Technique C — bisect the buffer *class* by toggling masking

The gate's four zeroing sites each mask a class. Turn them off one at a time (keep the others on) and see
which un-masking reproduces the layout sensitivity / changes highsiderr at full heap:

- slab zero-on-acquire (GMA:68) — cluster/cache slots
- `finalizeAlloc` (GMA:40) — GMA `alloc()` (incl. perc cache via `alloc_external`?)
- `operator new` (operators.cpp:14) — std-container buffers
- `ObjectPool::acquire` (object_pool.h:113) — Voice/VoiceSample/TimeStretcher pools

Whichever class, when un-masked, makes the render layout-sensitive is where the overhang lands. (We expect a
**heap** class, not the slab — see "what we know".)

## Technique D — read the overhang code directly

The class is "interpolation/cache overhang past write position". Inspect the read path for an index that can
exceed the validly-written length:

- `src/deluge/model/sample/sample_low_level_reader.cpp` — `considerUpcomingWindow` already got an overshoot
  clamp (the P4-D2 fix: `bytesLeftWhichMayBeRead < 0 → 0`). Look for **sibling** lookahead reads that weren't
  clamped (the interpolation taps, the window-fill loop).
- `src/deluge/dsp/timestretch/time_stretcher.cpp` and `src/deluge/model/sample/sample_cache.cpp` — the
  repitch/perc cache fill + read (highsiderr is repitch-cache-heavy). Look for reads of
  `[pos + INTERPOLATION_MAX_NUM_SAMPLES]`-style overhang past the cache's written cluster length.
- `src/deluge/model/voice/voice_sample.cpp` — interpolation buffer fill at cluster boundaries.
- Anchors: `sizeof(Cluster) + Cluster::size` is the slot size; the overhang is a small constant
  (interpolation lookahead). Find where a read can pass the last written sample without the cross-cluster /
  end-of-sample guard kicking in.

## Confirm & fix

1. **Fix:** clamp/pad the read so it never passes the validly-written length — either stop the read at the
   boundary, or guarantee a zeroed pad region of `overhang` samples *within* the same allocation (so the
   read stays in-bounds and reads defined 0). Prefer the latter if the DSP needs the lookahead samples.
2. **Prove it:** with the fix, highsiderr passes **with the 48-byte struct** AND for every `DELUGE_SIM_HEAP_PAD`
   value. Then the `cost` u8 band-aid is no longer load-bearing (you can keep it as a cheap win or revert).
3. **Regression guard:** add a golden/CI mode that renders each fixture twice under two different
   `DELUGE_SIM_HEAP_PAD` values and asserts bit-exactness. That turns "a struct-size change silently shifted
   a fixture" into an immediate, localized failure — the gap behind this whole episode (see the caveat added
   to the deterministic-alloc gate: a green golden does **not** prove a layout change is neutral).

## Pitfalls

- A green golden does not prove neutrality — it's layout-pinned. Use the pad-sweep guard above.
- Don't chase ASAN/valgrind on the custom heap; they won't see it.
- The overhang may read its **own** buffer's unwritten slack (in-bounds) in some paths — that's already
  masked by hand-out zeroing and is *not* this bug. The bug is the **past-end** read (guard pages catch
  exactly that).
- Keep `DELUGE_HOST_DETERMINISTIC=1` for all repros; without it other nondeterminism muddies the diff.
