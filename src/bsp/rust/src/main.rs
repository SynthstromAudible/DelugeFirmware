//! Rust/Embassy firmware image for the Synthstrom Deluge (RZ/A1L).
//!
//! The third BSP for the Deluge, alongside `src/bsp/rza1` (on-device C) and
//! `src/bsp/host` (simulator). Unlike those, this one OWNS reset, `main`/`_start`
//! and the Embassy executor: the HAL startup (`rza1l_hal` `_start` →
//! `_reset_handler` → `bl main`) lands in [`main`] here, which brings up the
//! platform on the deluge-sdk HAL+BSP and runs the portable C++ application's
//! `deluge_main()` superloop inside a single Embassy task (Stage 1).
//!
//! The C++ application is compiled by CMake into `libdeluge_app.a` and linked in
//! by `build.rs`; this crate implements the `<libdeluge/...>` C-ABI services it
//! calls. See docs/dev/libdeluge_bsp_design.md.
#![cfg_attr(target_os = "none", no_std)]
#![cfg_attr(target_os = "none", no_main)]
#![feature(impl_trait_in_assoc_type)]
// `audio_load_task`'s future nests the load driver's async calls inside the
// `LoadOps` trait's own `async fn` futures, and `embassy_executor::task` makes
// rustc compute the layout of the whole `TaskPool<Fut>` in one go. That exceeds
// the default 128-deep query limit ("queries overflow the depth limit" while
// computing the layout of `TaskPool<..audio_load_task::Fut..>`), which is a
// compiler budget, not a sign of runaway recursion — the future is a fixed,
// finite size. Raise it rather than flattening the driver into one function and
// losing the structure that makes finish/abandon exactly-once.
#![recursion_limit = "512"]

#[cfg(target_os = "none")]
use core::mem::MaybeUninit;
#[cfg(target_os = "none")]
use core::panic::PanicInfo;

#[cfg(target_os = "none")]
use embassy_executor::{Executor, InterruptExecutor, Spawner};
// General-purpose Rust heaps (for BSP/Embassy/our boot). The C++ app keeps its
// own GeneralMemoryAllocator over the region we hand it; the Rust app allocator
// (TLSF/slab, docs/dev/allocator_redesign.md) is a later, separately-gated step.
// Device-only: `deluge-alloc` is a `target_os = "none"`-only dependency (see
// Cargo.toml) — there is no Rust-side heap on host yet.
#[cfg(target_os = "none")]
use deluge_alloc as allocator;

// Link-only: the C++ app (archived into this image by build.rs) calls the
// deluge_resource_* residency C ABI and the deluge_{alloc,slab_*} allocator C ABI.
// `extern crate` forces both rlibs onto the link line so those #[no_mangle]
// symbols resolve; nothing here references them from Rust. deluge_alloc arrives
// transitively via deluge_resource (a distinct crate from the `deluge_alloc`
// aliased above — that one is the sibling deluge-sdk allocator).
#[cfg(target_os = "none")]
extern crate deluge_resource;
/// `host_app` feature: host-side sibling of the above. The host-built C++
/// `deluge_app` object closure (build.rs's `run_host_app`) calls the same
/// deluge_resource_*/deluge_{alloc,slab_*,heap_*} C ABI; `deluge_resource` is an
/// optional dep enabled by `host_app` (see Cargo.toml) so this `extern crate`
/// forces its rlib onto the host link line too.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
extern crate deluge_resource;

// Link-only, same reasoning as the `deluge_resource` pair above: the C++ app's
// parseSampleHeader/parseWaveTableHeader (storage/audio/audio_file_format.cpp) call the
// `deluge_audio_header_*` C ABI, whose sole definition is this crate's `abi.rs` -- there is no
// weak C++ fallback -- so without this `extern crate` rustc/lld would drop the rlib from the link.
#[cfg(target_os = "none")]
extern crate deluge_audio_format;
/// `host_app` feature: host-side sibling of the above — see that `extern crate`'s doc.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
extern crate deluge_audio_format;

// Link-only, same reasoning as the `deluge_resource` pair above. The C++ reader
// (sample_low_level_reader.cpp) CALLS the `deluge_sample_source_*`/`deluge_sample_region_*` C ABI;
// `deluge_sample_source`'s crate (`abi.rs`) is the sole definition of those symbols -- there is no
// C++ fallback in sample_source.cpp -- gated `cfg(any(target_os = "none", feature =
// "host_app"))` to match these two `extern crate` arms exactly. Without this, rustc/lld would never pull
// `deluge_sample_source`'s single-object rlib into the link at all (nothing in this crate's own Rust
// code references it), leaving those C ABI symbols undefined at link time on device/host_app.
#[cfg(target_os = "none")]
extern crate deluge_sample_source;
/// `host_app` feature: host-side sibling of the above — see that `extern crate`'s doc.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
extern crate deluge_sample_source;

// Link-only, same reasoning as the `deluge_sample_source` pair above. Its `abi.rs`
// defines the lifecycle trio of the `deluge_sample_reader_*` C ABI (`include/libdeluge/
// sample_reader.h`), gated identically. Nothing calls it yet (no consumer has migrated to it), so
// without this `extern crate` rustc/lld would drop the whole rlib from the link; this just
// proves the symbols are present and compile clean end-to-end ahead of a real caller being wired up.
#[cfg(target_os = "none")]
extern crate deluge_sample_reader;
/// `host_app` feature: host-side sibling of the above — see that `extern crate`'s doc.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
extern crate deluge_sample_reader;

// Link-only, same reasoning as the `deluge_sample_reader` pair above. Its `abi.rs`
// will define the `deluge_sample_stream_*` C ABI (`include/libdeluge/
// sample_stream.h`). Nothing calls it yet (no consumer has migrated to it), so
// without this `extern crate` rustc/lld would drop the whole rlib from the link; this just proves
// the symbols are present and compile clean end-to-end ahead of the facade wiring a real caller.
#[cfg(target_os = "none")]
extern crate deluge_sample_stream;
/// `host_app` feature: host-side sibling of the above — see that `extern crate`'s doc.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
extern crate deluge_sample_stream;

/// libdeluge POD types generated from include/libdeluge/*.h (types only; the
/// service functions are defined in [`ffi`]). No C++ app is linked on host
/// unless `host_app` is enabled (see build.rs), so there is no bindgen output
/// to include.
#[cfg(target_os = "none")]
#[allow(non_camel_case_types, non_upper_case_globals, dead_code)]
mod sys {
    include!(concat!(env!("OUT_DIR"), "/libdeluge_sys.rs"));
}
/// Host stand-in for the above (no C++ app / bindgen output off-target). Used
/// unless `host_app` is enabled, in which case the real bindgen output below
/// takes over.
#[cfg(all(not(target_os = "none"), not(feature = "host_app")))]
#[path = "sys_host.rs"]
mod sys;
/// `host_app` feature: the real host-ABI bindgen output (see build.rs), for
/// when the host-built C++ `deluge_app` object closure is linked in too.
/// Replaces the `sys_host.rs` stand-ins above with the genuine generated
/// types.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
mod sys {
    #![allow(
        non_upper_case_globals,
        non_camel_case_types,
        non_snake_case,
        dead_code
    )]
    include!(concat!(env!("OUT_DIR"), "/libdeluge_sys.rs"));
}

