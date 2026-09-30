//! The Rust async cluster-fill task and its BSP wiring: drains the resource
//! manager's loader queue (`deluge_resource_loader_next`), awaiting the actual
//! SD read instead of running it synchronously inline.
//!
//! ## What compiles where
//!
//! [`FILL_WAKE`], the `extern "C"` wakeup and request/poll functions, and the
//! drain machinery ([`FillOps`], [`fill_once`]) compile wherever this module
//! does, including the `#[path]` host test carriers. The production wiring
//! ([`ProdOps`], [`streaming_fill_task`]) needs the real C-ABI symbols, so it
//! compiles only on device and under `host_app`; `main.rs` spawns the task on
//! both. There is no C++ fallback: these are the only definitions.
//!
//! ## Why the drain loop is generic over [`FillOps`]
//!
//! Constructing a real `StreamedChunk`/`SampleStream`/`Sample` plus a seeded
//! resource manager inside a Rust host test is impractical, and mocking the
//! `#[no_mangle]` C symbols below would collide with the real ones whenever this
//! crate also links the C++ app (`host_app`). So the orchestration itself
//! ([`fill_once`]) takes its four operations — `next`/`begin`/`read`/`finish` (plus
//! the failure-path re-enqueue) — through the [`FillOps`] trait instead of calling
//! the `extern "C"` functions directly. [`ProdOps`] below wires that trait to the
//! real C ABI + the efatfs streaming read (`crate::efatfs_fs`/`crate::efatfs_host_shim`); the
//! host unit test
//! (`tests/streaming_fill_host.rs`) wires it to an in-memory fake queue instead.
//! This is the whole reason [`fill_once`] is independently testable — the real
//! end-to-end fill (real manager, real chunks) is validated separately, via a
//! ThreadSanitizer check and a full scenario harness.
//!
//! ## Concurrency
//!
//! The manager (`DelugeResource*`) is `!Send`/`!Sync` by design — single-executor
//! only, and [`streaming_fill_task`] is the only place this module touches it.
//!
//! That task does NOT run on the thread-mode executor. On device it runs on
//! `STREAM_EXEC`, a preemptive `InterruptExecutor` at SGI priority 24 (spawned
//! via `main.rs`'s `stream_bootstrap`); on host it owns the `"deluge-stream"`
//! thread. That placement is load-bearing rather than incidental: this task
//! holds the single `efatfs` FS mutex across its SD await, while the C++
//! storage FFI (`deluge_efatfs_*`) bridges to these same futures through
//! `sim_block::block_on`, a NON-yielding spin on device. A spin on the
//! thread-mode executor therefore cannot let a thread-mode task run — see the
//! FFI-bridge note in `efatfs_fs.rs`, "thread-mode tasks (PIC pump, OLED, UI)
//! do not run during one. Streaming is unaffected — it lives on its own
//! interrupt executor." Were the fill task thread-mode, that spin would wait
//! forever on a mutex only the task it is starving can release.
//!
//! Cross-tier safety for the `!Send` manager pointer comes from
//! `deluge_resource::sync`'s critical-section serialization, not from sharing an
//! executor with the C++ enqueue path (the CLUSTER_ENQUEUE sites in
//! `sample_stream.cpp`), which it does not.
use core::ffi::c_void;

use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::signal::Signal;

/// Raised to wake [`streaming_fill_task`] out of its idle wait — e.g. when a
/// cluster is newly enqueued. Before the task is spawned nobody awaits it;
/// signalling it is just a harmless flag set.
pub static FILL_WAKE: Signal<CriticalSectionRawMutex, ()> = Signal::new();

// ── C ABI: wakeup ────────────────────────────────────────────────────────────
// See `include/libdeluge/streaming_fill.h`'s doc comments for the C-side contract.

/// Wake [`streaming_fill_task`] out of its idle wait. Safe to call whether or
/// not the task exists yet — [`Signal::signal`] just records "latest value
/// pending"; a `Signal` nobody is waiting on drops the previous pending value
/// (if any) and stores the new one, which is fine here since the payload is
/// `()` (a pure wakeup, not data).
#[unsafe(no_mangle)]
pub extern "C" fn deluge_streaming_signal_fill() {
    FILL_WAKE.signal(());
}

// ── C ABI: the non-blocking request/poll pairs ──────────────────────────────
// See `include/libdeluge/streaming_fill.h` for the C-side contract.

