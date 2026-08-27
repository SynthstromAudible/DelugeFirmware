//! The Rust async cluster-fill task and its BSP wiring: drains the resource
//! manager's loader queue (`deluge_resource_loader_next`) on the same
//! thread-mode Embassy executor the fiber pump and the C++ enqueue path run on,
//! awaiting the actual SD read instead of running it synchronously inline.
//!
//! ## Two compilation tiers
//!
//! This module compiles in two tiers, because the selector the C++ side calls
//! (`deluge_streaming_async_active`) must resolve on the Embassy BSP regardless
//! of whether the async backing is actually enabled — a cargo feature can't
//! reach a C++ `#define`, so "is the async task active" has to be a runtime
//! call, and that call needs a real symbol to link against either way:
//!
//! - Always compiled whenever this module is (i.e. on the Embassy BSP, device
//!   or `host_app` — see the `mod streaming_loader;` cfg in `main.rs`), no
//!   matter the `async_streaming_loader` feature: [`FILL_WAKE`], the two
//!   `extern "C"` selector/wakeup functions just below it
//!   (`deluge_streaming_async_active`, `deluge_streaming_signal_fill`), and the
//!   per-asset fill-context table (`deluge_streaming_set_fill_context`,
//!   [`fill_context_for`]) — asset definition, which registers it, happens on
//!   every BSP at sample-load, not just the ones with the async fill task
//!   built in. Every other BSP/config (legacy/host-cooperative sim, rza1)
//!   never links this crate at all; the `__attribute__((weak))` C++ fallbacks
//!   in `async_fill.cpp` resolve there instead.
//! - Feature-gated behind `async_streaming_loader`: the actual drain machinery
//!   ([`FillOps`], [`fill_once`], [`ProdOps`], [`streaming_fill_task`]). Spawned
//!   in `main.rs` (device `main` + host `host_app`) only under that feature.
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
/// cluster is newly enqueued. Same shape as `fiber::WORKER_WAKE`. Always
/// compiled (see the module doc's "Two compilation tiers") — with the feature
/// off, or before the task is spawned, nobody ever awaits it; signalling it is
/// just a harmless flag set.
pub static FILL_WAKE: Signal<CriticalSectionRawMutex, ()> = Signal::new();

// ── Always-compiled C ABI: selector + wakeup ─────────────────────────────────
// See `include/libdeluge/streaming_fill.h`'s doc comments for the C-side contract.

/// Whether the async streaming-fill task owns the loader queue on this
/// build. The return value is the only thing that depends on the cargo
/// feature — the symbol itself must always exist so its call site (the Rust
/// range reader's fill-route gate, `deluge_sample_reader`) links regardless of
/// which config produced this BSP image.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_streaming_async_active() -> bool {
    cfg!(feature = "async_streaming_loader")
}

/// Wake [`streaming_fill_task`] out of its idle wait. Safe to call whether or
/// not the task exists yet — [`Signal::signal`] just records "latest value
/// pending"; a `Signal` nobody is waiting on drops the previous pending value
/// (if any) and stores the new one, which is fine here since the payload is
/// `()` (a pure wakeup, not data).
#[unsafe(no_mangle)]
pub extern "C" fn deluge_streaming_signal_fill() {
    FILL_WAKE.signal(());
}

// ── Always-compiled C ABI: reader range-fill blocking-fill routing ──────────
// See `include/libdeluge/streaming_fill.h`'s `deluge_streaming_fill_chunk_blocking` doc for the
// C-side contract. Always compiled (like the selector above) so the reader crate's call site links
// on any Rust/Embassy BSP regardless of the `async_streaming_loader` feature; a non-async build
// never reaches it at runtime because the reader gates on `deluge_streaming_async_active()`.

/// Most-urgent loader priority (lower = more urgent), the opposite end of `kLowestLoaderPriority`
/// (the passive-lookahead prefetch value `0xFFFF_FFFF`) — used to enqueue a chunk a caller is
/// synchronously BLOCKING on so the drain lands it ahead of any queued prefetch.
const BLOCKING_FILL_PRIORITY: u32 = 0;

/// Bound on the number of yield/re-poll cycles the on-fiber wait spins before giving up (a read
/// that repeatedly fails while the chunk stays leased — e.g. the card was pulled — would otherwise
/// never flip `loaded`). Far more than a successful single-cluster fill ever needs (that lands in
/// one drain cycle), finite so a permanent fault degrades to a not-ready result instead of wedging
/// the fiber.
const BLOCKING_FILL_MAX_CYCLES: u32 = 4096;