/// audio_io.h — duplex block audio over the SSI0 DMA rings.
#[cfg(target_os = "none")]
mod audio;
/// audio_io.h — host null-sink render pump: the priority-0 task actually
/// calls `deluge_app_render` and discards the output, instead of the no-op
/// stub in `host_link_stubs.rs`.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
mod audio_host;
/// The async audio-file load task and its request/collect C ABI: owns the AWAITED
/// file I/O of a card sample's load — open a candidate, size it, walk its header
/// — and publishes a plain result the C++ side collects on its own tier. It calls
/// no C++ itself; see its module doc for the two constraints that force that. Compiled
/// on the Embassy BSP unconditionally (device, or host under `host_app`) so
/// `deluge_audio_load_async_active`/`_request`/`_collect` always resolve to this
/// crate's strong symbols rather than `audio_load_abi.cpp`'s weak fallbacks; the driver and the task itself stay gated behind
/// `async_audio_load` — see its module doc's "Two compilation tiers".
#[cfg(any(target_os = "none", feature = "host_app"))]
mod audio_loader;
/// board.h — capability descriptor + GPIO/audio/CV bring-up. Compiled on host
/// too under `host_app` (the descriptor/probe are pure data/logic; the GPIO/CV
/// bring-up bodies get host no-op siblings — see board.rs).
#[cfg(any(target_os = "none", feature = "host_app"))]
mod board;
/// C++ memory-model bring-up (SDRAM bss/data, global ctors).
#[cfg(target_os = "none")]
mod boot_mem;
/// card.h — `deluge_card_flush`, the one ejection hook every loader on this BSP is
/// invalidated through. Gated identically to `audio_loader` above, which it calls into: the
/// symbol has to exist wherever a loader's tables do, and nowhere else (the weak fallback in
/// `card_abi.cpp` covers every other BSP).
#[cfg(any(target_os = "none", feature = "host_app"))]
mod card;
/// control_surface.h — pads/buttons/encoders + LEDs (M2b WIP).
mod control;
/// cv_gate.h — CV/gate outputs + external trigger clock.
#[cfg(target_os = "none")]
mod cv_gate;
/// display.h — main OLED output over deluge_bsp::oled.
mod display;
/// Storage-generic, host-testable core of the efatfs read path
/// (`HandleTable` + `FileContext` detach/reattach + generation guard + fill
/// loop). `efatfs_fs` wraps it with the device statics/mutexes/FFI; host tests
/// and `lens1_vt_sim` drive it directly. Also reachable under `host_app`
/// (any target, independent of `efatfs_streaming`) so `efatfs_host_shim` — the
/// host counterpart of `efatfs_fs` — can reuse it unchanged; see that module's
/// doc.
#[cfg(any(
    all(target_os = "none", feature = "efatfs_streaming"),
    feature = "host_app"
))]
mod efatfs_core;
/// The single-owner `embedded-fatfs` mount — one `FileSystem` behind an
/// async `Mutex`, the only way live code touches the vendored FS. Non-default:
/// `efatfs_streaming` feature. `mount()`/`with_fs()` are invoked from this
/// crate's boot path and the streaming read path once the feature is
/// enabled.
#[cfg(all(target_os = "none", feature = "efatfs_streaming"))]
mod efatfs_fs;
/// Host counterpart of `efatfs_fs` — mounts `embedded-fatfs` over a
/// block device backed by the SD_BUS-guarded async helpers (see that
/// module's doc) so a host harness can measure the real efatfs read path.
/// Test infrastructure only; no device path touched.
#[cfg(feature = "host_app")]
mod efatfs_host_shim;
/// `block_device_driver::BlockDevice<512>` over the real SD driver
/// (`deluge_bsp::sd`), feeding the `BufStream`/`embedded-fatfs` stack —
/// device-only counterpart of `fs_differential`'s host `FileBlockDevice`.
#[cfg(target_os = "none")]
mod fat_block_device;
/// `libdeluge/fault.h`: the board half of the portable pad-grid crash reporter — the
/// blocking PIC transport and the code/stack ranges, both usable from a fault vector.
#[cfg(target_os = "none")]
mod fault;
/// The libdeluge C-ABI service implementations the C++ app calls (stubs).
/// Compiled on host too under `host_app` (bodies are already host-safe).
#[cfg(any(target_os = "none", feature = "host_app"))]
mod ffi;
/// Non-header app/BSP symbols (USB-host globals, FatFS glue, NE10, runtime shims).
/// Compiled on host too under `host_app`; `_sbrk`/`_fini` stay device-only
/// (host glibc/crt provide them) and the linker-boundary stand-ins
/// (`program_stack_*`, `__frunk_*`) are host-only (device gets them from the
/// linker script).
#[cfg(any(target_os = "none", feature = "host_app"))]
mod ffi_extra;
/// The worker fiber: a stackful coroutine for the long synchronous C++ operations
/// that pause via `yield()`. This module is the context-switch primitive.
mod fiber;
/// file_bytes.h — the whole-file byte channel: read, write or rename a small settings file on the
/// storage tier and hand the BYTES to a caller that must not wait on the card. Gated identically to
/// `file_directory` above, and to `card` — which reaches this module's `flush_tables` and so must
/// never be compiled without it. The four settings consumers (runtime feature settings, the MIDI
/// device table, MIDI follow's defaults, performance view) always enter this channel after boot: it
/// has no feature switch.
///
/// Unlike `file_prefix` it borrows nothing from `audio_loader`: reading a whole file and writing one
/// back are not `LoadOps`'s four operations, so it declares its own three-operation trait.
#[cfg(any(target_os = "none", feature = "host_app"))]
mod file_bytes;
/// file_directory.h — the single-slot directory channel: open a directory on the storage tier and
/// hand the ready handle to a caller that must not wait on the card. Gated identically to
/// `file_prefix` below, and to `card` — which reaches this module's `flush_tables` and so must
/// never be compiled without it. It has no feature switch: after boot, every browser listing, SysEx directory
/// request and recording-folder scan off the storage owner opens its directory through it.
///
/// Unlike `file_prefix` it borrows nothing from `audio_loader`: a directory open is not one of
/// `LoadOps`'s four file operations, so it declares its own two-operation trait.
#[cfg(any(target_os = "none", feature = "host_app"))]
mod file_directory;
/// file_prefix.h — the single-slot prefix channel: read the first N bytes of a file on the
/// storage tier and hand them to a caller that must not wait on the card. Gated identically to
/// `audio_loader` above, whose `LoadOps`/`ProdOps` its driver reuses, and to `card` — which
/// reaches this module's `flush_tables` and so must never be compiled without it. `LoadSongUI`'s
/// song-artwork preview is the one caller, and it only enters this channel when
/// `async_file_prefix_active` is on; both of the features are off by default.
#[cfg(any(target_os = "none", feature = "host_app"))]
mod file_prefix;
/// The write channel: request/collect over the recorder's stream-write handles and the whole-file
/// write and read. Compiled unconditionally; the channel has no feature switch.
mod file_write;
/// flash.h — persistent settings flash over deluge_bsp::flash / spibsc.
mod flash;
/// Host-only no-op stubs for the peripheral (MIDI/audio/CV-gate/signals) C ABI
/// under `host_app`. Boot-and-idle never exercises real peripheral I/O on host;
/// the real `audio`/`cv_gate`/`midi`/`signals` modules below stay device-only.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
mod host_link_stubs;
/// midi_io.h — DIN MIDI over deluge_bsp::uart (+ USB-MIDI peripheral, see usb).
#[cfg(target_os = "none")]
mod midi;
/// Does a FINALIZED multi-cluster recording's residency table get sized correctly,
/// and does region index 1+ read back correctly through the region port on THIS target? See its
/// module doc. `host_app`-only.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
mod recorder_finalize_probe;
/// Does a still-recording sample's live read-back resolve on THIS target
/// (`async_streaming_loader` on)? See its module doc. `host_app`-only.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
mod recorder_probe;
/// Streaming-underrun harness: the reusable, thread-agnostic scenario driver
/// (load a real song, start playback, start a concurrent recording, step N audio
/// blocks) — see its module doc. `host_app`-only.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
mod scenario;
/// scheduler.h / OSLikeStuff scheduler_api.h — the cooperative task scheduler,
/// implemented on the Embassy executor (one task per registered Deluge task).
mod scheduler;
/// block_device.h + FatFS diskio — SD card over deluge_bsp::sd.
mod sd;
/// Streaming-underrun harness: packs a real FAT SD image from the golden
/// harness's song/sample corpus for [`scenario`] to load. `host_app`-only.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
mod sd_image;
/// Content-addressed naming/reuse policy for [`sd_image`]'s temp FAT images. Split out so it
/// stays plain-host testable without the `host_app` CMake build (tests/sd_image_path_host.rs).
#[cfg(not(target_os = "none"))]
mod sd_image_path;
/// Real impls of the simplest services (system.h, clock.h, memory.h).
mod services;
/// signals.h — board GPIO signals, battery, MIDI/gate timer.
#[cfg(target_os = "none")]
mod signals;
/// The blocking-I/O seam: a plain non-yielding block on device, and on host a spin driven
/// by a harness-registered progress hook. See the module doc for why host needs the hook.
mod sim_block;
/// The async cluster-fill task (R1) and its selector/wakeup C ABI (R2.1): drains
/// the resource manager's loader queue on this executor, awaiting the SD read
/// instead of running it inline on the fiber as the old C++ `loader::pump()`
/// did. Compiled on the Embassy
/// BSP unconditionally (device, or host under `host_app`) so
/// `deluge_streaming_async_active`/`deluge_streaming_signal_fill` always link;
/// the task itself (spawned below) and the rest of the drain machinery stay
/// gated behind `async_streaming_loader` — see its module doc's "Two
/// compilation tiers".
#[cfg(any(target_os = "none", feature = "host_app"))]
mod streaming_loader;
/// USB device bring-up — USB-MIDI 1.0 peripheral (Deluge → computer).
#[cfg(target_os = "none")]
mod usb;

#[cfg(target_os = "none")]
unsafe extern "C" {
    /// One-time C++ application bring-up (deluge.cpp / app.h). On this BSP it also
    /// runs `registerTasks()`, which spawns one Embassy task per registered Deluge
    /// task through the `scheduler_api.h` C ABI in [`scheduler`]. After this returns
    /// the spawned runners drive the app — there is no `deluge_app_tick` loop.
    fn deluge_app_init(board: *const sys::DelugeBoard);
}
// `host_app` feature: host-side sibling of the device import above. The
// host-built `deluge_app` object closure (build.rs's `run_host_app`)
// exports the exact same symbol; this lets the host `host_app` boot path
// (below) call the real `deluge_app_init` → `registerTasks()` on host too.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
unsafe extern "C" {
    fn deluge_app_init(board: *const sys::DelugeBoard);
}

/// `#[global_allocator]` binding for `extern crate alloc` — required as
/// soon as any dependency needs it, which `fat_block_device.rs`'s
/// `embedded-fatfs` (`alloc` feature) is the first to on this target; nothing
/// else here used `alloc` before. Backed by `fs_alloc::DelugeGlobalAlloc` — the
/// same TLSF heap *implementation* `deluge_resource` uses for the residency
/// engine's C ABI, but a DISTINCT instance/arena (its own `DelugeHeap`, not
/// shared with `deluge_resource`'s). Aliased to the `fs_alloc` crate name to
/// avoid colliding with the sibling deluge-sdk `deluge-alloc` crate
/// (`allocator`, above) — both would otherwise normalize to
/// the identifier `deluge_alloc`.
///
/// NOT YET INITIALISED with a real backing arena (`fs_alloc::DelugeGlobalAlloc
/// ::init` is never called): this registration exists purely so `alloc` has a
/// `GlobalAlloc` impl to bind to at link time, satisfying the whole-stack
/// cross-compile. `deluge_alloc::deluge_alloc`'s null-handle guard makes an
/// allocation through an uninitialised instance return null (not UB), so this
/// is safe to add now without a live heap — but nothing that actually needs
/// `alloc` (e.g. a real on-device `embedded-fatfs` mount) can run correctly
/// until a future task calls `.init()` with a real arena and decides where it
/// comes from (dedicated pool vs. shared with an existing heap).
#[cfg(target_os = "none")]
#[global_allocator]
static FS_ALLOCATOR: fs_alloc::DelugeGlobalAlloc = fs_alloc::DelugeGlobalAlloc::new();

#[cfg(target_os = "none")]
#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    // `extern "C"` boundary: never unwind (panic = "abort" is also set).
    //
    // Print STRAIGHT to the RTT channel rather than through `log::error!`. A panic is
    // exactly the moment the logging stack cannot be trusted to be working: this used
    // `log::error!`, the `log` crate's max level was compiled out in release (see
    // deluge-sdk's workspace `log` dependency), and every panic on the only profile that
    // fits the device was therefore silent. The spin below happens in whatever context
    // panicked, so on the storage-owner fiber it stalls the whole thread-mode executor
    // while audio carries on in its own interrupt executor -- a dead front panel with
    // sound still playing, and not one byte of explanation. `rprintln!` shares the
    // channel the logger sits on but owes it nothing.
    #[cfg(all(feature = "rtt", target_os = "none"))]
    rtt_target::rprintln!("PANIC: {}", _info);
    loop {
        core::hint::spin_loop();
    }
}

#[cfg(target_os = "none")]
static mut EXECUTOR: MaybeUninit<Executor> = MaybeUninit::uninit();

/// Preemptive Embassy executor for audio. Runs the audio
/// render task in a GIC SGI handler at a priority above thread mode but below the
/// µs hard-RT ISRs, so it preempts the cooperative thread executor (a storage
/// `yield()` spin can no longer starve audio) yet is itself preempted by
/// OSTM/MTU2-gate/MIDI. The scheduler routes the priority-0 task here.
#[cfg(target_os = "none")]
static AUDIO_EXEC: InterruptExecutor = InterruptExecutor::new();

/// GIC Software-Generated Interrupt id driving [`AUDIO_EXEC`] (0..=15; SMP-free
/// board, so SGIs are otherwise unused).
#[cfg(target_os = "none")]
const AUDIO_SGI: u8 = 8;
/// Audio SGI GIC priority. Numerically ABOVE the hard-RT IRQs (OSTM=14, UART/MIDI
/// =10, DMAC=13) so they preempt the render, and below PMR (31) so it is
/// forwarded. (Lower number = more urgent.)
#[cfg(target_os = "none")]
const AUDIO_SGI_PRIORITY: u8 = 20;