/// Most-urgent loader priority (lower = more urgent), the opposite end of `kLowestLoaderPriority`
/// (the passive-lookahead prefetch value `0xFFFF_FFFF`) — used to enqueue a chunk a caller has
/// asked for and is waiting on, so the drain lands it ahead of any queued prefetch.
const BLOCKING_FILL_PRIORITY: u32 = 0;

/// The process-wide resource-manager singleton, as the untyped pointer the loader-queue C ABI takes.
fn resource_manager() -> *mut c_void {
    // SAFETY: no preconditions; returns the boot-singleton manager or null.
    unsafe { crate::sys::deluge_streaming_resource_manager() }.cast::<c_void>()
}

unsafe extern "C" {
    /// The chunk-table slot backing `ptr` (`deluge_resource.h`), for the loader-queue enqueue below.
    fn deluge_resource_slot_of(mgr: *mut c_void, ptr: *mut c_void) -> u32;
    /// Enqueue the chunk at `slot` at `priority` AND take a queue-owned lease, so the entry survives
    /// the requester dropping its own (`deluge_resource.h`).
    fn deluge_resource_loader_enqueue_owned(mgr: *mut c_void, slot: u32, priority: u32);
    /// Non-destructive "loader queue non-empty" predicate (`deluge_resource.h`): true while any
    /// queued+leased chunk remains — what [`queue_is_drained`] below negates.
    fn deluge_resource_loader_has_any(mgr: *mut c_void) -> bool;
}

/// True when the loader queue holds nothing outstanding.
///
/// The single definition of drained-ness, used by the non-blocking [`deluge_streaming_queue_drained`].
fn queue_is_drained(mgr: *mut c_void) -> bool {
    // SAFETY: `mgr` is the boot-singleton manager; the predicate only reads per-slot queue state,
    // popping and mutating nothing.
    !unsafe { deluge_resource_loader_has_any(mgr) }
}

/// Enqueues `chunk_backing` for the fill task under a queue-owned lease and wakes the drain.
///
/// The owned enqueue is what lets a fire-and-forget request survive the requester walking away:
/// `deluge_resource_loader_next` only serves a chunk with a live lease, and *silently de-queues* any
/// other, so an un-owned entry whose requester dropped its lease (the reader's `if !filled` arm) was
/// discarded on the drain's next poll -- which is why the display-only overview pre-scan once never
/// converged on device.
fn enqueue_chunk_for_fill(chunk_backing: *mut crate::sys::DelugeChunkBacking) {
    // `chunk_backing` is a resident, leased `StreamedChunk*`.
    let mgr = resource_manager();
    if !mgr.is_null() {
        let slot = unsafe { deluge_resource_slot_of(mgr, chunk_backing.cast()) };
        unsafe { deluge_resource_loader_enqueue_owned(mgr, slot, BLOCKING_FILL_PRIORITY) };
    }
    FILL_WAKE.signal(());
}

/// Enqueues `chunk_backing` for filling and returns immediately.
///
/// The chunk is enqueued under a queue-owned lease, so the fill survives the requester returning to
/// its loop.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_streaming_request_chunk(
    chunk_backing: *mut crate::sys::DelugeChunkBacking,
) {
    if chunk_backing.is_null() {
        return;
    }
    enqueue_chunk_for_fill(chunk_backing);
}

/// Wakes the fill task so it drains the loader queue, and returns immediately.
///
/// Pair with [`deluge_streaming_queue_drained`].
#[unsafe(no_mangle)]
pub extern "C" fn deluge_streaming_request_drain() {
    FILL_WAKE.signal(());
}

/// True when the loader queue holds nothing outstanding. Never waits.
///
/// A null manager reports drained: there is no queue, so there is nothing for a caller to wait on,
/// and reporting otherwise would loop a poller forever.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_streaming_queue_drained() -> bool {
    let mgr = resource_manager();
    if mgr.is_null() {
        return true;
    }
    queue_is_drained(mgr)
}