unsafe extern "C" {
    /// The process-wide resource-manager singleton (see the `prod` module's own copy of this
    /// prototype; a foreign-fn prototype may be declared in more than one module without conflict).
    fn deluge_streaming_resource_manager() -> *mut c_void;
    /// The chunk-table slot backing `ptr` (`deluge_resource.h`), for the loader-queue enqueue below.
    fn deluge_resource_slot_of(mgr: *mut c_void, ptr: *mut c_void) -> u32;
    /// Enqueue the chunk at `slot` at `priority` AND take a queue-owned lease, so the entry survives
    /// the requester dropping its own (`deluge_resource.h`).
    fn deluge_resource_loader_enqueue_owned(mgr: *mut c_void, slot: u32, priority: u32);
    /// Non-destructive "loader queue non-empty" predicate (`deluge_resource.h`): true while any
    /// queued+leased chunk remains — the condition the drain-all-queued wait below polls to empty.
    fn deluge_resource_loader_has_any(mgr: *mut c_void) -> bool;
}

/// True once `chunk_backing`'s fill has landed.
///
/// The single definition of chunk readiness: both [`WaitChunkLoaded`] and the non-blocking
/// [`deluge_streaming_chunk_ready`] call it, so a poller and a waiter cannot disagree about what
/// "ready" means.
fn chunk_is_loaded(chunk_backing: *mut c_void) -> bool {
    // SAFETY: the caller holds a lease on `chunk_backing` for the duration, so it stays resident
    // while we read its `loaded` flag.
    unsafe { deluge_sample_fill::chunk::loaded(chunk_backing) }
}

/// True when the loader queue holds nothing outstanding.
///
/// The single definition of drained-ness, shared by [`WaitQueueDrained`] and the non-blocking
/// [`deluge_streaming_queue_drained`].
fn queue_is_drained(mgr: *mut c_void) -> bool {
    // SAFETY: `mgr` is the boot-singleton manager; the predicate only reads per-slot queue state,
    // popping and mutating nothing.
    !unsafe { deluge_resource_loader_has_any(mgr) }
}

/// Enqueues `chunk_backing` for the fill task under a queue-owned lease and wakes the drain.
///
/// Shared by the blocking and non-blocking request paths so both hand the drain the same protocol.
/// The owned enqueue is what lets a fire-and-forget request survive the requester walking away --
/// see [`deluge_streaming_fill_chunk_blocking`]'s doc for why the un-owned form silently discarded
/// the overview pre-scan's work.
fn enqueue_chunk_for_fill(chunk_backing: *mut c_void) {
    // SAFETY: `deluge_streaming_resource_manager` returns the boot-singleton manager;
    // `chunk_backing` is a resident, leased `StreamedChunk*`.
    let mgr = unsafe { deluge_streaming_resource_manager() };
    if !mgr.is_null() {
        let slot = unsafe { deluge_resource_slot_of(mgr, chunk_backing) };
        unsafe { deluge_resource_loader_enqueue_owned(mgr, slot, BLOCKING_FILL_PRIORITY) };
    }
    FILL_WAKE.signal(());
}

/// Poll-until-`loaded` future the on-fiber wait in [`deluge_streaming_fill_chunk_blocking`] drives
/// through `block_on_fiber`. Each `Pending` suspends the fiber (via `block_on_fiber`'s own
/// `yield_now`), letting the executor run [`streaming_fill_task`] to drain the queue; the fiber is
/// re-polled by `worker_poll` and re-checks the flag. `remaining` bounds the spin so a chunk that
/// never lands (a persistently failing read) reports not-ready rather than wedging the fiber.
struct WaitChunkLoaded {
    chunk: *mut c_void,
    remaining: u32,
}

impl core::future::Future for WaitChunkLoaded {
    type Output = bool;

    fn poll(
        mut self: core::pin::Pin<&mut Self>,
        _cx: &mut core::task::Context<'_>,
    ) -> core::task::Poll<bool> {
        if chunk_is_loaded(self.chunk) {
            return core::task::Poll::Ready(true);
        }
        if self.remaining == 0 {
            return core::task::Poll::Ready(false); // Gave up: repeated drains never landed it.
        }
        self.remaining -= 1;
        // Re-wake the drain in case it idled after a failed attempt re-queued this chunk at lowest
        // priority; the executor runs `streaming_fill_task` while this fiber is suspended.
        FILL_WAKE.signal(());
        core::task::Poll::Pending
    }
}