/// GIC handler for [`AUDIO_SGI`]: drive the audio interrupt executor. Registered
/// in the HAL dispatch (`gic::register`), which already acks (GICC_IAR) before and
/// EOIs (GICC_EOIR) after, with IRQs re-enabled for nesting.
#[cfg(target_os = "none")]
fn audio_sgi_handler() {
    // Mark the audio context for `deluge_in_audio_context()` — the predicate the resource
    // manager's asymmetric critical section keys off. Must wrap the whole poll: every manager
    // access the audio task makes has to be inside it, or the manager will mask on the one
    // path that must not.
    let prev = crate::services::audio_context_enter();
    // SAFETY: only called from the SGI handler, after AUDIO_EXEC.start().
    unsafe { AUDIO_EXEC.on_interrupt() };
    crate::services::audio_context_exit(prev);
}

/// Preemptive Embassy executor for the storage streaming fill task. A second interrupt
/// executor at a priority *between* audio and thread mode, so audio still preempts the fill
/// and the fill preempts thread mode.
///
/// That ordering is the whole point: once the worker fiber's storage waits become
/// non-yielding spins, a spin in thread mode must not be able to monopolise the context that
/// releases the filesystem mutex — which is this task, since it holds that mutex across its
/// SD await (`efatfs_fs::read_at` -> `with_fs`).
#[cfg(target_os = "none")]
static STREAM_EXEC: InterruptExecutor = InterruptExecutor::new();

/// GIC Software-Generated Interrupt id driving [`STREAM_EXEC`]. [`AUDIO_SGI`] is 8; 9 is the
/// next free one (SMP-free board, so SGIs are otherwise unused).
#[cfg(target_os = "none")]
const STREAM_SGI: u8 = 9;
/// Streaming SGI GIC priority. Numerically BELOW [`AUDIO_SGI_PRIORITY`] (20) so audio
/// preempts the fill, above thread mode so the fill preempts the fiber, and below PMR (31) so
/// it is forwarded at all. (Lower number = more urgent.)
#[cfg(target_os = "none")]
const STREAM_SGI_PRIORITY: u8 = 24;

/// GIC handler for [`STREAM_SGI`]: drive the streaming interrupt executor. Registered in the
/// HAL dispatch (`gic::register`), which already acks (GICC_IAR) before and EOIs (GICC_EOIR)
/// after, with IRQs re-enabled for nesting.
///
/// Deliberately does NOT mark the audio context (unlike [`audio_sgi_handler`]): this context
/// is preempted by audio, so its resource-manager accesses must keep masking. See
/// `deluge_resource::sync`'s module doc.
#[cfg(target_os = "none")]
fn stream_sgi_handler() {
    // SAFETY: only called from the SGI handler, after STREAM_EXEC.start().
    unsafe { STREAM_EXEC.on_interrupt() };
}

/// Bootstrap for [`STREAM_EXEC`]: obtain that executor's own `Spawner` from a task already
/// running on it, then spawn the (non-`Send`) storage tasks locally.
///
/// The indirection exists because `InterruptExecutor::start` hands out a `SendSpawner`, while
/// the fill task's future holds a raw `DelugeResource*` and so is not `Send`. This task is
/// `Send` (it owns nothing across its single await), and once it is running ON `STREAM_EXEC`
/// it can spawn onto that executor directly.
///
/// The audio-file load task lands here rather than on the thread-mode executor for the reason
/// its own module doc gives: like the fill task, it holds the single `efatfs` FS mutex across
/// an SD await, and the storage fiber's non-yielding `sim_block::block_on` spin would deadlock
/// against that if both shared an executor. The file-prefix task is here for the same reason
/// and on the same terms.
#[cfg(all(
    target_os = "none",
    any(
        feature = "async_streaming_loader",
        feature = "async_audio_load_active",
        feature = "async_file_prefix_active",
        feature = "efatfs_streaming"
    )
))]
#[embassy_executor::task]
async fn stream_bootstrap() {
    // SAFETY: this task only ever runs on STREAM_EXEC (it is spawned onto it and nowhere
    // else), so the Spawner yielded here belongs to STREAM_EXEC, and the non-Send task
    // spawned with it never leaves the executor it was created on.
    let spawner = unsafe { Spawner::for_current_executor() }.await;
    #[cfg(feature = "async_streaming_loader")]
    spawner.spawn(streaming_loader::streaming_fill_task().unwrap());
    #[cfg(all(feature = "async_audio_load_active", feature = "efatfs_streaming"))]
    spawner.spawn(audio_loader::audio_load_task().unwrap());
    // Here rather than on the thread-mode executor for the same reason as the load task above: it
    // holds the FS mutex across an SD await, and the storage fiber's non-yielding
    // `sim_block::block_on` spin would deadlock against that if the two shared an executor.
    #[cfg(all(feature = "async_file_prefix_active", feature = "efatfs_streaming"))]
    spawner.spawn(file_prefix::file_prefix_task().unwrap());
    // Here for the same reason again: `task_dir_open` goes through `with_fs`, so it holds the FS
    // mutex across an SD await.
    #[cfg(feature = "efatfs_streaming")]
    spawner.spawn(file_directory::file_directory_task().unwrap());
    // And again: every one of `task_file_open`/`_read_exact`/`_write`/`_truncate` and `task_rename`
    // goes through `with_fs`, so this task holds the FS mutex across an SD await too.
    #[cfg(feature = "efatfs_streaming")]
    spawner.spawn(file_bytes::file_bytes_task().unwrap());
    // The recorder's cluster write. Same reason as its neighbours: `stream_write_at` goes through
    // `with_fs`, so this task holds the FS mutex across an SD await.
    #[cfg(feature = "efatfs_streaming")]
    spawner.spawn(file_write::file_write_task().unwrap());
}