// ── C ABI: per-asset fill-context table ─────────────────────────────────────
// Registered by C++ at sample-load (`deluge_streaming_define_asset()`/`SampleStream::open_read_stream()`,
// see `chunk_residency.cpp`/`sample_stream.cpp`); read by `deluge_sample_fill::native_begin`/
// `native_finish`. See `include/libdeluge/streaming_fill.h`'s doc for the C-side
// contract. `StreamingFillDescriptor` (used by [`FillOps`]'s own signatures below) and
// `DelugeChunkConvertState` also live in the shared `deluge_sample_fill` crate;
// `pub use` (not a plain `use`) so `tests/streaming_fill_host.rs`'s `#[path]`-recompiled copy of
// this file can still reach it as `streaming_loader::StreamingFillDescriptor`.
pub use deluge_sample_fill::StreamingFillDescriptor;

/// `kLowestLoaderPriority` — re-enqueue value for a cluster whose read just
/// failed while still wanted, so it sinks behind everything else instead of
/// being popped again immediately.
const LOWEST_PRIORITY: u32 = 0xFFFF_FFFF;

/// The four operations [`fill_once`] drains the loader queue through. `chunk`
/// values are opaque `StreamedChunk*` backing pointers, as returned by
/// [`FillOps::next`] and passed back unchanged to `begin`/`finish`/
/// `enqueue_lowest` — this trait never interprets them, only threads them
/// through, so a test double can hand back whatever identity it likes.
///
/// A generic `fill_once<O: FillOps>` (rather than `dyn FillOps` or free
/// functions) keeps this a zero-cost, monomorphized boundary in the production
/// path while still being fully substitutable in tests.
pub trait FillOps {
    /// Pop the most-urgent queued+still-leased chunk, or a null pointer if the
    /// queue is empty (`deluge_resource_loader_next`).
    fn next(&self) -> *mut c_void;
    /// Whether `chunk` has been marked unloadable since it was enqueued
    /// (`deluge_sample_fill::chunk::unloadable`) — a safety-net skip right after
    /// `next()`: already dequeued, so skipping can't loop, and it doesn't count
    /// against the fill budget.
    fn is_unloadable(&self, chunk: *mut c_void) -> bool;
    /// Resolve `chunk`'s destination buffer + physical sector range
    /// (`deluge_sample_fill::native_begin`). `ok == false` means skip this chunk
    /// entirely (unloadable / geometry error) — no read, no `finish`.
    fn begin(&self, chunk: *mut c_void) -> StreamingFillDescriptor;
    /// Await the read for descriptor `d` into `buf`. Returns whether it
    /// succeeded — routes to the efatfs handle at `d.byte_offset`; `read_at`
    /// handles a bad/zero handle by returning false.
    async fn read(&self, d: &StreamingFillDescriptor, buf: &mut [u8]) -> bool;
    /// Run the post-read convert/stitch/publish tail
    /// (`deluge_sample_fill::native_finish`). Only called after a *successful*
    /// read — the convert/stitch/publish tail never runs on a failed read.
    fn finish(&self, chunk: *mut c_void, read_ok: bool) -> bool;
    /// `chunk`'s hard-lease count EXCLUDING the load queue's own
    /// (`deluge_resource_slot_of` + `deluge_resource_external_lease_count_by_slot`),
    /// consulted only after a failed read to decide drop-vs-requeue (see
    /// `fill_once`). Excluding the queue's own lease is what makes that decision
    /// answerable: a chunk enqueued through
    /// `deluge_resource_loader_enqueue_owned` carries a lease the queue itself
    /// holds, so the raw count never reaches 0 and an abandoned chunk would be
    /// retried — and pinned — forever.
    fn external_lease_count(&self, chunk: *mut c_void) -> u32;
    /// Release the queue-owned lease on `chunk`, if it has one
    /// (`deluge_resource_loader_release_owned`). A no-op for a chunk whose
    /// enqueuer holds its own lease. Must be called on every path that stops
    /// caring about `chunk`, or the lease pins it against eviction for good.
    fn release_owned(&self, chunk: *mut c_void);
    /// Re-enqueue `chunk` at [`LOWEST_PRIORITY`] (`deluge_resource_loader_enqueue`)
    /// — the read failed while the chunk was still wanted. Any queue-owned lease
    /// stays held: the entry lives on, so its lease must too.
    fn enqueue_lowest(&self, chunk: *mut c_void);
}