/// Fill a reserved chunk through the async drain, blocking on the worker fiber — the reader
/// range-fill's async-BSP path. See `streaming_fill.h`'s `deluge_streaming_fill_chunk_blocking` doc
/// for the full contract; in brief: enqueue + wake the drain, then on-fiber yield-wait until the
/// chunk's `loaded` flag flips (byte-equivalent to the synchronous fill, same `native_finish`
/// tail), off-fiber return false immediately (degrade-to-eventual — no stack to suspend, and a
/// non-yielding `block_on` here is the very livelock this replaces).
///
/// # Why the enqueue takes a queue-owned lease
///
/// `deluge_resource_loader_next` only serves a chunk with a live lease, and *silently de-queues* any
/// other. The off-fiber degrade returns false, whereupon the reader drops the lease it was holding
/// (`Reader::acquire_and_fill`'s `if !filled` arm) — so the entry this call had just enqueued was
/// discarded on the drain's very next poll, and the chunk never loaded. That is why the display-only
/// overview pre-scan never converged on device: every `slowRoutine` tick re-requested the same
/// cluster, re-enqueued it at the most-urgent priority against the live streaming queue, and threw
/// its own progress away. Enqueueing through `deluge_resource_loader_enqueue_owned` gives the queue
/// the lease it requires, so a fire-and-forget fill survives the requester walking away: the drain
/// lands it, `native_finish` marks it ready, and the scan's next tick takes a cache hit.
///
/// Both branches use the owned enqueue, so the drain has one protocol to honour rather than two.
/// On-fiber that is redundant but harmless (the suspended reader holds its own lease throughout).
#[unsafe(no_mangle)]
pub extern "C" fn deluge_streaming_fill_chunk_blocking(chunk_backing: *mut c_void) -> bool {
    if chunk_backing.is_null() {
        return false;
    }
    enqueue_chunk_for_fill(chunk_backing);

    if crate::fiber::on_fiber() {
        crate::fiber::block_on_fiber(WaitChunkLoaded {
            chunk: chunk_backing,
            remaining: BLOCKING_FILL_MAX_CYCLES,
        })
    } else {
        // Off the worker fiber (the display-only overview pre-scan): no stack to suspend. The chunk
        // is enqueued under a queue-owned lease (see above), so it loads without us; return
        // not-ready and the consumer's next tick finds it resident.
        false
    }
}

/// Poll-until-queue-empty future the offline drain in [`deluge_streaming_drain_queue_blocking`]
/// drives through `block_on_fiber`. Each `Pending` suspends the fiber (via `block_on_fiber`'s own
/// `yield_now`), letting the executor run [`streaming_fill_task`] to drain the queue; the fiber is
/// re-polled by `worker_poll` and re-checks. Same shape as [`WaitChunkLoaded`], but the readiness
/// predicate is "the loader queue is empty" (`deluge_resource_loader_has_any` false) rather than one
/// named chunk's `loaded` flag. `remaining` bounds the spin so a cluster whose read keeps failing
/// (and re-queuing itself) reports drained-with-cap rather than wedging the fiber.
struct WaitQueueDrained {
    mgr: *mut c_void,
    remaining: u32,
}

impl core::future::Future for WaitQueueDrained {
    type Output = bool;

    fn poll(
        mut self: core::pin::Pin<&mut Self>,
        _cx: &mut core::task::Context<'_>,
    ) -> core::task::Poll<bool> {
        if queue_is_drained(self.mgr) {
            return core::task::Poll::Ready(true); // Queue drained — nothing left to load.
        }
        if self.remaining == 0 {
            return core::task::Poll::Ready(false); // Gave up: a read kept failing and re-queuing.
        }
        self.remaining -= 1;
        // Wake the drain: the executor runs `streaming_fill_task` while this fiber is suspended.
        FILL_WAKE.signal(());
        core::task::Poll::Pending
    }
}

/// Drain the WHOLE loader queue through the async fill task, blocking on the worker fiber — the
/// offline stem-export drain (`StemExport::renderWait`'s async-BSP branch). See `streaming_fill.h`'s
/// `deluge_streaming_drain_queue_blocking` doc for the C-side contract; in brief: unlike
/// [`deluge_streaming_fill_chunk_blocking`] (which blocks on ONE named chunk), this wakes
/// `streaming_fill_task` and yield-waits until the loader queue is empty
/// (`deluge_resource_loader_has_any` false) — draining everything the preceding
/// `AudioEngine::routine()` enqueued. On-fiber it yields until
/// drained, bounded by [`BLOCKING_FILL_MAX_CYCLES`]; off-fiber it returns false immediately (no stack
/// to suspend — renderWait is always on-fiber, so this is only a safety net).
#[unsafe(no_mangle)]
pub extern "C" fn deluge_streaming_drain_queue_blocking() -> bool {
    // SAFETY: `deluge_streaming_resource_manager` returns the boot-singleton manager.
    let mgr = unsafe { deluge_streaming_resource_manager() };
    if mgr.is_null() {
        return false;
    }
    FILL_WAKE.signal(());

    if crate::fiber::on_fiber() {
        crate::fiber::block_on_fiber(WaitQueueDrained {
            mgr,
            remaining: BLOCKING_FILL_MAX_CYCLES,
        })
    } else {
        // Off the worker fiber: no stack to suspend. renderWait always runs on-fiber; this branch is
        // a safety net only.
        false
    }
}