/// Firmware entry. The HAL reset handler (`_reset_handler` in rza1l-hal) ends in
/// `bl main`; control lands here with caches/MMU off and stacks set up.
#[cfg(target_os = "none")]
#[unsafe(no_mangle)]
pub extern "C" fn main() -> ! {
    // RTT logger first, so every boot step is visible over the probe. The ring
    // buffer + control block live in uncached SRAM (.rtt_buffer / rza1l_rtt.x).
    #[cfg(all(feature = "rtt", target_os = "none"))]
    {
        let channels = rtt_target::rtt_init! {
            up: { 0: { size: 16384, name: "Terminal", section: ".rtt_buffer" } }
            section_cb: ".rtt_buffer"
        };
        rtt_target::set_print_channel(channels.up.0);
        rtt_target::init_logger_with_level(log::LevelFilter::Debug);
    }
    log::info!("deluge-rust: boot — Rust/Embassy BSP for RZ/A1L");

    // No Rust-side SRAM heap. Nothing here allocates from on-chip SRAM -- the SDK crate that would
    // make `deluge_alloc::SRAM` a global allocator is not linked -- and the 64 KiB static pool it used
    // to be initialised over came straight out of the C++ app's fast on-chip heap, which starts at
    // the image end rounded up to 64 KiB. `deluge_alloc::SRAM` stays uninitialised, so an allocation
    // from it fails rather than landing anywhere; give it a real arena before anything needs it.

    // CPG/PLL, MMU, L1+L2 caches, SDRAM controller, GIC, and the embassy-time
    // (OSTM) driver — the whole platform bring-up, owned by the BSP.
    unsafe { deluge_bsp::system::init_clocks() };
    log::info!("deluge-rust: clocks/MMU/cache/SDRAM/GIC up");

    // SDRAM is up: zero the app's SDRAM .bss and copy .sdram_init from its SRAM
    // load address (the debugger can't write SDRAM at download).
    unsafe { boot_mem::init_sdram_memory() };

    // Rust SDRAM allocator gets the reserved top slice; the app owns the rest
    // (its heap runs __heap_start .. deluge_memory_external_end()).
    unsafe {
        allocator::SDRAM.init(
            boot_mem::RUST_SDRAM_BASE as *mut u8,
            boot_mem::RUST_SDRAM_RESERVE,
        )
    };
    log::info!("deluge-rust: SDRAM memory image ready");

    // Run C++ global constructors before any application code touches globals.
    unsafe { boot_mem::run_init_array() };
    log::info!("deluge-rust: C++ global constructors done");

    // Setup window — GIC source registration, with IRQs still masked:
    //  - PIC co-processor UART (SCIF1) with DMA RX/TX, started at 31250 bps; the
    //    pump task (control::pic_pump) runs the handshake up to 200000 bps.
    //  - The six front-panel quadrature encoders (GPIO + GIC edge IRQs).
    // Both must be configured before the global interrupt enable below.
    unsafe { deluge_bsp::uart::init_pic(31_250) };
    unsafe { deluge_bsp::uart::init_midi(31_250) };
    unsafe { deluge_bsp::encoder::irq_init() };
    // Sample-accurate MIDI/gate one-shot timer (MTU2 ch2). Unlock MTU2 register
    // writes once, then register the compare-match ISR; armed at render time by
    // the app via deluge_midi_gate_timer_arm(). MTU2's module clock is already
    // ungated by init_clocks (STBCR3).
    unsafe { rza1l_hal::mtu2::enable_write() };
    unsafe { signals::gate_timer_setup() };
    // Trigger-clock (analog clock-in): route P1_14 → IRQ6 (falling edge — an
    // on-board transistor inverts the external clock, so an external rising edge
    // is a falling edge here). The app's handler runs from the cooperative
    // trigger_clock_task between app ticks; with no preemption it needs none of
    // the critical-section protection the original ISR path used. See cv_gate.rs.
    unsafe { deluge_bsp::trigger_clock::irq_init() };
    // USB-MIDI 1.0 peripheral (Deluge → computer): bring up USB0 in device mode
    // and build the device now (registers the USB0 GIC handler); its tasks are
    // spawned on the executor below. The GIC line is enabled by the driver when
    // device.run() starts.
    let usb_midi = unsafe { usb::build() };

    // Audio interrupt-executor: register the SGI handler and set its priority
    // (below the hard-RT IRQs, above thread mode). The GIC line is *enabled* only
    // after AUDIO_EXEC.start() below, so audio_sgi_handler can never run before the
    // executor it drives is initialised.
    unsafe {
        rza1l_hal::gic::register(AUDIO_SGI as u16, audio_sgi_handler);
        rza1l_hal::gic::set_priority(AUDIO_SGI as u16, AUDIO_SGI_PRIORITY);
        rza1l_hal::gic::register(STREAM_SGI as u16, stream_sgi_handler);
        rza1l_hal::gic::set_priority(STREAM_SGI as u16, STREAM_SGI_PRIORITY);
    }

    // Verify the worker-fiber context switch in isolation before it drives real
    // operations. Pure + synchronous; logs PASS/FAIL over RTT.
    fiber::selftest();

    // Unmask IRQs so the time driver and peripheral ISRs fire.
    unsafe { cortex_ar::interrupt::enable() };

    // Tell the interrupt-executor SGI pender where the GIC Distributor actually
    // is. The pender otherwise derives it from CBAR/PERIPHBASE, which is only
    // valid on Cortex-A MPCore parts; on this single-core RZ/A1L CBAR reads
    // 0xF000_0000 while the Renesas-integrated GIC Distributor is fixed at
    // 0xE820_1000. Without this the pender writes GICD_SGIR to a bogus address
    // and the audio SGI never fires (silent audio). Must precede AUDIO_EXEC use.
    embassy_executor::set_gicd_base(0xE820_1000);

    // Start the audio interrupt-executor and stash its SendSpawner so the scheduler
    // can spawn the priority-0 (audio) task onto it. Must precede deluge_app_init
    // (in app_task), which calls registerTasks() → addRepeatingTask(priority 0).
    // Enable the SGI in the GIC only now that the executor is initialised.
    scheduler::set_audio_spawner(AUDIO_EXEC.start(AUDIO_SGI));
    unsafe { rza1l_hal::gic::enable(AUDIO_SGI as u16) };

    // Streaming fill executor, started here for the same reason audio is: the GIC line must
    // not be enabled before the executor its handler drives exists. Spawned via
    // `stream_bootstrap` — see that task's doc for why a SendSpawner cannot take the fill task
    // directly.
    // Every task this executor can host is listed, not just the ones that happened to be on when it was
    // written: a channel switched on without its entry here would have its task spawned onto an executor
    // nobody started -- a dead channel that every consumer's owner arm would then hide.
    #[cfg(any(
        feature = "async_streaming_loader",
        feature = "async_audio_load_active",
        feature = "async_file_prefix_active",
        feature = "efatfs_streaming"
    ))]
    STREAM_EXEC
        .start(STREAM_SGI)
        .spawn(stream_bootstrap().unwrap());
    #[cfg(not(any(
        feature = "async_streaming_loader",
        feature = "async_audio_load_active",
        feature = "async_file_prefix_active",
        feature = "efatfs_streaming"
    )))]
    let _ = STREAM_EXEC.start(STREAM_SGI);
    unsafe { rza1l_hal::gic::enable(STREAM_SGI as u16) };

    let executor: &'static mut Executor = unsafe {
        let p = core::ptr::addr_of_mut!(EXECUTOR);
        (*p).write(Executor::new());
        (*p).assume_init_mut()
    };
    executor.run(move |spawner: Spawner| {
        // Stash the spawner so the scheduler's add*Task C-ABI entry points (called
        // synchronously by the C++ app during registerTasks) can spawn task runners.
        scheduler::set_spawner(spawner);
        // The PIC pump decodes pad/button input concurrently with the app's tick
        // loop (which yields each tick, letting these tasks make progress).
        spawner.spawn(control::pic_pump().unwrap());
        // Bridges the encoder edge ISRs to the scheduler: unblocks the app's
        // self-blocking encoder task on movement (else encoders stay dead).
        spawner.spawn(control::encoder_wake_pump().unwrap());
        // The sole PIC transmitter: drains the LED/pad output queue.
        spawner.spawn(control::pad_render().unwrap());
        // The OLED render task: SSD1309 init + frame streaming over RSPI0.
        spawner.spawn(display::oled_render().unwrap());
        // CV DAC writer (shares RSPI0 with the OLED).
        spawner.spawn(cv_gate::cv_writer().unwrap());
        // External trigger-clock: calls the app's clock-in handler once per
        // captured rising edge (cooperatively, between app ticks).
        spawner.spawn(cv_gate::trigger_clock_task().unwrap());
        // USB-MIDI 1.0 peripheral: the device state machine + the two MIDI 1.0
        // endpoint pumps (host↔class byte queues, bridged by midi.rs port 1).
        spawner.spawn(usb::device_task(usb_midi.device).unwrap());
        spawner.spawn(usb::midi_rx_task(usb_midi.ep_out).unwrap());
        spawner.spawn(usb::midi_tx_task(usb_midi.ep_in).unwrap());
        spawner.spawn(app_task().unwrap());
        // Re-identifies the card after a swap. Must be its own task: `sd::init()`
        // needs embassy-time Timers, which the storage-owner fiber cannot provide
        // (see `sd::card_service`), and parking `app_task` on a ~1.3 s card
        // bring-up would stall the worker-fiber pump it owns.
        spawner.spawn(sd::card_service().unwrap());
        // The async cluster-fill task is NOT spawned here any more: it runs on STREAM_EXEC,
        // its own preemptive interrupt executor (see `STREAM_EXEC` and `stream_bootstrap`).
        // A non-yielding storage spin in thread mode must not be able to starve it.
    });
}

/// The application bring-up task. Runs the C++ app's one-time init, which on this
/// BSP also calls `registerTasks()` → spawns one Embassy task per registered
/// Deluge task via the [`scheduler`] C ABI. Those runners (plus the BSP's async
/// I/O tasks) then drive everything cooperatively, so this task has nothing left
/// to do and parks forever. The decomposed scheduler replaces the old yielding
/// `deluge_app_tick` superloop.
#[cfg(target_os = "none")]
#[embassy_executor::task]
async fn app_task() {
    use embassy_time::Timer;
    use rza1l_hal::gpio;

    // M1a sign-of-life: blink the SYNC LED (P6.7). Visible on the panel and
    // proves the executor, time driver and GPIO all work end-to-end.
    const SYNC_LED_PORT: u8 = 6;
    const SYNC_LED_BIT: u8 = 7;
    // SAFETY: we own this pin; GPIO clocks are up after init_clocks.
    unsafe { gpio::set_as_output(SYNC_LED_PORT, SYNC_LED_BIT) };
    for i in 0..6 {
        unsafe { gpio::write(SYNC_LED_PORT, SYNC_LED_BIT, true) };
        Timer::after_millis(120).await;
        unsafe { gpio::write(SYNC_LED_PORT, SYNC_LED_BIT, false) };
        Timer::after_millis(120).await;
        log::info!("deluge-rust: alive ({})", i);
    }

    // Bring SD up BEFORE deluge_app_init so the app's boot settings read finds the
    // card ready (no "SD CARD ERROR" popup). But first wait for the PIC's baud-rate
    // handshake (31250→200000, run by pic_pump) to finish: sd::init() racing that
    // handshake corrupts it → garbled pads. With the PIC already ready, sd::init has
    // nothing to race. Must be async — sd::init's embassy-time Timers can't run under
    // block_on (integrated timer queue).
    deluge_bsp::pic::wait_ready().await;
    crate::sd::boot_init().await;

    // With `efatfs_streaming` enabled (the default): give the
    // FS allocator a real backing arena, then mount the single-owner
    // embedded-fatfs `FileSystem` — BEFORE `deluge_app_init` so the FS is ready
    // for the first C++ sample-load. A failed mount must NOT brick boot — but note
    // the streaming read is efatfs-only with no fallback filesystem, so a
    // failed mount means streamed samples won't load (open_read_stream fails).
    // The SD block driver is already up (boot_init above) and nothing has
    // touched the card yet.
    #[cfg(feature = "efatfs_streaming")]
    {
        // Backing arena for `crate::FS_ALLOCATOR` (see its doc): embedded-fatfs's
        // `alloc` feature needs a live heap before its first allocation (LFN
        // directory-scan scratch, handle-table strings). 96 KiB — plain `.bss`,
        // well within the RZ/A1L's on-chip SRAM.
        const EFATFS_ARENA_SIZE: usize = 96 * 1024;
        static mut EFATFS_ARENA: [u8; EFATFS_ARENA_SIZE] = [0; EFATFS_ARENA_SIZE];

        // SAFETY: `EFATFS_ARENA` is a function-local static this block alone ever
        // touches; this runs at most once (app_task runs once). `addr_of_mut!` +
        // `size_of_val(&*p)` (not `&mut EFATFS_ARENA` directly) avoids a
        // `static_mut_refs` reference to the static itself.
        let (base, size) = unsafe {
            let p = core::ptr::addr_of_mut!(EFATFS_ARENA);
            (p.cast::<u8>(), core::mem::size_of_val(&*p))
        };
        // SAFETY: `base`/`size` describe that same live, exclusively-owned
        // 'static arena; `deluge_heap_create` only ever writes within
        // `[base, base+size)`.
        let handle = unsafe { fs_alloc::deluge_heap_create(base, size) };
        assert!(
            !handle.is_null(),
            "efatfs: {EFATFS_ARENA_SIZE}-byte arena too small for a DelugeHeap control block"
        );
        crate::FS_ALLOCATOR.init(handle);
        log::info!(
            "efatfs: FS_ALLOCATOR initialised over a {} KiB arena",
            EFATFS_ARENA_SIZE / 1024
        );

        match crate::efatfs_fs::mount().await {
            Ok(()) => log::info!("efatfs: mounted"),
            Err(()) => log::warn!("efatfs: mount failed — streamed samples will not load"),
        }
    }

    log::info!("deluge-rust: deluge_app_init() (registers + spawns task runners)");
    // deluge_app_init → registerTasks() spawns the per-task runners onto this
    // executor via scheduler::set_spawner's stashed spawner. They begin running as
    // soon as we yield below.
    unsafe { deluge_app_init(board::deluge_board()) };
    log::info!("deluge-rust: scheduler running; pumping async worker");

    // The scheduler's task runners own the periodic app work. This task now pumps
    // the worker fiber ([`fiber`]): it drives the long user-initiated operations
    // (song load, stem export, grid clip create) that `yield()` instead of
    // busy-waiting. Each pass starts/resumes a ready operation; between passes the
    // executor runs every other task + I/O, so the awaited work makes progress
    // (this is what lets song-load-while-playing complete instead of hanging on a
    // frozen executor).
    //
    // Wake-driven: sleep on WORKER_WAKE rather than polling on a fixed tick. While
    // an op is suspended a coarse fallback timer also re-checks (covers predicate
    // flips that aren't signalled — most are, via deluge_worker_run, the task
    // runners, and the SD waker); when idle we sleep until an op is submitted.
    use embassy_futures::select::select;
    loop {
        let busy = fiber::worker_poll();
        if busy {
            let _ = select(
                fiber::WORKER_WAKE.wait(),
                embassy_time::Timer::after_millis(8),
            )
            .await;
        } else {
            fiber::WORKER_WAKE.wait().await;
        }
    }
}

