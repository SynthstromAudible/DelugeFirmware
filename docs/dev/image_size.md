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
of them libstdc++ accessors). Each pays a call at every use site. Not an `.ARM.exidx` entry each: lld merges identical
adjacent entries, so there are 10,125 entries for about 23,000 functions.

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

### The scenes in the device image (P3d-1c step 3a, `feat/ui-integration`)

Linking `deluge_ui_shell`, `deluge_scenes`, `deluge_scenes_bind` and the `gui/ui/shell_*` guest beside the legacy
menu, measured on `RelWithDebInfo`:

| | Before (`f882c6678`) | After |
|---|---:|---:|
| SRAM heap | 264,784 B | **127,088 B** |
| `.text` | 1,615,676 B | 1,798,528 B |
| `.rodata` | 509,052 B | 442,956 B |
| `.bss` | 327,380 B | 327,876 B |
| `.ARM.extab` + `.ARM.exidx` | 130,192 B | 146,612 B |
| `.sdram_init` (image: its pack) | 72,612 B (72,612 B) | 269,556 B (27,668 B in a 33,950 B reserve) |

- The SDRAM content's load copy is deflated (`scripts/tasks/sdram_pack.py`, at staging) and inflated at boot
  (`miniz_oxide`, no allocator, about 9 KB of SRAM text and tables). The inflate code stays in `.text`: in the reclaimed
  tail, `--icf=safe` would fold other crates' identical functions onto its copies, and their callers would run heap
  memory.
- In `.sdram_init`: the scenes' menu node and cell tables (118,795 B, `deluge::scenes::menu`, ASSERTed over 64 KB), the
  legacy menu items' vtables (74 KB) and the scenes'. In `.sdram_bss`: `shellHostUI` (25.7 KB) and the scenes'
  statics (13 KB). The UI tier alone reads any of them.
- What stays in SRAM is code: the scenes' 98 KB and the shell and guest's 18 KB, beside the legacy menu items' 104 KB
  and `SoundEditor`'s 10 KB. Retiring the legacy menu (step 3d) returns the last two.
- Without the compression the image did not link: the load copy of the moved tables alone overflowed SRAM by
  60,476 B.

## Placement waste