/// Drain the loader queue: for each queued cluster, resolve → await the read →
/// convert/stitch/mark-ready:
/// - the post-`next()` unloadable safety-net skip — drop, keep draining,
///   don't count it;
/// - the post-`begin()` `!ok` skip (unloadable / geometry error) — drop, keep
///   draining;
/// - on a **successful** read: run the convert/stitch/publish `finish` tail,
///   then keep draining;
/// - on a **failed** read: `finish` is never called (it's a success-only
///   tail). Instead check the external lease count: if it dropped to 0 while
///   loading, the chunk is already unwanted — drop it and keep draining.
///   Otherwise a caller still wants it — re-enqueue at lowest priority and
///   stop, else we'd keep re-popping the same cluster until the card is back.
///
/// Every path that stops caring about a chunk releases its queue-owned lease
/// ([`FillOps::release_owned`], a no-op unless it was enqueued through
/// `deluge_resource_loader_enqueue_owned`). Only the re-enqueue path keeps it,
/// because there the queue entry lives on. Missing one of these would pin the
/// chunk against eviction for the rest of the run — on a pulled card, every
/// queued chunk at once.
pub async fn fill_once<O: FillOps>(ops: &O) {
    loop {
        let chunk = ops.next();
        if chunk.is_null() {
            return;
        }

        if ops.is_unloadable(chunk) {
            // Safety net: already de-queued by `next()`, so skipping can't
            // loop. Doesn't count against the fill budget.
            ops.release_owned(chunk);
            continue;
        }

        let d = ops.begin(chunk);
        if !d.ok {
            // Unloadable / geometry error — already dequeued by `next`; skip it,
            // don't loop on it, keep draining the rest of the queue.
            ops.release_owned(chunk);
            continue;
        }

        // SAFETY: `d.ok` is true, so `dest` is a valid, exclusively-owned
        // destination for exactly `num_sectors * 512` bytes (the production
        // `begin` resolves it from the chunk's own payload buffer; the test
        // double's `begin` hands back its own owned backing storage).
        let buf =
            unsafe { core::slice::from_raw_parts_mut(d.dest, (d.num_sectors as usize) * 512) };
        #[cfg(feature = "host_app")]
        {
            let us = FILL_READ_LATENCY_US.load(core::sync::atomic::Ordering::Relaxed);
            if us > 0 {
                embassy_time::Timer::after_micros(u64::from(us)).await;
            }
        }
        let read_ok = ops.read(&d, buf).await;

        if read_ok {
            // Success tail: convert/stitch/publish (which marks the chunk ready), then release the
            // queue's lease and keep draining. For a fire-and-forget enqueuer that was the only
            // lease, the chunk is now ready-but-unleased: available as a cache hit to whoever asked
            // for it, and evictable again under memory pressure (so a scan that loses the race
            // simply re-requests).
            ops.finish(chunk, true);
            ops.release_owned(chunk);
            continue;
        }

        // Read failed. If nobody outside the queue still holds a lease, the chunk is already
        // unwanted — release the queue's own lease and keep draining. This is also the bound on
        // retrying a permanently failing read (a pulled card): a fire-and-forget chunk is dropped
        // here rather than re-queued forever while pinned.
        // Otherwise a caller still wants it: re-queue at lowest priority (keeping the queue lease,
        // since the entry survives) and stop.
        if ops.external_lease_count(chunk) == 0 {
            ops.release_owned(chunk);
            continue;
        }
        ops.enqueue_lowest(chunk);
        return;
    }
}

// ── Production wiring ───────────────────────────────────────────────────────
// Only compiled where the real C-ABI symbols below are actually linked: always
// on device, and on host only under `host_app` (which links the host-built C++
// app object closure — see Cargo.toml). Kept separate from `fill_once`/`FillOps`
// above so a host test carrier without `host_app` never emits a reference to an
// undefined extern symbol.
#[cfg(any(target_os = "none", feature = "host_app"))]
mod prod {
    use super::{FillOps, LOWEST_PRIORITY};
    use core::ffi::c_void;
    use deluge_sample_fill::StreamingFillDescriptor;

    unsafe extern "C" {
        fn deluge_resource_loader_next(mgr: *mut c_void) -> *mut c_void;
        fn deluge_resource_loader_enqueue(mgr: *mut c_void, slot: u32, priority: u32);
        fn deluge_resource_slot_of(mgr: *mut c_void, ptr: *mut c_void) -> u32;
        fn deluge_resource_external_lease_count_by_slot(mgr: *mut c_void, slot: u32) -> u32;
        fn deluge_resource_loader_release_owned(mgr: *mut c_void, slot: u32) -> bool;
    }