/// `host_app` feature: host sibling of [`app_task`] above — the real C++ app's
/// one-time bring-up, run on the host executor spawned by `fn main`'s
/// `host_app` boot path. Mirrors the device sequencing exactly (wait for the
/// PIC handshake, bring SD up, then `deluge_app_init`), minus the device-only
/// SYNC LED blink (no GPIO on host).
///
/// After init, this runs the SAME worker-fiber pump loop as the device
/// [`app_task`] (see its doc comment for the full rationale) rather than
/// parking. The loop is needed here too: it drives the worker ring, so
/// owner-dispatched storage work actually runs. Coalesced owner ops (the
/// recorder card-write drain, card re-init) enqueue onto that ring; without the
/// loop draining it, a `Coalescer`'s single-flight guard latches
/// `in_flight_ = true` on its first dispatch and never releases
/// (`run_and_release` never runs), so every later request silently no-ops. It
/// is also what lets a real streaming cluster read (once one is queued — see
/// the `sim_latency`/streaming-underrun harness) genuinely reach the fiber and,
/// under `sim_latency`, suspend it.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
#[embassy_executor::task]
async fn host_app_task() {
    // Mirrors the device `app_task`: wait for the PIC's (host: synthetic)
    // ready handshake, then bring SD up, before the app's first storage access.
    deluge_bsp::pic::wait_ready().await;
    crate::sd::boot_init().await;

    // Host counterpart of `app_task`'s efatfs mount (see its comment) —
    // mount the shim's embedded-fatfs `FileSystem` BEFORE `deluge_app_init` so
    // the app's first sample-load can open an efatfs handle. No FS_ALLOCATOR
    // arena needed here (unlike the device): `efatfs_host_shim.rs`'s module doc
    // notes embedded-fatfs's `alloc` feature just uses the host's implicit std
    // allocator. A failed mount must NOT abort boot — but the streaming read
    // is efatfs-only, so a failed mount means streamed samples won't load
    // rather than reverting to C-FatFS, exactly like the device. No `sim_latency::set_off_fiber_instant`
    // dance is needed on this path either: this mount's block device awaits the
    // SD_BUS-guarded async helpers directly (see the shim's module doc) rather
    // than bridging through a synchronous off-fiber dispatch that would need
    // that workaround, and in any case `sim_latency::pump` runs on the
    // dedicated audio OS thread (spawned above in `main`, before this task),
    // so this task's `block_on` busy-spin never starves it (see Appendix A of
    // the R0 design doc for why Lens 1's single-threaded shape is different).
    #[cfg(feature = "efatfs_streaming")]
    match crate::efatfs_host_shim::mount().await {
        Ok(()) => log::info!("efatfs: mounted (host)"),
        Err(()) => log::warn!("efatfs: host mount failed — streaming falls back to C FatFS"),
    }

    log::info!("deluge-bsp-rust: host deluge_app_init() (registers + spawns task runners)");
    // deluge_app_init → registerTasks() spawns the per-task runners onto this
    // executor via scheduler::set_spawner's stashed spawner. They begin running
    // as soon as we yield below.
    unsafe { deluge_app_init(board::deluge_board()) };
    log::info!("deluge-bsp-rust: host scheduler running; pumping async worker");

    // Same wake-driven pump shape as the device `app_task` — see there for the
    // full rationale (busy: race WORKER_WAKE against a coarse 8ms fallback;
    // idle: sleep on WORKER_WAKE alone).
    use embassy_futures::select::select;
    loop {
        let busy = fiber::worker_poll();
        if busy {
            let _ = select(
                fiber::WORKER_WAKE.wait(),
                embassy_time::Timer::after_millis(8),
            )
            .await;
        } else {
            fiber::WORKER_WAKE.wait().await;
        }
    }
}