Read off the linker scripts and the image's symbols. The load-copy item is built and measured (see
[Applied](#applied)); the others are not built:

- **The heap start is rounded up to 64 KB.** rza1-rt's `link_rtt.x` (deluge-sdk) does `. = ALIGN(0x10000); end = .;
  __sram_heap_start = end;`. The rounding exists so the bootloader's copy length is a whole 64 KB, and the heap does
  not need it. Starting the heap at the unrounded image end returns 0–64 KB (41 KB at `10e2f7133`). It also moves the
  image ceiling from `0x202E0000` to the RTT reserve at `0x202EB000`. The `code_end` metadata can keep the rounded
  value.
- **The `.sdram_init` load copy is compressed and returned to the heap.** `.sdram_init` is linked at its SDRAM address
  only; the build's staging deflates it into `.sdram_init_pack`, reserved at an eighth of its size in the boot-only
  tail after `.ARM.exidx`, and `__sram_heap_base` is the pack's start. The boot inflates it into SDRAM
  (`boot_mem.rs`, `sdram_pack.rs`) before the app creates its heaps, so anything placed in SDRAM costs about a tenth
  of its size in the image until the boot, and nothing in steady state (see [Applied](#applied)).
- **SDRAM can run code.** The MMU maps it executable and write-back cached (section attribute `0x1DEE`, XN = 0). The
  `PLACE_SDRAM_TEXT` "problems with execution from SDRAM" note in `board_config.h` dates from the legacy BSP. If the
  boot copies text into SDRAM with the caches on, it must clean the D-cache and invalidate the I-cache afterwards.
  Cold code (menus, browsers, song load/save) could move there. Code moved to SDRAM costs SRAM only between the
  bootloader's copy and the boot's, so the move frees its steady-state bytes. Copying from memory-mapped SPI flash
  (`0x18000000`, cached) at boot would take it out of the bootloader's copy entirely.
- **Data that could move to SDRAM:** about 47 KB of `deluge_bsp_rust::channel` task pools and staging buffers (cold,
  not DMA targets; same treatment as `EFATFS_ARENA`), and the efatfs `HANDLES` table (10 KB of `.data`). Reverb's
  128 KB of delay lines is a bench question: random-access taps through the L2.

## The UI and immediate mode

The menu system costs about 215 KB of SRAM:

| Part | Size |
|---|---:|
| `gui/menu_item/` code | 110 KB in 2,624 functions; 1,346 of them are 16 bytes or less |
| menu-item vtables | 73 KB: 295 vtables, `MenuItem` declares 49 virtuals |
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
  `linker/sdram_sections.x`. The fragment is passed before rza1-rt's `link.x`, so its patterns win.
- The SDRAM heap starts at `__sdram_static_end`, the end of all static SDRAM content, whatever the section order.
- The setting descriptor and name tables (their rows, extras, conditions and choice words) in SDRAM: every C++
  `menu_tables` inline namespace's `.rodata`, matched in `linker/sdram_sections.x`. A menu or an Edit reads them, never
  the audio tier or an interrupt. They are 15,868 B, measured as +18,976 B of SRAM heap together with the row
  shrinks that followed (heap 306,208 → 325,184 B at `8e794a52b`). The script ASSERTs more than 8 KB of them in
  `.sdram_init`, so a rename that stops the pattern matching fails the link.
- The image ends with its boot-only tail, which `__sram_heap_base` hands to the heap. rza1l-hal starts the heap at
  that hook, or at the unrounded image end.
  - This also fixed the `.bin`, which used to begin with the `.sdram_init` load copy instead of the vector table and
    the bootloader header. A probe loading the ELF never noticed.
- `-Oz` on the cold C++ (`cmake/optimise_for_size.cmake`, device builds only).
- The logger is installed only after `Board::new` turns the MMU on.
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

## Refactor targets

Audited on 2026-10-11 against `78d32470c`, after everything in [Applied](#applied): the SRAM heap is 444 KB, `.text`
1,505 KB, `.rodata` 433 KB, `.bss` 326 KB. Each target's current cost was measured from the ELF. Savings are estimates
unless marked measured.

| # | Target | Saves | Effort | Risk |
|---:|---|---:|---|---|
| 1 | Oscillator tables to SDRAM | 287 KB | an hour | audio path: bench |
| 2 | Reverb delay lines to SDRAM | 128 KB | an hour | audio path: bench |
| 3 | No exceptions | 100–145 KB | 4–6 days | OOM recovery paths |
| 4 | Immediate-mode menus, with context menus | 120–150 KB | large | existing program |
| 5 | Finish the domain split | 50–70 KB | large | existing program |
| 6 | Inline threshold instead of `-fno-inline-functions` | ~35 KB | a day of builds | codegen |
| 7 | Container instantiations | 25–30 KB | days | low |
| 8 | Async efatfs | ~25 KB | 3–4 days | low |
| 9 | Diagnostic strings | 15–25 KB | a day | none |
| 10 | Table-driven cold code | 10–12 KB | a day | none |
| 11 | Static destructors | ~6 KB | an hour | none |
| 12 | Streaming statics to SDRAM | ~45 KB | an hour | streaming: bench |

1. **Oscillator tables to SDRAM, 287 KB.** The `dsp::fixed` band-limited tables (74 wave tables, 264 KB, plus
   `kTanhTable`). Link them into `.sdram_init`: its load copy already goes to the heap, so the code change is one
   section pattern, and the result stays bit-exact. A voice reads one table of 4–16 KB, sequentially with a stride, so
   each 32-byte line serves about 16 samples. L2 misses then go to SDRAM instead of SRAM. Bench at high polyphony
   across bands.
   - Generating the tables at boot shrinks only the image, not the SRAM. FFTs show that the saw, square and triangle
     tables are exact Fourier truncations of each other, but `analogSquare` and `mysterySynthA/BSaw` are independent
     per band. Rounding a generated saw or square misses the stored values by 1 LSB in 2.7% of samples.
2. **Reverb to SDRAM, 128 KB.** `AudioEngine::reverb` is a `std::variant` sized by its 128 KB members (Mutable,
   Digital; Freeverb is 100 KB). It can go to `.sdram_bss`. Every tap advances sequentially, so it costs about one
   miss per 8 samples per tap (estimate). Bench it. The alternative, heap-allocating only the active model, saves
   just 28 KB.
3. **No exceptions, 100–145 KB.** Today the image carries `.ARM.extab` 34 KB, `.ARM.exidx` 81 KB, about 29 KB of
   landing pads and about 5 KB of unwinder.
   - **Scope:** 16 throw sites (the allocators, the three global `operator new`, `model_out_of_memory`, two copy
     constructors that throw) and 67 catches in 32 files. Because global `new` throws, every std container can throw.
   - **What the catches do:** about 42 convert to an error code, mostly on vector growth. About 12 clean up a
     half-built clone. About 9 ignore the failure. Seven are `catch (...)` firewalls. One is behavioural:
     `sound.cpp` steals a voice when there is no memory for a new one.
   - **Plan:**
     1. `[[noreturn]] oom()` and strong `std::__throw_*` definitions that call it.
     2. Fallible primitives: `try_alloc`, and a small vector with `try_push_back` / `try_reserve` returning
        `std::expected`.
     3. Convert the error-code catches. Make the throwing clones two-phase `cloneFrom() -> Error`.
     4. Allocators and `operator new` call `oom()`.
     5. Build with `-fno-exceptions` and delete the firewalls.
     6. Discard `.ARM.exidx` / `.ARM.extab`. Rust is already `panic=abort`.
4. **Immediate-mode menus, 120–150 KB.**
   - Already planned (`feat/immediate-mode`).
   - **Measured:** `menu_item` 97.5 KB of code and 71.5 KB of vtables (290 classes); `menus.cpp` 17.1 KB, of which
     15.3 KB is the static initialisation of 497 globals; `sound_editor` 10.2 KB.
   - Context menus are a further 12.4 KB and worth adding to that spec's scope.
5. **Finish the domain split, 50–70 KB.**
   - **Measured:** the legacy streaming `read*/write*File` methods are 46.9 KB (209 functions; `Song::readFromFile`
     alone is 5.6 KB). The bridge is 19 KB, and the streaming XML/JSON backends about 9 KB. The new codecs (about
     66 KB) are already linked, so today both paths are paid for.
   - **Interim:** reading everything through `TreeDocument` drops the two streaming deserializers (about 5 KB).
6. **An inline threshold, ~35 KB.**
   - 2,329 C++ functions are 4 bytes or less, reached by 12,091 calls. Inlining them saves about 35 KB of `.text`.
     Inlining 9–16-byte functions costs about 25 KB, which is why `-finline-hint-functions` lost.
   - **Try:** `-finline-functions -mllvm -inline-threshold=N` at N = 0, 5 and 25, the same threshold at the ThinLTO
     link (`-Wl,-mllvm,-inline-threshold=N`), and dsp-next left as it is. Unmeasured; LLVM's single-caller bonus
     needs watching.
7. **Container instantiations, 25–30 KB.** De-duplicated (`--icf=safe` already folds some), libstdc++ code is 132 KB.
   `std::vector` is 40.6 KB across 64 element types, 23.6 KB of it growth paths. `_Rb_tree` is 18.3 KB across 14
   instantiations.
   - Constant lookup maps (`map<Button,…>` in `horizontal_menu.cpp` and `patch_cable_strength.cpp`, `set<Scale>` in
     `chord_keyboard.h`) become a switch, array or bitmask: 5.5 KB measured.
   - A `PtrVector<T>` facade over one `vector<void*>`: about 8 KB. On the device, `external_allocator` can alias
     `std::allocator`, since both reach the same heap.
   - A type-erased mutation core for `OrderedPosVector`: 5–6 KB.
   - `MultiRangeArray` without `std::variant`: 3–4 KB.
   - One id-map core for the `Doc*` maps: about 4 KB.
   - `always_inline` on the project allocators: about 2 KB.
   - The audio-tier vectors (`unique_ptr<Voice>`, `VoiceSample*`, `TimeStretcher*`) get fewer calls under the
     facade, but bench them.
8. **Async efatfs, ~25 KB.** Storage and channel code is 110 KB, and generic duplication is not the cause. Each
   await costs about 100–115 B of state-machine boilerplate.
   - The vendored `crates/embedded-fatfs` (de)serializes its boot sector, BPB, FSInfo and directory entries one
     awaited field at a time: 15.2 KB. Reading the sector or entry once and parsing it synchronously saves 10–12 KB,
     and makes directory walks faster.
   - Eleven checkout → `with_fs` → commit functions in `efatfs/device.rs` each repeat about 600 B of wrapper. One
     `ctx_op(handle, CtxOp)` path saves 5–6 KB.
   - Path-level operations for `file_bytes` (read/write/stat in one `with_fs`): 4–5 KB.
   - One shared parent walk in `dir.rs` (about 8 copies today): 1.5–2.5 KB.
   - Dropping FAT12: 1.5 KB measured. Check that the host images aren't FAT12.
   - Nine channel REQUEST/RESULT/IN_FLIGHT statics (3.2 KB) sit in SRAM `.data` because their empty state has a
     non-zero niche byte. Zero it, or add the `.data` pattern to `.sdram_init`.
   - Don't make the FS synchronous: a synchronous SD wait on the preemptive streaming executor starves thread mode.
   - Bugs found on the way: `copy_file` reopens the destination for every chunk, and `deluge_efatfs_cluster_size`
     calls `fs.stats()`, which can scan the whole FAT.
9. **Diagnostic strings, 15–25 KB.** About 74 KB of printable strings sit in `.rodata`. The device build keeps all
   347 `D_PRINT*` sites (`ENABLE_TEXT_OUTPUT=1`).
   - Full `__FILE__` paths and Rust panic locations take 10.5 KB. Use `__FILE_NAME__` (or `-ffile-prefix-map`) and
     `--remap-path-prefix`: 4–8 KB.
   - Then either gate `D_PRINT` separately from RelWithDebInfo, or defer formatting defmt-style (an ID plus arguments
     over RTT): 12–18 KB.
10. **Table-driven cold code, 10–12 KB.**
    - `ArpeggiatorBase::calculateRandomizerAmounts` (3.9 KB, one block pasted ten times).
    - `FlashStorage` read/write/reset (5.3 KB).
    - `MidiFollow::initDefaultMappings` (2.5 KB).
    - `chords.cpp`'s 3.8 KB of static initialisation, which could be `constexpr`.
11. **Static destructors, ~6 KB.** 302 `__dtor_` thunks and 305 `atexit` registrations (`-fno-use-cxa-atexit`) for
    globals that are never destroyed. Use `-fno-c++-static-destructors`.
12. **Streaming statics to SDRAM, ~45 KB.** `deluge_sample_source::abi::POOL` (28 KB) and the sample-stream
    `REGISTRY` (12 KB), plus `streaming_fill_task::POOL` and `app_task::POOL` (4.4 KB, which the channel pattern
    misses). Check whether the render path walks them, then bench.

Leave in SRAM: `SD_DMA_BUF` (64 KB) and the SSI buffers (24 KB), which are DMA targets on the audio path, and the hot
DSP kernels.

Checked and not worth it: session vs arranger views (little shared code); near-duplicates ICF misses (26 KB over 196
groups, mostly allocator variants); the instrument hierarchy (no parallel duplication); vtables outside the menus
(about 24 KB); collapsing the storage channel tasks into one (about 1 KB, and it loses fairness against the streaming
fill); `std::function` (0.3 KB).

## Suggested order

1. Cheap and contained:
   - `--icf=safe`, the runtime shims and the heap-start fix in rza1-rt's `link_rtt.x`.
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