// ── Non-blocking request/poll pairs ─────────────────────────────────────────
//
// The return-to-loop form of the two blocking waits above. A caller that cannot block -- because its
// predicate needs another task to be polled, and it is itself inside the executor's poll -- requests
// the work, returns to its own loop, and asks again next tick. Neither function ever waits.

/// Enqueues `chunk_backing` for filling and returns immediately.
///
/// Pair with [`deluge_streaming_chunk_ready`]. The chunk is enqueued under a queue-owned lease, so
/// the fill survives the requester returning to its loop.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_streaming_request_chunk(chunk_backing: *mut c_void) {
    if chunk_backing.is_null() {
        return;
    }
    enqueue_chunk_for_fill(chunk_backing);
}

/// True once a chunk requested via [`deluge_streaming_request_chunk`] has landed. Never waits.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_streaming_chunk_ready(chunk_backing: *mut c_void) -> bool {
    if chunk_backing.is_null() {
        return false;
    }
    chunk_is_loaded(chunk_backing)
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
    // SAFETY: returns the boot-singleton manager.
    let mgr = unsafe { deluge_streaming_resource_manager() };
    if mgr.is_null() {
        return true;
    }
    queue_is_drained(mgr)
}

// ── Always-compiled C ABI: per-asset fill-context table ─────────────────────
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
#[cfg(feature = "async_streaming_loader")]
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
#[cfg(feature = "async_streaming_loader")]
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
#[cfg(feature = "async_streaming_loader")]
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
// above (which compile under `async_streaming_loader` alone) so a plain host
// build of this feature — no `host_app` — never emits a reference to an
// undefined extern symbol. Also requires `async_streaming_loader` itself
// (`fill_once`/`FillOps` are gated on it) — see the module doc's "Two
// compilation tiers".
#[cfg(all(
    feature = "async_streaming_loader",
    any(target_os = "none", feature = "host_app")
))]
mod prod {
    use super::{FillOps, LOWEST_PRIORITY};
    use core::ffi::c_void;
    use deluge_sample_fill::StreamingFillDescriptor;

    unsafe extern "C" {
        fn deluge_streaming_resource_manager() -> *mut c_void;
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
            // SAFETY: returns the one process-wide GeneralMemoryAllocator
            // resource manager; no aliasing/ownership concern, it's a stable
            // singleton pointer.
            let mgr = unsafe { deluge_streaming_resource_manager() };

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
            #[cfg(all(target_os = "none", feature = "efatfs_streaming"))]
            {
                return crate::efatfs_fs::read_at(d.handle, d.byte_offset, buf).await;
            }
            #[cfg(all(feature = "host_app", feature = "efatfs_streaming"))]
            {
                return crate::efatfs_host_shim::read_at(d.handle, d.byte_offset, buf).await;
            }
            // No efatfs backing compiled in (should not occur once efatfs_streaming is default): fail the
            // read so the loader re-enqueues rather than silently reading nothing.
            #[cfg(not(feature = "efatfs_streaming"))]
            {
                let _ = (d, buf);
                false
            }
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

#[cfg(all(
    feature = "async_streaming_loader",
    any(target_os = "none", feature = "host_app")
))]
pub use prod::ProdOps;

/// The fill task: wakes on [`FILL_WAKE`], drains the loader queue via the real
/// [`ProdOps`], then goes back to sleep. Spawned in `main.rs` (device `main` +
/// host `host_app`) under `async_streaming_loader`; the C++ enqueue path
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
#[cfg(all(
    feature = "async_streaming_loader",
    any(target_os = "none", feature = "host_app")
))]
#[embassy_executor::task]
pub async fn streaming_fill_task() {
    FILL_WAKE.wait().await;
    let ops = ProdOps::new();
    loop {
        fill_once(&ops).await;
        FILL_WAKE.wait().await;
    }
}