/// Host harness entry (`cargo build`/`cargo test` off-target, no `target_os =
/// "none"`). Exercises the core host modules (`fiber`, `scheduler`, `sd`,
/// `services`) on std without any device BSP/HAL — no MMU/GIC/SDRAM bring-up.
/// Proves the host binary actually links and runs: the fiber context switch
/// works on a std thread stack, and the `sd.rs` file-backed block-device shim
/// round-trips real bytes through the same `deluge_block_read`/
/// `deluge_block_write` C ABI the app would call. With `host_app` off, no C++
/// app is linked (see build.rs); with it on, this also boots the real C++ app
/// (see the `host_app`-gated blocks below).
#[cfg(not(target_os = "none"))]
fn main() {
    env_logger::init();
    log::info!("deluge-bsp-rust: HOST harness");
    // Run the fiber selftest.
    assert!(fiber::selftest(), "fiber selftest failed on host");

    // sd.rs host shim round-trip: write a known sector via the C-ABI
    // deluge_block_write, read it back via deluge_block_read, and verify the
    // bytes survive — proves the file-backed shim actually persists data, not
    // just that it links. Sector 1 (not 0): leaves a notional boot sector alone.
    //
    // Deliberately called here, synchronously, before any executor/fiber
    // exists — it is a bootstrap-time smoke test of the raw ABI shim itself,
    // not an app FatFS access, so it has no owner to route through yet. Under
    // `storage-owner-audit`'s pre-flight gate this does NOT
    // trip `sd.rs`'s `on_fiber()` debug_assert!: the assert's guard is
    // `on_fiber() || !worker_started()`, and `worker_started()` only latches
    // true once the first `worker_poll()` runs, which is after this
    // synchronous self-test returns — so it runs unconditionally in both
    // configs.
    //
    // `sim_latency`-on only: SKIPPED here instead. Under `sim_latency`,
    // `deluge_block_write`/`deluge_block_read` route through
    // `sim_latency::modeled_write`/`modeled_read` (see sd.rs's module doc),
    // which pends on `sim_latency::pump` — a genuinely-spawned Embassy task —
    // to resolve the modeled delay. No executor exists yet at this point in
    // `fn main()`, so nothing could ever spawn `pump`, and the off-fiber
    // `block_on` below would busy-spin forever waiting on a modeled transfer
    // nobody services. The same round trip (write + read, byte-for-byte data
    // assertion) stays covered under `sim_latency` by
    // `tests/sim_latency_host.rs`'s exercise, which brings up a real executor
    // with `sim_latency::pump` running before issuing any transfer.
    //
    // Also SKIPPED whenever `host_app` is on. This write is destructive
    // (it clobbers sector 1 of WHATEVER image `DELUGE_SD_IMAGE` names) and, on
    // a `host_app` build, that can be a real, externally-supplied FAT image —
    // e.g. `preemptive_race_tsan/run.sh`'s own documented, recommended
    // `DELUGE_SD_IMAGE=<cached image>` workflow (its header comment: reusing an
    // already-packed image is "100% reliable", vs. packing fresh in-process).
    // This self-test's synthetic byte pattern lands on FAT32's FSInfo sector,
    // which C-FatFS's `f_mount`
    // tolerates (it just re-derives the free-cluster count) but
    // `embedded-fatfs`'s stricter mount validation rejects outright
    // (`CorruptedFileSystem`) — so enabling `efatfs_streaming` on this path
    // surfaced a pre-existing landmine that C-FatFS-only builds never
    // triggered. Under `host_app`, the real app's own boot (FatFS/efatfs mount
    // + whatever scenario runs) already exercises the same
    // `deluge_block_write`/`deluge_block_read` ABI end-to-end with real data,
    // making this synthetic smoke test both redundant and unsafe there.
    #[cfg(any(feature = "sim_latency", feature = "host_app"))]
    log::info!(
        "deluge-bsp-rust: sd round-trip SKIPPED ({})",
        if cfg!(feature = "host_app") {
            "host_app is on — this write is destructive and DELUGE_SD_IMAGE may name a \
             real caller-supplied FAT image; the app's own boot exercises the same ABI"
        } else {
            "sim_latency has no executor/pump yet at this bootstrap point — see \
             tests/sim_latency_host.rs for the covered equivalent"
        }
    );
    #[cfg(not(any(feature = "sim_latency", feature = "host_app")))]
    {
        const TEST_SECTOR: u32 = 1;
        let mut pattern = [0u8; 512];
        for (i, b) in pattern.iter_mut().enumerate() {
            *b = (i as u8).wrapping_mul(31).wrapping_add(7);
        }
        let status = sd::deluge_block_write(0, pattern.as_ptr(), TEST_SECTOR, 1);
        assert_eq!(
            status, 0,
            "sd round-trip: deluge_block_write failed (status={status})"
        );
        let mut readback = [0u8; 512];
        let status = sd::deluge_block_read(0, readback.as_mut_ptr(), TEST_SECTOR, 1);
        assert_eq!(
            status, 0,
            "sd round-trip: deluge_block_read failed (status={status})"
        );
        assert_eq!(
            pattern, readback,
            "sd round-trip: readback did not match what was written"
        );
        log::info!("deluge-bsp-rust: sd round-trip OK (sector {TEST_SECTOR}, 512 bytes)");
    }

    // --- Whole-BSP host boot smoke (no C++ app; `host_app` OFF) ------------
    // Bring up a host Embassy executor (platform-std) and spawn the four
    // control/display tasks — the same ones main.rs spawns on device (minus the
    // app/audio/cv/usb tasks, which need the C++ app or real peripherals). With
    // no app and no input, each runs its init sequence then parks:
    //   pic_pump       → pic::init(); loops on pic::read_byte() (host: parks)
    //   pad_render     → pic::wait_ready(); OUT.receive() (parks, nothing queued)
    //   encoder_wake_pump → parks on ENCODER_WAKER (host: never fires)
    //   oled_render    → pic::wait_ready(); oled::init(); sends ONE blank frame
    //                    (captured by the oled host sim), then parks on wait_redraw
    // Boot is proven by the captured blank frame; the watchdog bounds it so a
    // regression (init that blocks/crashes) fails instead of hanging forever.
    #[cfg(not(feature = "host_app"))]
    {
        use embassy_executor::{Executor, Spawner};
        use std::time::{Duration, Instant};

        std::thread::Builder::new()
            .name("deluge-bsp-boot".into())
            .spawn(|| {
                let executor: &'static mut Executor = Box::leak(Box::new(Executor::new()));
                executor.run(|spawner: Spawner| {
                    crate::scheduler::set_spawner(spawner);
                    spawner.spawn(control::pic_pump().unwrap());
                    spawner.spawn(control::pad_render().unwrap());
                    spawner.spawn(control::encoder_wake_pump().unwrap());
                    spawner.spawn(display::oled_render().unwrap());
                });
            })
            .expect("spawning the host BSP executor thread");

        // Wait (bounded) for oled_render to capture its first (blank) frame.
        let deadline = Instant::now() + Duration::from_secs(10);
        loop {
            // A blank frame is all-zero; captured_frame() returns the last frame the
            // oled sim received. Before the first send it is the sim's initial state;
            // we detect "boot reached first send" via a distinct signal below.
            if deluge_bsp::oled::boot_frame_captured() {
                break;
            }
            if Instant::now() >= deadline {
                panic!("host BSP boot: oled_render did not capture its blank frame in time");
            }
            std::thread::sleep(Duration::from_millis(5));
        }
        log::info!("deluge-bsp-rust: whole-BSP host boot OK (control/display tasks quiescent)");
    }

    // --- Host app boot-and-idle smoke (`host_app` ON) ----------------------
    // Same host executor-on-a-thread shape as the whole-BSP smoke above, but this time
    // also spawn a host `app_task` that runs the REAL C++ app's one-time
    // bring-up (`deluge_app_init` → `deluge_boot` + `registerTasks()` +
    // `encoders::init()`, mirroring the device `app_task` in `main`, above).
    // `registerTasks()` calls `addRepeatingTask`/`addConditionalTask` a couple
    // dozen times, spawning one Embassy task runner per Deluge task via
    // `scheduler::set_spawner`'s stashed spawner — so "at least one scheduler
    // slot claimed" is the strongest cheap, real-app-driven signal that boot
    // reached the scheduler (not just BSP init); `oled_render`'s captured first
    // frame (same signal the whole-BSP smoke above uses) is the fallback.
    #[cfg(feature = "host_app")]
    {
        use embassy_executor::{Executor, Spawner};
        use std::sync::{Arc, Barrier};
        use std::time::{Duration, Instant};

        // --- Streaming-underrun harness: opt-in scenario mode --------------
        // Off by default (env var unset) — the boot-and-idle smoke below is byte-for-
        // byte unchanged from before this task. Set DELUGE_STREAMING_SCENARIO_SONG
        // (e.g. "SONGS/Cordae.XML") to switch this run into the scenario: pack a real
        // FAT SD image from the golden harness's corpus (unless DELUGE_SD_IMAGE is
        // already set, in which case that image is used as-is — must already contain
        // the requested song), load it, start real-time playback + a concurrent
        // output recording, and step until DELUGE_STREAMING_SCENARIO_BLOCKS audio
        // blocks (default 500) have rendered. This MUST run before anything below
        // touches SD (the audio thread spawn is SD-inert, but `host_app_task` mounts
        // the card as soon as it starts) — see sd_image.rs's module doc.
        let scenario_cfg = std::env::var("DELUGE_STREAMING_SCENARIO_SONG")
            .ok()
            .map(|song| {
                if std::env::var_os("DELUGE_SD_IMAGE").is_none() {
                    let fixture = std::env::var("DELUGE_STREAMING_SCENARIO_FIXTURE")
                        .unwrap_or_else(|_| "cordae".to_string());
                    let repo_root = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
                        .ancestors()
                        .nth(3)
                        .expect("CARGO_MANIFEST_DIR (src/bsp/rust) has a repo root 3 levels up")
                        .to_path_buf();
                    let img = crate::sd_image::pack_golden_fixture(&repo_root, &fixture);
                    // SAFETY: called before any thread below is spawned (no concurrent
                    // env access yet) — same precondition sim_latency_host_exercise.rs
                    // documents for its own DELUGE_SD_IMAGE set_var.
                    unsafe { std::env::set_var("DELUGE_SD_IMAGE", &img) };
                }
                let target_blocks = std::env::var("DELUGE_STREAMING_SCENARIO_BLOCKS")
                    .ok()
                    .and_then(|s| s.parse().ok())
                    .unwrap_or(500u64);
                // Widen the per-step poll budget for a manual demo run (e.g. a very long
                // `DELUGE_STREAMING_SCENARIO_BLOCKS` window), default unchanged.
                let step_timeout_secs = std::env::var("DELUGE_STREAMING_SCENARIO_STEP_TIMEOUT_S")
                    .ok()
                    .and_then(|s| s.parse().ok())
                    .unwrap_or(20u64);
                // Optional `sd::sim_latency` override, applied by `scenario::run` right
                // after song load completes (see `ScenarioConfig::post_load_sim_latency`'s
                // doc comment for why post-load, not pre-load) — lets a manual `cargo run`
                // dial the modeled SD latency from "trivially fast" (unset — no effect) to
                // "absurdly slow" without a rebuild, to demonstrate the
                // `deluge_sim_underrun_*_count()` counters are wired to real fill-vs-drain
                // behaviour. No effect unless the `sim_latency` feature is enabled.
                #[cfg(feature = "sim_latency")]
                let post_load_sim_latency = {
                    let bps = std::env::var("DELUGE_SIM_LATENCY_THROUGHPUT_BPS")
                        .ok()
                        .and_then(|s| s.parse::<u32>().ok());
                    let us = std::env::var("DELUGE_SIM_LATENCY_OVERHEAD_US")
                        .ok()
                        .and_then(|s| s.parse::<u32>().ok());
                    match (bps, us) {
                        (None, None) => None,
                        (b, u) => {
                            // Defaults mirror `sd::sim_latency`'s own un-overridden constants
                            // (20MB/s throughput, 500us overhead) so setting only one of the
                            // two env vars still produces a sane pair.
                            let bps = b.unwrap_or(20_000_000);
                            let us = u.unwrap_or(500);
                            log::info!(
                                "deluge-bsp-rust: post-load sim_latency override = {bps} bytes/sec, {us} us overhead"
                            );
                            Some((bps, us))
                        }
                    }
                };
                #[cfg(not(feature = "sim_latency"))]
                let post_load_sim_latency = None;
                crate::scenario::ScenarioConfig {
                    song_full_path: Box::leak(song.into_boxed_str()),
                    target_blocks,
                    step_timeout: embassy_time::Duration::from_secs(step_timeout_secs),
                    post_load_sim_latency,
                    // R5a Phase 0 Task 4's contention knob — not wired to an env var here.
                    // This binary's manual `cargo run` scenario path isn't the sweep
                    // instrument Task 5 automates (that's `lens1_vt_sim`, which DOES wire
                    // it — see its `LENS1_CONCURRENT_LISTING_EVERY_BLOCKS`); `None` keeps
                    // this call site's existing behaviour exactly as it was.
                    concurrent_listing_every_blocks: None,
                }
            });

        // --- Opt-in recorder live-readback probe ----------------------------
        // Off by default. Set DELUGE_RECORDER_PROBE=1 to switch this run into the diagnostic:
        // construct a real SampleRecorder, feed it audio, and probe whether a still-recording
        // sample's data can be read back through the region port on THIS target — see
        // recorder_probe.rs's module doc. Mutually exclusive with the streaming-underrun
        // scenario above in practice (both would work concurrently, but nothing exercises them
        // together) — this task only needs the boot-ready signal, not a loaded song.
        let recorder_probe_requested = std::env::var("DELUGE_RECORDER_PROBE").as_deref() == Ok("1");
        let recorder_probe_step_timeout_ms: u64 =
            std::env::var("DELUGE_RECORDER_PROBE_STEP_TIMEOUT_MS")
                .ok()
                .and_then(|s| s.parse().ok())
                .unwrap_or(20_000);
        let recorder_probe_poll_window_ms: u64 =
            std::env::var("DELUGE_RECORDER_PROBE_POLL_WINDOW_MS")
                .ok()
                .and_then(|s| s.parse().ok())
                .unwrap_or(5_000);

        // --- Opt-in finalized multi-cluster regression probe -----------------
        // Off by default. Set DELUGE_RECORDER_FINALIZE_PROBE=1 to switch this run into the
        // regression gate: construct a real SampleRecorder, drive it to RecorderStatus::COMPLETE,
        // and confirm the residency table was sized correctly + region index 1 reads back
        // correctly through the region port on THIS target — see recorder_finalize_probe.rs's
        // module doc. Mutually exclusive with the other opt-in probes/scenarios in practice.
        let recorder_finalize_probe_requested =
            std::env::var("DELUGE_RECORDER_FINALIZE_PROBE").as_deref() == Ok("1");
        let recorder_finalize_probe_step_timeout_ms: u64 =
            std::env::var("DELUGE_RECORDER_FINALIZE_PROBE_STEP_TIMEOUT_MS")
                .ok()
                .and_then(|s| s.parse().ok())
                .unwrap_or(20_000);
        let recorder_finalize_probe_poll_window_ms: u64 =
            std::env::var("DELUGE_RECORDER_FINALIZE_PROBE_POLL_WINDOW_MS")
                .ok()
                .and_then(|s| s.parse().ok())
                .unwrap_or(5_000);

        // --- Second host executor thread for the audio task ----------------
        // Device routes the priority-0 (audio) task onto `AUDIO_EXEC`, a
        // preemptive GIC-SGI interrupt-executor (see `main`, above), so it runs
        // concurrently with — and can preempt — the main thread executor. Host
        // has no interrupt context to stand in for that, so a dedicated
        // `std::thread` running its own platform-std `Executor` is the host
        // analogue: real OS-thread preemption instead of an SGI, but the same
        // "audio is not cooperatively scheduled alongside everything else"
        // property (races enumerated under TSan — this task only needs a
        // functional two-thread boot).
        //
        // Ordering barrier: exactly like the device (`set_audio_spawner` is
        // called *before* the main executor's closure spawns `app_task`, which
        // reaches `registerTasks()`), the main thread here must not let
        // `host_app_task` call `deluge_app_init` until `scheduler::AUDIO_SPAWNER`
        // is `Some` — otherwise `scheduler::claim`'s `use_audio` check races
        // `registerTasks()`'s `addRepeatingTask(priority 0)` and the audio task
        // silently falls back to the cooperative main-thread spawner instead of
        // routing to this thread. A two-party `Barrier` makes the rendezvous
        // synchronous: the audio thread reaches its side immediately after
        // `set_audio_spawner`, the main thread reaches its side immediately
        // before spawning the host-app executor thread below.
        // This host runs audio, streaming and the app on three real OS threads, so a `Pending`
        // storage future here is satisfied by ANOTHER THREAD rather than by nothing —
        // `streaming_fill_task` holds the FS mutex across its SD await on its own thread, so
        // cross-thread contention is the normal case. Install the yielding hook before any
        // thread can issue a transfer, or `sim_block::block_on`'s default no-hook verdict would
        // call a normally-contended read a wedge (its budget is under a millisecond of
        // spinning). See `sim_block::use_threaded_host_hook` for the trade this accepts.
        crate::sim_block::use_threaded_host_hook();

        let audio_spawner_ready = Arc::new(Barrier::new(2));
        let audio_thread_barrier = Arc::clone(&audio_spawner_ready);
        std::thread::Builder::new()
            .name("deluge-audio".into())
            .spawn(move || {
                let executor: &'static mut Executor = Box::leak(Box::new(Executor::new()));
                executor.run(|spawner: Spawner| {
                    // sim_latency harness only: spawned HERE — on this separate OS
                    // thread's executor, not the host-app executor's — deliberately.
                    // `deluge_app_init` (called synchronously, off-fiber, from
                    // `host_app_task` below) can itself issue a `sim_latency`-modeled
                    // transfer (e.g. the boot-time FatFS mount read) via a plain
                    // `block_on`, which hijacks its OS thread with a busy poll loop
                    // and never yields back to that thread's executor — so a `pump`
                    // spawned on the SAME (host-app) executor would never get polled
                    // and the transfer would spin forever. `pump` only touches
                    // cross-thread-safe primitives (`Signal`/`AtomicWaker` over
                    // `CriticalSectionRawMutex`, `embassy_time::Timer` off the shared
                    // std time driver — see sd.rs's module doc), so running it on this
                    // independent thread lets it keep making progress while the
                    // host-app thread is busy-spinning.
                    #[cfg(feature = "sim_latency")]
                    spawner.spawn(crate::sd::sim_latency::pump().unwrap());
                    crate::scheduler::set_audio_spawner(spawner.make_send());
                    log::info!(
                        "deluge-bsp-rust: host audio executor up on thread {:?} — set_audio_spawner done",
                        std::thread::current().name()
                    );
                    // Release the main thread, which was waiting on this before
                    // proceeding to deluge_app_init. `Executor::run`'s closure
                    // then returns and the executor blocks polling forever —
                    // once `registerTasks()` spawns the priority-0 task onto the
                    // stashed `SendSpawner` it runs right here, on this thread.
                    audio_thread_barrier.wait();
                });
            })
            .expect("spawning the host audio executor thread");

        // Do not proceed to spawn the host-app executor (whose `host_app_task`
        // calls `deluge_app_init`) until the audio thread has stashed its
        // spawner.
        audio_spawner_ready.wait();

        // --- Third host executor thread for the streaming fill task ------------
        // Device runs the fill task on `STREAM_EXEC`, a preemptive interrupt executor at a
        // priority between audio and thread mode. Host has no interrupt context, so a
        // dedicated `std::thread` with its own executor is the analogue — real OS-thread
        // preemption instead of an SGI, with the property that matters preserved: a
        // non-yielding storage spin on the app thread cannot starve the fill task, which
        // holds the FS mutex across its SD await.
        //
        // Mutual exclusion needs nothing new. `deluge_in_audio_context()` is always false on
        // host, so every thread — this one included — masks, and the global
        // critical-section mutex serializes all three (see `deluge_resource::sync`'s module
        // doc).
        //
        // Same barrier rationale as the audio thread above: the loader queue must have an
        // owner before `host_app_task` reaches `deluge_app_init`, or the enqueues its boot
        // performs sit undrained until some later `FILL_WAKE` happens to arrive.
        //
        // "Owner exists" is all the barrier buys, and all it should: the fill task's first act
        // is to wait on `FILL_WAKE`, deliberately BEFORE constructing `ProdOps`, because that
        // constructor reads app state `deluge_app_init` is still writing. See
        // `streaming_fill_task`'s doc — an earlier version of this thread raced
        // `Cluster::set_size` exactly that way, which is what TSan caught.
        //
        // The audio-file load task shares this thread for the same reason it shares
        // `STREAM_EXEC` on device: it too holds the FS mutex across an SD await, so it must not
        // live where a non-yielding storage spin could starve it. See `audio_loader`'s module
        // doc.
        #[cfg(any(
            feature = "async_streaming_loader",
            feature = "async_audio_load_active",
            feature = "async_file_prefix_active"
        ))]
        {
            let stream_ready = Arc::new(Barrier::new(2));
            let stream_barrier = Arc::clone(&stream_ready);
            std::thread::Builder::new()
                .name("deluge-stream".into())
                .spawn(move || {
                    let executor: &'static mut Executor = Box::leak(Box::new(Executor::new()));
                    executor.run(|spawner: Spawner| {
                        #[cfg(feature = "async_streaming_loader")]
                        spawner.spawn(streaming_loader::streaming_fill_task().unwrap());
                        #[cfg(feature = "async_audio_load_active")]
                        spawner.spawn(audio_loader::audio_load_task().unwrap());
                        #[cfg(feature = "async_file_prefix_active")]
                        spawner.spawn(file_prefix::file_prefix_task().unwrap());
                        spawner.spawn(file_directory::file_directory_task().unwrap());
                        spawner.spawn(file_bytes::file_bytes_task().unwrap());
                        spawner.spawn(file_write::file_write_task().unwrap());
                        log::info!(
                            "deluge-bsp-rust: host streaming executor up on thread {:?}",
                            std::thread::current().name()
                        );
                        stream_barrier.wait();
                    });
                })
                .expect("spawning the host streaming executor thread");
            stream_ready.wait();
        }

        std::thread::Builder::new()
            .name("deluge-bsp-host-app".into())
            .spawn(move || {
                let executor: &'static mut Executor = Box::leak(Box::new(Executor::new()));
                executor.run(|spawner: Spawner| {
                    crate::scheduler::set_spawner(spawner);
                    spawner.spawn(control::pic_pump().unwrap());
                    spawner.spawn(control::pad_render().unwrap());
                    spawner.spawn(control::encoder_wake_pump().unwrap());
                    spawner.spawn(display::oled_render().unwrap());
                    // NOTE: `sim_latency::pump` is deliberately NOT spawned on this
                    // executor — see the audio-thread executor closure above for why
                    // (this thread's `host_app_task` calls `deluge_app_init`
                    // synchronously, off-fiber, which can itself busy-spin a
                    // `block_on`'d sim_latency transfer and would starve a
                    // same-thread `pump`).
                    spawner.spawn(host_app_task().unwrap());
                    // The async cluster-fill task is NOT spawned here any more: it owns the
                    // `"deluge-stream"` thread above, mirroring the device's STREAM_EXEC. A
                    // non-yielding storage spin on THIS thread — the one `host_app_task`'s
                    // worker-fiber pump loop and the C++ enqueue path run on — must not be
                    // able to starve it.
                    // Streaming-underrun harness: spawned on THIS executor —
                    // the same one `host_app_task`'s worker-fiber pump loop runs on —
                    // so `scenario::run`'s C-ABI calls interleave cooperatively with
                    // the real app's own task graph, exactly like a real HID event
                    // handler would (see scenario.rs's module doc: it never spawns a
                    // thread itself).
                    if let Some(cfg) = scenario_cfg {
                        spawner.spawn(crate::scenario::scenario_task(cfg).unwrap());
                    }
                    if recorder_probe_requested {
                        spawner.spawn(
                            crate::recorder_probe::recorder_probe_task(
                                recorder_probe_step_timeout_ms,
                                recorder_probe_poll_window_ms,
                            )
                            .unwrap(),
                        );
                    }
                    if recorder_finalize_probe_requested {
                        spawner.spawn(
                            crate::recorder_finalize_probe::recorder_finalize_probe_task(
                                recorder_finalize_probe_step_timeout_ms,
                                recorder_finalize_probe_poll_window_ms,
                            )
                            .unwrap(),
                        );
                    }
                });
            })
            .expect("spawning the host app executor thread");

        // Bounded watchdog: boot must never hang forever. ~20s is generous for
        // a host process (no real hardware waits), but the real app's boot
        // sequence (deluge_boot + registerTasks + encoders::init) touches a lot
        // of BSP surface for the first time on host, so give it room.
        let deadline = Instant::now() + Duration::from_secs(20);
        let mut boot_ok = false;
        loop {
            let tasks = crate::scheduler::registered_task_count();
            if tasks > 0 {
                log::info!(
                    "deluge-bsp-rust: HOST APP boot OK — registerTasks() claimed {tasks} scheduler slot(s)"
                );
                boot_ok = true;
            }
            // Fallback signal: the app rendered its first real OLED frame (only
            // reachable once deluge_boot/registerTasks got far enough to drive
            // display output), in case task-count observation somehow races past
            // a transient zero.
            if deluge_bsp::oled::boot_frame_captured() {
                log::info!("deluge-bsp-rust: HOST APP boot OK — first OLED frame captured");
                boot_ok = true;
            }
            if boot_ok {
                break;
            }
            if Instant::now() >= deadline {
                panic!(
                    "host app boot: registerTasks() had not claimed any scheduler slot within 20s \
                     (deluge_app_init likely wedged or crashed before reaching registerTasks())"
                );
            }
            std::thread::sleep(Duration::from_millis(5));
        }

        // Boot alone (a scheduler slot claimed / first OLED frame) only
        // shows registerTasks() ran — it doesn't show the
        // priority-0 (audio) task actually executes on the second executor
        // thread. Wait (bounded) for at least one `deluge_audio_drive` call
        // observed on the "deluge-audio" thread — the scheduled render, not
        // `AudioEngine::runRoutine()`'s pre-registration direct call from
        // `deluge_boot` (see `audio_host.rs`'s doc comment), which necessarily
        // runs on this (host-app executor) thread and doesn't count.
        let audio_deadline = Instant::now() + Duration::from_secs(5);
        loop {
            if audio_host::audio_thread_render_seen() {
                log::info!(
                    "deluge-bsp-rust: HOST APP audio-thread routing OK — priority-0 task rendered on \"deluge-audio\""
                );
                break;
            }
            if Instant::now() >= audio_deadline {
                panic!(
                    "host app boot: no deluge_audio_drive call was observed on the \"deluge-audio\" \
                     thread within 5s after boot — the priority-0 task did not route to the audio \
                     executor (scheduler::set_audio_spawner wiring regressed?)"
                );
            }
            std::thread::sleep(Duration::from_millis(5));
        }

        // Streaming-underrun harness: if scenario mode was requested, wait
        // for `scenario::scenario_task` (spawned above, on the host-app executor) to
        // finish, report its outcome, and exit — this REPLACES the generic soak below
        // (the scenario's own block-count step already keeps both executors running
        // concurrently for the requested window; a soak on top would just be dead
        // time). Bounded by a generous overall deadline so a wedged scenario still
        // exits nonzero instead of hanging the process forever.
        if let Some(cfg) = scenario_cfg {
            // std::time::Duration, not embassy_time::Duration (cfg.step_timeout's type) —
            // this loop runs on the plain OS thread, same as every other polling loop in
            // this block.
            let watchdog = Duration::from_millis(cfg.step_timeout.as_millis() * 8);
            let deadline = Instant::now() + watchdog;
            let result = loop {
                if let Some(r) = crate::scenario::take_result() {
                    break r;
                }
                if Instant::now() >= deadline {
                    log::error!(
                        "deluge-bsp-rust: HOST APP scenario TIMED OUT after {watchdog:?} with no \
                         result (scenario_task wedged?)"
                    );
                    hard_exit(1);
                }
                std::thread::sleep(Duration::from_millis(20));
            };
            log::info!(
                "deluge-bsp-rust: HOST APP scenario result: song='{}' boot_ready={} \
                 load_dispatched={} listing_completed={} load_committed={} load_completed={} \
                 playback_started={} playback_confirmed_active={} recording_started={} \
                 blocks_rendered={} cluster_reads={} recorder_writes={} underrun_wait={} \
                 underrun_unassign={}",
                cfg.song_full_path,
                result.boot_ready,
                result.song_load_dispatched,
                result.listing_completed,
                result.load_committed,
                result.load_completed,
                result.playback_started,
                result.playback_confirmed_active,
                result.recording_started,
                result.blocks_rendered,
                result.cluster_reads,
                result.recorder_writes,
                result.underrun_wait,
                result.underrun_unassign,
            );
            let ok = result.load_completed
                && result.playback_confirmed_active
                && result.recording_started
                && result.blocks_rendered >= cfg.target_blocks
                && result.cluster_reads > 0
                && result.recorder_writes > 0;
            if ok {
                log::info!(
                    "deluge-bsp-rust: HOST APP scenario PASSED — song streamed + recorded, \
                     real dispatch drained (cluster_reads={}, recorder_writes={})",
                    result.cluster_reads,
                    result.recorder_writes
                );
                hard_exit(0);
            } else {
                log::error!("deluge-bsp-rust: HOST APP scenario FAILED (see fields above)");
                hard_exit(1);
            }
        }

        // If the recorder live-readback probe was requested, wait for it
        // (spawned above, on the host-app executor) to finish, report the finding, and exit —
        // same shape as the scenario block above. A diagnostic, not a normative gate: any
        // outcome (READY, LOADING-forever, UNAVAILABLE) is a valid, reportable finding, so this
        // only hard_exit(1)s if the HARNESS itself failed (couldn't even set up the recorder /
        // never reached boot), not on an unresolved read.
        if recorder_probe_requested {
            let watchdog = Duration::from_millis(
                recorder_probe_step_timeout_ms + recorder_probe_poll_window_ms + 10_000,
            );
            let deadline = Instant::now() + watchdog;
            let result = loop {
                if let Some(r) = crate::recorder_probe::take_result() {
                    break r;
                }
                if Instant::now() >= deadline {
                    log::error!(
                        "deluge-bsp-rust: HOST APP recorder probe TIMED OUT after {watchdog:?} \
                         with no result (recorder_probe_task wedged?)"
                    );
                    hard_exit(1);
                }
                std::thread::sleep(Duration::from_millis(20));
            };
            log::info!(
                "deluge-bsp-rust: HOST APP recorder live-readback probe result: boot_ready={} \
                 initial_state={} final_state={} poll_iterations={} resolved_ready={} \
                 (state: 0=harness-error 1=READY 2=LOADING 3=UNAVAILABLE)",
                result.boot_ready,
                result.initial_state,
                result.final_state,
                result.poll_iterations,
                result.resolved_ready,
            );
            if !result.boot_ready
                || result.initial_state == crate::recorder_probe::STATE_HARNESS_ERROR
            {
                log::error!(
                    "deluge-bsp-rust: HOST APP recorder probe HARNESS FAILURE (see fields above)"
                );
                hard_exit(1);
            }
            if result.resolved_ready {
                log::info!(
                    "deluge-bsp-rust: HOST APP recorder probe FINDING — a still-recording \
                     sample's data DID resolve READY on this target (async_streaming_loader on)."
                );
            } else {
                log::warn!(
                    "deluge-bsp-rust: HOST APP recorder probe FINDING — a still-recording \
                     sample's data did NOT resolve READY on this target within the poll window \
                     (final_state={}) — matches the SR3b routing spike's prediction that the \
                     async loader cannot read a handle-less recording.",
                    result.final_state,
                );
            }
            hard_exit(0);
        }

        // If the finalized multi-cluster regression probe was requested, wait for it
        // (spawned above, on the host-app executor) to finish, report the finding, and exit. This
        // one IS a normative gate (unlike the live-readback probe above): a finalized recording's
        // residency table MUST be sized correctly and region index 1+ MUST read back correctly, on
        // every target, or this is a critical regression.
        if recorder_finalize_probe_requested {
            let watchdog = Duration::from_millis(
                recorder_finalize_probe_step_timeout_ms
                    + recorder_finalize_probe_poll_window_ms
                    + 10_000,
            );
            let deadline = Instant::now() + watchdog;
            let result = loop {
                if let Some(r) = crate::recorder_finalize_probe::take_result() {
                    break r;
                }
                if Instant::now() >= deadline {
                    log::error!(
                        "deluge-bsp-rust: HOST APP recorder finalize probe TIMED OUT after \
                         {watchdog:?} with no result (recorder_finalize_probe_task wedged?)"
                    );
                    hard_exit(1);
                }
                std::thread::sleep(Duration::from_millis(20));
            };
            log::info!(
                "deluge-bsp-rust: HOST APP recorder finalize probe result: boot_ready={} \
                 initial_state={} final_state={} poll_iterations={} table_clusters={} \
                 expected_clusters={} bytes_ok={} passed={} \
                 (state: 0=harness-error 1=READY 2=LOADING 3=UNAVAILABLE)",
                result.boot_ready,
                result.initial_state,
                result.final_state,
                result.poll_iterations,
                result.table_clusters,
                result.expected_clusters,
                result.bytes_ok,
                result.passed,
            );
            if !result.boot_ready
                || result.initial_state == crate::recorder_finalize_probe::STATE_HARNESS_ERROR
            {
                log::error!(
                    "deluge-bsp-rust: HOST APP recorder finalize probe HARNESS FAILURE (see fields above)"
                );
                hard_exit(1);
            }
            if result.passed {
                log::info!(
                    "deluge-bsp-rust: HOST APP recorder finalize probe PASSED — finalized \
                     multi-cluster recording's residency table sized correctly \
                     (table_clusters={} >= expected_clusters={}), region 1 read back READY with \
                     correct bytes.",
                    result.table_clusters,
                    result.expected_clusters,
                );
                hard_exit(0);
            } else {
                log::error!(
                    "deluge-bsp-rust: HOST APP recorder finalize probe FAILED — SR3b Task 3 \
                     regression present (see fields above)"
                );
                hard_exit(1);
            }
        }

        // --- Widen the concurrent window before exit ------------------------
        // The boot-OK / audio-routing-OK checks above only prove the two
        // executors are both alive and correctly wired — they fire within
        // ~1-2 render cycles of boot, which leaves the host-app and audio
        // threads almost no time to actually overlap. The real races
        // (`AudioEngine::audioRoutineLocked`, `audioSampleTimer`) come from
        // the *whole* task graph `registerTasks()` spawns (17 slots, incl.
        // `UITimerManager::routine`) ticking on the host-app executor
        // CONCURRENTLY with the audio thread's ongoing renders — so, instead
        // of exiting the instant boot-proof is established, keep both
        // executors running for a bounded soak so the repeating main-thread
        // tasks and the audio render overlap heavily before exit. This is
        // what lets a TSan run actually see the cross-thread C++ race
        // surface.
        //
        // Bounded two ways — wall-clock (`SOAK_DURATION`) and render-cycle
        // count (`SOAK_MAX_RENDER_BLOCKS`, via `audio_host::drive_count()`),
        // whichever is hit first — so this can never hang: it's a fixed
        // sleep-and-poll loop, nothing here waits on anything unbounded.
        const SOAK_DURATION: Duration = Duration::from_secs(4);
        const SOAK_MAX_RENDER_BLOCKS: u64 = 4_000;
        let soak_start = Instant::now();
        let soak_start_blocks = audio_host::drive_count();
        let soak_deadline = soak_start + SOAK_DURATION;
        loop {
            let rendered = audio_host::drive_count().saturating_sub(soak_start_blocks);
            if Instant::now() >= soak_deadline || rendered >= SOAK_MAX_RENDER_BLOCKS {
                log::info!(
                    "deluge-bsp-rust: HOST APP soak done after {:?} — {rendered} render \
                     block(s) rendered concurrently with the main-thread task graph",
                    soak_start.elapsed()
                );
                break;
            }
            std::thread::sleep(Duration::from_millis(20));
        }

        log::info!("deluge-bsp-rust: HOST harness OK");
        // See `hard_exit`'s doc comment: a normal return here (or a plain
        // `std::process::exit`) races the still-live host-app/audio executor
        // threads against libc's atexit-run C++ static destructors.
        hard_exit(0);
    }

    #[cfg(not(feature = "host_app"))]
    log::info!("deluge-bsp-rust: HOST harness OK");
}