    /// The real [`FillOps`], wired to `libdeluge/streaming_fill.h` +
    /// `deluge_resource.h`'s loader-queue C ABI and the efatfs streaming read
    /// (`crate::efatfs_fs`/`crate::efatfs_host_shim`). `!Send`/`!Sync` (a raw
    /// `DelugeResource*`) by construction — must only ever run on the single
    /// thread-mode executor the C++ enqueue path also runs on.
    pub struct ProdOps {
        mgr: *mut c_void,
    }

    impl ProdOps {
        /// Must only be constructed and used on the single thread-mode Embassy
        /// executor the whole app (and the C++ enqueue path) runs on — see the
        /// module doc's Concurrency section.
        pub fn new() -> Self {
            // The one process-wide GeneralMemoryAllocator resource manager;
            // no aliasing/ownership concern, it's a stable singleton pointer.
            let mgr = super::resource_manager();

            // No startup capacity guard here: convert-state lives directly on
            // each `StreamedChunk` via `deluge_sample_fill::chunk::convert_state`/
            // `set_convert_state`, which has no separate capacity to overflow (it's a plain field
            // access on a chunk the caller already holds), so there is nothing to guard.
            Self { mgr }
        }
    }

    impl FillOps for ProdOps {
        fn next(&self) -> *mut c_void {
            // SAFETY: `mgr` is the valid singleton resource manager.
            unsafe { deluge_resource_loader_next(self.mgr) }
        }

        fn is_unloadable(&self, chunk: *mut c_void) -> bool {
            // SAFETY: `chunk` was just returned by `next()`.
            unsafe { deluge_sample_fill::chunk::unloadable(chunk) }
        }

        fn begin(&self, chunk: *mut c_void) -> StreamingFillDescriptor {
            // `chunk` was just returned by `next()` (a queued, still-leased `StreamedChunk*`).
            deluge_sample_fill::native_begin(chunk)
        }

        async fn read(&self, d: &StreamingFillDescriptor, buf: &mut [u8]) -> bool {
            #[cfg(target_os = "none")]
            return crate::efatfs_fs::read_at(d.handle, d.byte_offset, buf).await;
            #[cfg(feature = "host_app")]
            return crate::efatfs_host_shim::read_at(d.handle, d.byte_offset, buf).await;
        }

        fn finish(&self, chunk: *mut c_void, read_ok: bool) -> bool {
            // `chunk` is the same pointer `begin` was just called with, whose data has just been
            // successfully read into its payload buffer (when `read_ok`).
            deluge_sample_fill::native_finish(chunk, read_ok)
        }

        fn external_lease_count(&self, chunk: *mut c_void) -> u32 {
            // SAFETY: `mgr`/`chunk` are both still valid (the chunk hasn't been
            // freed — it's still leased, just its read failed).
            let slot = unsafe { deluge_resource_slot_of(self.mgr, chunk) };
            unsafe { deluge_resource_external_lease_count_by_slot(self.mgr, slot) }
        }

        fn release_owned(&self, chunk: *mut c_void) {
            // SAFETY: `mgr`/`chunk` are both still valid — the chunk is still resident here (this
            // runs before anything can free it), and a release on a chunk the queue holds no lease
            // on is a checked no-op inside the manager.
            let slot = unsafe { deluge_resource_slot_of(self.mgr, chunk) };
            unsafe { deluge_resource_loader_release_owned(self.mgr, slot) };
        }

        fn enqueue_lowest(&self, chunk: *mut c_void) {
            // SAFETY: `mgr`/`chunk` are both still valid (the chunk hasn't been
            // freed — it's still leased, just its read failed).
            let slot = unsafe { deluge_resource_slot_of(self.mgr, chunk) };
            unsafe { deluge_resource_loader_enqueue(self.mgr, slot, LOWEST_PRIORITY) };
        }
    }
}

#[cfg(any(target_os = "none", feature = "host_app"))]
pub use prod::ProdOps;

/// Milliseconds the fill task waits before each drain pass. Host harness only; see
/// [`deluge_streaming_set_fill_latency_ms`].
#[cfg(feature = "host_app")]
static FILL_LATENCY_MS: core::sync::atomic::AtomicU32 = core::sync::atomic::AtomicU32::new(0);

