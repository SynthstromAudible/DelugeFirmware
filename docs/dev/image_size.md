# Device image size

Where the device image's on-chip SRAM goes, and the levers that shrink it, each measured by building the image. The
image (code, read-only data, initialised data and `.bss`) lives in the RZ/A1L's 2.875 MB of on-chip SRAM, and whatever
it leaves over becomes the internal heap. Measured on 2026-10-11 at `10e2f7133`. Every figure in
[Measured levers](#measured-levers) comes from a full `dbt build RelWithDebInfo`. Figures marked *estimate* do not.

## Measuring

- **Section sizes:** `llvm-size -A -d build/RelWithDebInfo/deluge.elf`.
- **Headroom:** the SRAM heap, `__sram_heap_end - __sram_heap_start` (`0x202EB000`, the RTT reserve, minus the start).
  The heap starts where the image's boot-only tail does (see [Applied](#applied)), and rza1l-hal ASSERTs at link time
  that it is not negative. The audit below predates that layout: there, the HAL started the heap at the image end rounded
  up to 64 KB, the image had to end at or below `0x202E0000`, and headroom was `0x202E0000 - __exidx_end`.
- **Attribution:** `llvm-nm -S --defined-only` for every sized symbol, then `llvm-symbolizer --no-inlines` on its
  address to find the source file. C++ is bucketed by source directory and Rust by crate (from the v0 mangling). Data
  symbols have no line info, so they are bucketed by name instead.
- **Variants:** each lever is a patch on the same base, built in a separate worktree. A worktree needs
  `DBT_NO_SYNC=1`, because `dbt` insists on a `.git` *directory*. A worktree's `__FILE__` strings are longer, so its
  base `.rodata` is 1.9 KB larger than the main tree's. Variants are compared against the worktree base.

The scripts are in `.superpowers/estate/size-audit/`: `classify.py` attributes symbols (its section ranges are
hard-coded, so update them from `llvm-size` first), `measure.sh` prints one result line per image, and `variants.sh`
applies, builds and measures named variants. Run a copy of `variants.sh`, not the file itself: bash reads a script as
it runs, so editing it mid-run replays part of it.

## Where the SRAM goes

| Section | Size |
|---|---:|
| `.text` | 1,701 KB |
| `.rodata` | 509 KB |
| `.bss` (+ `.frunk_bss`) | 371 KB (+ 35 KB) |
| `.ARM.exidx` + `.ARM.extab` | 97 KB + 42 KB |
| `.data` | 41 KB |
| `.sdram_init` load copy (dead after boot) | 30 KB |

Headroom under `0x202E0000`: 41,400 B.

The largest contributors:

| What | SRAM | Notes |
|---|---:|---|
| `gui/` (ui, views, menu_item, context_menu) | ~440 KB | code; plus 73 KB of menu-item vtables in `.rodata` |
| `dsp::fixed` oscillator tables | 287 KB | band-limited saw/square tables; read per sample, keep in SRAM |
| libstdc++ template instantiations | 175 KB | thousands of small copies (`vector<T>::_M_fill_insert` per `T`, `_Rb_tree` per map) |
| Rust, all crates | 274 KB | see [Rust](#rust) |
| vtables (all) | 95 KB | 553 vtables, 295 of them menu items |
| `AudioEngine::reverb` | 128 KB | `.bss`; delay lines |
| `SD_DMA_BUF` | 64 KB | `.bss`; DMA target, must stay in SRAM |
| libc/runtime (no debug info) | 98 KB | see the shims lever below |

Under `-fno-inline-functions`, clang marks every definition `noinline`, so 5,910 functions are 4 bytes or less (4,108
of them libstdc++ accessors). Each pays a call at every use site and an 8-byte `.ARM.exidx` entry.

Duplicate crates are not a cost. `--gc-sections` drops unreferenced copies, which was shown in 2026-08 and still holds.

## Measured levers

Headroom each adds when applied alone to the base (39,480 B in the worktree):

| Lever | Gain | Notes |
|---|---:|---|
| `-Oz` on cold sources only | **+93.4 KB** | see below; hot code untouched |
| `-Wl,--icf=safe` | **+90.5 KB** | lld folds identical functions; link-only. `--icf=all` +91.6 KB, but can merge functions whose addresses are compared |
| Runtime shims | **+71.3 KB** | removes the demangler and all of newlib printf/stdio; see below |
| `-fignore-exceptions` | **+68.7 KB** | stand-in for an exception-free build; `.ARM.extab` 41.7 → 1.8 KB |
| Rust `opt-level = "z"` | **+41.5 KB** | Rust text −33 KB, C++ −12 KB |
| `-Os`, all C++ | +83.2 KB | slows the audio path too |
| `-Oz`, all C++ | +161.2 KB | slows the audio path too |
| `-finline-hint-functions` instead of `-fno-inline-functions` | **−18.0 KB** | worse: `.text` +52 KB. Keep `noinline` |
| `-fno-unwind-tables` (with `-fignore-exceptions`) | 0 | no-op on ARM EHABI, see below |

**All five recommended levers together** (`--icf=safe`, shims, `-fignore-exceptions`, Rust `"z"`, cold `-Oz`): headroom
goes from 39 KB to **386 KB** (+346.5 KB).

### Cold `-Oz`

Applied with per-source `COMPILE_OPTIONS` to `gui/`, `hid/`, `storage/`, `io/debug/`,
`model/{project_bridge,edits,action,consequence,favourite}/` and `processing/stem_export/`, plus `target_compile_options`
on `deluge_project`, `deluge_serialization` and `deluge_music_theory`. DSP, voices, playback, modulation and the clip
model stay at `-O2`.

The `optsize`/`minsize` function attributes survive ThinLTO, so per-source levels work. An earlier `-Os` trial on the
model libraries saved only 2.7 KB because those libraries are small.

### Runtime shims

Three small definitions:

- `__gnu_cxx::__verbose_terminate_handler` traps. Today it pulls in libiberty's `__cxa_demangle` (32 KB) and
  `fputs`/`fwrite`.
- `std::__glibcxx_assert_fail` traps. It has 4 callers, and pulls in `fprintf` → `_vfprintf_r` → `_dtoa_r`.
- `snprintf` forwards to `vsnprintf_` from `eyalroz_printf`, which is already linked. Today the 4 app `snprintf` calls
  reach newlib's float `_svfprintf_r`. Every format in use (`%d`, `%s`, `%03d`, `%.4f`) is supported by
  `eyalroz_printf`. A production version could instead set `PRINTF_ALIAS_STANDARD_FUNCTION_NAMES_HARD` in
  `src/lib/printf.h`.

In production, the traps should report through `FREEZE_WITH_ERROR`, not `__builtin_trap`.

### Exceptions

Every C++ TU builds with exceptions on. They are used to report allocation failure: the SDRAM, fast and external
allocators `throw deluge::exception::BAD_ALLOC`, and about 40 files catch it. `-fignore-exceptions` drops the landing
pads and the LSDAs, which approximates the saving from converting those sites to returned errors (`std::expected` or a
null check).

On ARM EHABI, LLVM emits an `.ARM.exidx` entry for every function whatever the flags, so `-fno-unwind-tables` changes
nothing. A true `-fno-exceptions` build marks functions `nounwind`, so their entries become `CANTUNWIND`, and lld
merges adjacent identical `CANTUNWIND` entries. With no unwinder left, `.ARM.exidx` could be discarded outright. That
is up to a further ~87 KB (*estimate*). Nothing else reads the tables: the fault reporter scans the stack for code
addresses.

## Placement waste

Worked out from the linker scripts, not built:

- **The heap start is rounded up to 64 KB.** `rza1l_rtt.x` (deluge-sdk) does `. = ALIGN(0x10000); end = .;
  __sram_heap_start = end;`. The rounding exists so the bootloader's copy length is a whole 64 KB, and the heap does
  not need it. Starting the heap at the unrounded image end returns 0–64 KB (41 KB at `10e2f7133`). It also moves the
  image ceiling from `0x202E0000` to the RTT reserve at `0x202EB000`. The `code_end` metadata can keep the rounded
  value.
- **The `.sdram_init` load copy is never reused.** It is placed `AT> RAM` straight after `.bss` (LMA `0x2007E924`,
  29.5 KB). The boot copies it to SDRAM in `runtime/device.rs` before the app runs, and the SRAM is never used again.
  Putting the load copy at the end of the image and starting the heap at its LMA returns it as heap. That also makes
  anything placed in SDRAM free in steady state: it costs SRAM only between the bootloader's copy and the boot's.
  `fault.rs` uses `__sram_heap_start` as `code_end`, so it would need the true end of code instead.
- **SDRAM can run code.** The MMU maps it executable and write-back cached (section attribute `0x1DEE`, XN = 0). The
  `PLACE_SDRAM_TEXT` "problems with execution from SDRAM" note in `board_config.h` dates from the legacy BSP. If the
  boot copies text into SDRAM with the caches on, it must clean the D-cache and invalidate the I-cache afterwards.
  Cold code (menus, browsers, song load/save) could move there. Before the load-copy fix above, that only moves
  bytes, because the load copy still occupies SRAM. Copying from memory-mapped SPI flash (`0x18000000`, cached) at
  boot would take it out of the bootloader's copy entirely.
- **Data that could move to SDRAM:** about 47 KB of `deluge_bsp_rust::channel` task pools and staging buffers (cold,
  not DMA targets; same treatment as `EFATFS_ARENA`), and the efatfs `HANDLES` table (10 KB of `.data`). Reverb's
  128 KB of delay lines is a bench question: random-access taps through the L2.

## The UI and immediate mode

The menu system costs about 236 KB of SRAM:

| Part | Size |
|---|---:|
| `gui/menu_item/` code | 110 KB in 2,624 functions; 1,346 of them are 16 bytes or less |
| menu-item vtables | 73 KB: 295 vtables, `MenuItem` declares 49 virtuals |
| `.ARM.exidx` entries for that code | ~21 KB |
| `menus.cpp` + `sound_editor.cpp` code | 32 KB, including a 2 KB static constructor |

The 412 menu objects themselves are already `PLACE_SDRAM_BSS`.

The cost is the shape, not the logic: one class per menu item, many tiny overrides, and a 250-byte vtable for each
class. ICF and cold `-Oz` reduce the code but not the vtables. With all five levers applied, the menu system is still
about 214 KB. The immediate-mode design (`docs/superpowers/specs/2026-07-02-immediate-mode-ui-design.md` on branch
`feat/immediate-mode`, OLED scope) replaces it with plain build functions over a small widget engine. Phase 3 of that
plan deletes `menus.cpp`, the ~285 menu classes and `SoundEditor`. At roughly 60 B of build code per item plus an
engine of 10–15 KB, the replacement would be 30–50 KB, saving about 160–180 KB on top of the levers (*estimate*).

The rest of `gui/` gains little from immediate mode:

| Area | Code | Why |
|---|---:|---|
| browser, load, save | ~73 KB | mostly listing and loading logic, which is the same in either paradigm; phase 4 trims the drawing and navigation |
| grid views (`instrument_clip_view`, `session_view`, `arranger_view`, `automation_view`, `performance_view`, keyboard) | ~165 KB | large gesture-handling functions, out of the plan's scope |

## Rust

Rust totals 274 KB of code and data. Most of it is subsystems that used to be C (FatFS, storage, USB, sample
streaming), so it is moved function rather than language overhead:

| Subsystem | Size |
|---|---:|
| Filesystem: `embedded_fatfs` (~50 KB, most of it instantiated over the SD block device), the efatfs layer (54 KB, of which 13 KB is `.data` handle tables) and the file channels (`file_bytes`, `file_stream`, `file_directory`, `file_prefix`, ~28 KB) | 139 KB |
| Sample streaming and the resource manager (`deluge_sample_*`, `deluge_resource`, audio loader, streaming loader) | 45 KB, of which 12 KB is the `.data` `REGISTRY` |
| `compiler_builtins`: libm, memcpy, 64-bit division. It serves the C++ app: its `sinf`, `powf`, `expf` and the rest resolve here through 12-byte thunks, and newlib's libm is not linked | 20 KB |
| USB and MIDI | 17 KB |
| Runtime, executor and boot | 15 KB |
| Peripherals, HAL, SD driver | 18 KB |
| `core::fmt`, logging, panic paths | under 7 KB |

Why the filesystem is large: almost none of it is monomorphization, since nearly every `embedded_fatfs` function is
instantiated once. The cost is async state machines. Each `task_*` entry point (`task_dir_open` 3.4 KB, `task_rename`
3.1 KB, `task_unlink` 2.5 KB) and `mount` (9.4 KB) inlines its whole path walk and cluster chain into its own future.
The same shape shows under `embassy_executor`: its 36 KB is our task bodies inlined into `TaskStorage::poll` (the USB
device task 5.5 KB, the audio loader 4.0 KB).

Rust levers:

- `opt-level = "z"`: measured +41.5 KB. Applied with overrides (+31 KB), with no formatting before the MMU is on (see
  [Applied](#applied)).
- Move the shared path-walk and directory-entry helpers out of line (`#[inline(never)]` on non-async helpers, or one
  shared future), so the tasks stop each carrying a copy (*not measured*).
- Make the `.data` tables (`HANDLES`, `TASK_FILES`, `REGISTRY`) zero-initialised or move them to SDRAM.
- `bench_park` is spawned behind a runtime check (`bench_ran`), so non-bench images carry it (1.9 KB plus its task
  pool). A `cfg` gate drops it.

## Applied

On branch `feat/image-size` (with rza1l-hal `40ec4195` on deluge-sdk branch `fix/sram-heap-at-image-end`), everything in
steps 1 and 2 below except moving the sample-stream `REGISTRY`:

| | Before (`10e2f7133`) | After |
|---|---:|---:|
| SRAM heap | 44 KB | **444 KB** |
| `.text` | 1,701 KB | 1,450 KB |
| `.rodata` | 509 KB | 504 KB |
| `.data` | 41 KB | 27 KB |
| `.bss` | 371 KB | 326 KB |
| `.ARM.extab` + `.ARM.exidx` | 138 KB | 112 KB |
| Boot-only tail (`.sdram_init` load copy, `.init_array`) | 30 KB, stranded mid-image | 43 KB, returned to the heap |

- `--icf=safe`, and `snprintf` resolved to `eyalroz_printf` at link (`build/device.rs`).
- libstdc++'s terminate handler and `__glibcxx_assert_fail` replaced by Rust ones that report through the panic
  handler (`ffi_extra.rs`). A C++ `std::terminate` used to be a silent device death. It now prints over RTT and draws
  the fault pattern on the pads.
- `bench_park` compiled only into `DELUGE_DSP_BENCH` images (`cfg(dsp_bench)`, read from the CMake cache).
- The storage channels' state and the efatfs mount's tables in SDRAM, by v0-mangled module path in
  `linker/sdram_sections.x`. The fragment is passed before `rza1l.x`, so its patterns win.
- The SDRAM heap starts at `__sdram_static_end`, the end of all static SDRAM content, whatever the section order.
- The image ends with its boot-only tail, which `__sram_heap_base` hands to the heap. rza1l-hal starts the heap at
  that hook, or at the unrounded image end.
  - This also fixed the `.bin`, which used to begin with the `.sdram_init` load copy instead of the vector table and
    the bootloader header. A probe loading the ELF never noticed.
- `-Oz` on the cold C++ (`cmake/optimise_for_size.cmake`, device builds only).
- The logger is installed only after `init_clocks()` turns the MMU on.
- Rust `opt-level = "z"`, with the sample-streaming crates, the resource manager, both allocators, the HAL and the
  executor at `"s"`: +31 KB, against +42 KB for `"z"` everywhere.

**Nothing may format before the MMU is on.** Built for size, LLVM lowers unaligned reads to `__aeabi_uread4` calls,
and `compiler_builtins` implements that helper as a plain `ldr`. Until the MMU is enabled, all memory is strongly
ordered and an unaligned access faults. `core::fmt` reads its templates unaligned, so the first `"z"` image faulted on
its first log line (DFSR `0x01`). `core` is built by build-std at the root crate's level, so no per-package override
reaches it. With no logger set, the `log` macros return before formatting. The HAL's pre-MMU lines are therefore
dropped, among them `gic::init`'s report of an interrupt a reset left active. The cold C++ under `-Oz` also calls
`__aeabi_uread4`, which is safe only because none of it runs before the MMU is on.

The host test estate passes. The image boots on a device: "going into main loop" at 1.51 s and the job summary at
11.52 s, the same as before. The startup job's worst run was 26 ms, against 21–23 ms for the `"s"` images.

## Suggested order

1. Cheap and contained:
   - `--icf=safe`, the runtime shims and the heap-start fix in `rza1l_rtt.x`.
   - A `cfg` gate on `bench_park`.
   - The storage-only Rust buffers to SDRAM: the channel task pools and staging buffers (~47 KB) and the efatfs
     `HANDLES` table (10 KB). Only the storage tasks touch them and none are DMA targets. Zero-initialising them alone
     saves nothing, because `.data` and `.bss` are both SRAM.
2. Needs a check with the DSP bench (`tools/dsp_bench.py`):
   - Cold `-Oz`, Rust `opt-level = "z"` and reclaiming the `.sdram_init` load copy.
   - The sample-stream `REGISTRY` (12 KB) to SDRAM, since the playback path may look it up.
   - The efatfs path walk out of line: measure it as a variant after `"z"`, which already inlines less.
3. Structural:
   - Remove exceptions (`BAD_ALLOC` → returned errors) and discard `.ARM.exidx`.
   - Cold code to SDRAM.
   - Immediate-mode menus, an existing program on its own merits. Its ~160–180 KB is measured against the image after
     steps 1 and 2, so it adds to them.