/// Immediate, unconditional process termination for the `host_app` boot smoke
/// — skips libc's atexit-run destructors entirely (a raw Linux `exit_group(2)`
/// syscall, not `std::process::exit`/a normal return from `fn main`).
///
/// Needed because the real C++ app links genuine C++ objects with static
/// storage duration (`AudioEngine`, `midiEngine`, `playbackHandler`,
/// `cvEngine`, …) that register destructors via `__cxa_atexit` when
/// constructed at process start (`.init_array`). A normal return from `fn
/// main` (or `std::process::exit`, which still calls libc `exit()`) runs
/// those destructors on the main thread while the host-app executor thread —
/// still alive, still ticking the real scheduler task runners spawned by
/// `registerTasks()` — keeps calling virtual methods on those same objects
/// concurrently. Found running the host_app boot smoke under ThreadSanitizer:
/// the unsanitized build is fast enough to usually win this exit race (the
/// process was gone before a background tick landed mid-destructor), but
/// TSan's instrumentation overhead reliably loses it — a background task's
/// `deluge_display_consume_transfer_ack`/`deluge_audio_input_resync` tick fired
/// after a static's destructor had already reset its vtable, so the next
/// virtual call landed on the pure-virtual stub: `pure virtual method called`
/// / `terminate called without an active exception` (SIGABRT), not a TSan
/// `WARNING: data race` — unsurprising, since the racing code (the prebuilt
/// C++ app + libc's exit path) is outside TSan's instrumentation (see
/// HOST_HARNESS.md's scoping note); TSan only widened the timing window that
/// exposed a real lifecycle bug it can't itself see. This is exactly the
/// "process never returns" shape of the real device's `main -> !` anyway —
/// there is no orderly app shutdown on hardware either — so a hard kill after
/// the boot-OK observation is the *correct* model for this smoke, not a
/// workaround.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
fn hard_exit(code: i32) -> ! {
    #[cfg(target_os = "linux")]
    unsafe {
        core::arch::asm!(
            "syscall",
            in("rax") 231usize, // exit_group
            in("rdi") code,
            options(noreturn, nostack)
        );
    }
    #[cfg(not(target_os = "linux"))]
    std::process::exit(code);
}