/// Host harness knob: make the streaming fill task wait `ms` milliseconds before each drain pass, so a gate can hold
/// cluster fills while a consumer keeps ticking. 0, the default, means no wait.
///
/// Compiled only under `host_app`: the device image has neither this function nor the static it sets.
#[cfg(feature = "host_app")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_streaming_set_fill_latency_ms(ms: u32) {
    FILL_LATENCY_MS.store(ms, core::sync::atomic::Ordering::Relaxed);
}

/// Microseconds each chunk's read takes, on top of the card's own time. Host harness only; see
/// [`deluge_streaming_set_fill_read_latency_us`].
#[cfg(feature = "host_app")]
static FILL_READ_LATENCY_US: core::sync::atomic::AtomicU32 = core::sync::atomic::AtomicU32::new(0);

/// Host harness knob: make every chunk's read take `us` microseconds longer, in the fill task and so without holding
/// anything else up. The harness's card answers at once, so this is how a gate models the device card's time to read a
/// chunk; [`deluge_streaming_set_fill_latency_ms`] models the fill task's wait to be scheduled. 0, the default, adds
/// nothing.
///
/// Compiled only under `host_app`: the device image has neither this function nor the static it sets.
#[cfg(feature = "host_app")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_streaming_set_fill_read_latency_us(us: u32) {
    FILL_READ_LATENCY_US.store(us, core::sync::atomic::Ordering::Relaxed);
}

/// The fill task: wakes on [`FILL_WAKE`], drains the loader queue via the real
/// [`ProdOps`], then goes back to sleep. Spawned in `main.rs` (device `main` +
/// host `host_app`); the C++ enqueue path
/// (`sample_stream.cpp`) wakes it via `deluge_streaming_signal_fill`.
///
/// # Why `ProdOps` is constructed AFTER the first wake, never before
///
/// [`ProdOps::new`] reaches into the C++ resource manager, and
/// `GeneralMemoryAllocator::ensureClusterSystem()` on that path READS the global
/// `Cluster::size` — which `deluge_app_init` writes during boot
/// (`AudioFileManager::init()` -> `Cluster::set_size`). This task no longer shares an
/// execution context with `deluge_app_init`: on device it runs on `STREAM_EXEC`, whose SGI is
/// enabled before the thread executor ever reaches `app_task`, and on host it owns the
/// `"deluge-stream"` thread. Constructing at the top of the task therefore raced that boot
/// write — caught by TSan as `Cluster::set_size` vs `ProdOps::new` across two threads — and on
/// device could have built the cluster system from the 32768 default instead of the card's real
/// cluster size, silently wrong on any non-32KB card.
///
/// Waiting first is a complete fix rather than a narrowing, because nothing can signal
/// [`FILL_WAKE`] until the C++ enqueue path runs, which is necessarily after init. `FILL_WAKE`
/// is a latching `Signal`, so a wake that arrives before this task is first polled is stored
/// and returned immediately — no wake is lost by waiting.
#[cfg(any(target_os = "none", feature = "host_app"))]
#[embassy_executor::task]
pub async fn streaming_fill_task() {
    // The device derives the tier from the storage handler's own mark (`tier::storage_isr`); this line is the
    // evidence on RTT that the derivation sees it. Expected: `Storage`.
    #[cfg(target_os = "none")]
    log::info!(
        "tier: the streaming fill task runs on {:?}",
        crate::tier::current()
    );
    crate::tier::scoped(crate::tier::Tier::Storage, streaming_fill_task_body()).await
}

#[cfg(any(target_os = "none", feature = "host_app"))]
async fn streaming_fill_task_body() {
    FILL_WAKE.wait().await;
    let ops = ProdOps::new();
    loop {
        #[cfg(feature = "host_app")]
        {
            let ms = FILL_LATENCY_MS.load(core::sync::atomic::Ordering::Relaxed);
            if ms > 0 {
                embassy_time::Timer::after_millis(u64::from(ms)).await;
            }
        }
        fill_once(&ops).await;
        FILL_WAKE.wait().await;
    }
}

abi_pin!(
    deluge_streaming_queue_drained,
    deluge_streaming_request_chunk,
    deluge_streaming_request_drain,
    deluge_streaming_signal_fill,
);
