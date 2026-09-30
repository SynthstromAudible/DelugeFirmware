//! The async cluster-fill task: drains the resource manager's loader queue, awaiting each SD read,
//! and the `libdeluge/streaming_fill.h` C ABI around it.
//!
//! These are the only definitions of the C-ABI symbols; there is no C++ fallback. The production
//! wiring (`ProdOps`, `streaming_fill_task`) needs the app's C symbols, so it exists only on device
//! and under `host_app`.
//!
//! The drain loop, [`fill_once`], is generic over [`FillOps`] so `tests/streaming_fill_host.rs` can
//! drive it with a fake queue: building real chunks and a seeded manager in a Rust test is
//! impractical, and mocking the C symbols would collide with the real ones under `host_app`.
//!
//! # Concurrency
//!
//! The fill task runs on the storage tier, not the thread-mode executor: on device on
//! `STREAM_EXEC`, a preemptive `InterruptExecutor`, and on host on the `"deluge-stream"` thread.
//! This is required: the task holds the efatfs FS mutex across its SD await, and thread-mode
//! callers reach the filesystem through a non-yielding `block_on` spin, which would deadlock
//! waiting on a task it starves.
//!
//! The manager pointer is `!Send`; cross-tier safety comes from `deluge_resource::sync`'s
//! critical-section serialization, not from sharing an executor with the C++ enqueue path.
use core::ffi::c_void;

use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::signal::Signal;

/// Wakes the fill task, e.g. when a cluster is enqueued.
///
/// Latching: a wake raised before the task first waits is kept, not lost.
pub static FILL_WAKE: Signal<CriticalSectionRawMutex, ()> = Signal::new();

/// Wakes the fill task; safe to call before the task exists.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_streaming_signal_fill() {
    FILL_WAKE.signal(());
}

/// The most urgent loader priority (lower is more urgent), for a chunk a caller is waiting on, so
/// it lands ahead of queued prefetches.
const BLOCKING_FILL_PRIORITY: u32 = 0;

/// Returns the resource-manager singleton (or null), untyped for the loader-queue C ABI.
fn resource_manager() -> *mut c_void {
    // SAFETY: no preconditions; returns the boot-singleton manager or null.
    unsafe { crate::sys::deluge_streaming_resource_manager() }.cast::<c_void>()
}

unsafe extern "C" {
    /// Returns the chunk-table slot backing `ptr`.
    fn deluge_resource_slot_of(mgr: *mut c_void, ptr: *mut c_void) -> u32;
    /// Enqueues the chunk at `slot` and takes a queue-owned lease, so the entry survives the
    /// requester dropping its own.
    fn deluge_resource_loader_enqueue_owned(mgr: *mut c_void, slot: u32, priority: u32);
    /// Returns whether any queued, leased chunk remains; pops nothing.
    fn deluge_resource_loader_has_any(mgr: *mut c_void) -> bool;
}

/// Returns whether the loader queue holds nothing outstanding.
fn queue_is_drained(mgr: *mut c_void) -> bool {
    // SAFETY: `mgr` is the boot-singleton manager; the predicate only reads per-slot queue state,
    // popping and mutating nothing.
    !unsafe { deluge_resource_loader_has_any(mgr) }
}

/// Enqueues `chunk_backing` for the fill task under a queue-owned lease and wakes the drain.
///
/// The owned lease is what lets a fire-and-forget request survive: `deluge_resource_loader_next`
/// silently de-queues any chunk without a live lease, so an un-owned entry whose requester dropped
/// its lease would be discarded unfilled.
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

/// Returns whether the loader queue holds nothing outstanding; never waits.
///
/// With no manager there is no queue, so this reports drained rather than loop a poller forever.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_streaming_queue_drained() -> bool {
    let mgr = resource_manager();
    if mgr.is_null() {
        return true;
    }
    queue_is_drained(mgr)
}

// Re-exported because it appears in `FillOps`'s signatures.
pub use deluge_sample_fill::StreamingFillDescriptor;

/// `kLowestLoaderPriority`: the re-enqueue priority for a chunk whose read failed while still
/// wanted, so it sinks behind everything else rather than being popped again at once.
const LOWEST_PRIORITY: u32 = 0xFFFF_FFFF;

/// The operations [`fill_once`] drains the loader queue through.
///
/// `chunk` values are opaque `StreamedChunk*` backing pointers from [`FillOps::next`], passed back
/// unchanged; [`fill_once`] never interprets them.
pub trait FillOps {
    /// Pops the most urgent queued, still-leased chunk; null when the queue is empty.
    fn next(&self) -> *mut c_void;
    /// Returns whether `chunk` has been marked unloadable since it was enqueued.
    fn is_unloadable(&self, chunk: *mut c_void) -> bool;
    /// Resolves `chunk`'s destination buffer and sector range.
    ///
    /// `ok == false` means skip the chunk (unloadable or a geometry error): no read, no `finish`.
    /// When `ok`, `dest` must be valid and exclusive for `num_sectors * 512` bytes.
    fn begin(&self, chunk: *mut c_void) -> StreamingFillDescriptor;
    /// Reads the data for `d` into `buf`, returning whether it succeeded.
    async fn read(&self, d: &StreamingFillDescriptor, buf: &mut [u8]) -> bool;
    /// Runs the convert/stitch/publish tail, which marks the chunk ready; called only after a
    /// successful read.
    fn finish(&self, chunk: *mut c_void, read_ok: bool) -> bool;
    /// Returns `chunk`'s hard-lease count excluding the load queue's own.
    ///
    /// Consulted after a failed read to choose drop or requeue. The queue's lease must be excluded:
    /// otherwise an abandoned owned-enqueue chunk never reaches 0 and is retried, and pinned,
    /// forever.
    fn external_lease_count(&self, chunk: *mut c_void) -> u32;
    /// Releases the queue-owned lease on `chunk`, if any.
    ///
    /// Must be called on every path that stops caring about `chunk`, or the lease pins it against
    /// eviction for good.
    fn release_owned(&self, chunk: *mut c_void);
    /// Re-enqueues `chunk` at `LOWEST_PRIORITY` after a failed read; any queue-owned lease stays
    /// held with the entry.
    fn enqueue_lowest(&self, chunk: *mut c_void);
}

/// Drains the loader queue, reading and publishing each queued chunk.
///
/// - An unloadable chunk, or one whose `begin` is not `ok`, is dropped.
/// - After a successful read, `finish` publishes the chunk.
/// - After a failed read, a chunk nobody else leases is dropped; one still wanted is re-enqueued
///   at lowest priority and the drain stops, rather than re-popping it until the card returns.
///
/// Every path except the re-enqueue releases the queue-owned lease; a missed release pins the chunk
/// for the rest of the run (on a pulled card, every queued chunk).
pub async fn fill_once<O: FillOps>(ops: &O) {
    loop {
        let chunk = ops.next();
        if chunk.is_null() {
            return;
        }

        if ops.is_unloadable(chunk) {
            // Already de-queued by `next()`, so skipping cannot loop.
            ops.release_owned(chunk);
            continue;
        }

        let d = ops.begin(chunk);
        if !d.ok {
            // Unloadable or a geometry error; already de-queued, so skip it.
            ops.release_owned(chunk);
            continue;
        }

        // SAFETY: `d.ok`, so per `FillOps::begin`'s contract `dest` is valid and exclusive for
        // `num_sectors * 512` bytes.
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
            // For a fire-and-forget enqueuer the chunk is now ready but unleased: a cache hit for
            // whoever asked, and evictable again (a requester that loses that race re-requests).
            ops.finish(chunk, true);
            ops.release_owned(chunk);
            continue;
        }

        // Read failed. Dropping a chunk nobody else leases also bounds retries of a permanently
        // failing read (a pulled card).
        if ops.external_lease_count(chunk) == 0 {
            ops.release_owned(chunk);
            continue;
        }
        ops.enqueue_lowest(chunk);
        return;
    }
}

// Production wiring: only where the app's C symbols are linked (device, and host under
// `host_app`).
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

    /// The production [`FillOps`]: the resource manager's loader queue and the efatfs streaming
    /// read.
    ///
    /// `!Send`/`!Sync` (it holds a raw manager pointer); used only by the fill task.
    pub struct ProdOps {
        mgr: *mut c_void,
    }

    impl ProdOps {
        /// Creates the ops over the resource-manager singleton.
        ///
        /// Call only after `deluge_app_init` has set the cluster size (see `streaming_fill_task`).
        pub fn new() -> Self {
            let mgr = super::resource_manager();
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
            // NONE (a sample still recording, or not yet opened) names no slot; the read fails.
            let Some(slot) = crate::abi::handle::from_abi(d.handle) else {
                return false;
            };
            #[cfg(target_os = "none")]
            return crate::storage::efatfs::device::read_at(slot, d.byte_offset, buf).await;
            #[cfg(feature = "host_app")]
            return crate::storage::efatfs::host::read_at(slot, d.byte_offset, buf).await;
        }

        fn finish(&self, chunk: *mut c_void, read_ok: bool) -> bool {
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

/// Milliseconds the fill task waits before each drain pass (host harness knob).
#[cfg(feature = "host_app")]
static FILL_LATENCY_MS: core::sync::atomic::AtomicU32 = core::sync::atomic::AtomicU32::new(0);

/// Makes the fill task wait `ms` milliseconds before each drain pass (host harness only; 0, the
/// default, means no wait).
///
/// Models the fill task's wait to be scheduled, so a gate can hold fills while a consumer ticks.
#[cfg(feature = "host_app")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_streaming_set_fill_latency_ms(ms: u32) {
    FILL_LATENCY_MS.store(ms, core::sync::atomic::Ordering::Relaxed);
}

/// Microseconds added to each chunk's read (host harness knob).
#[cfg(feature = "host_app")]
static FILL_READ_LATENCY_US: core::sync::atomic::AtomicU32 = core::sync::atomic::AtomicU32::new(0);

/// Makes every chunk's read take `us` microseconds longer (host harness only; 0, the default, adds
/// nothing).
///
/// Models the device card's read time, which the host card lacks. The wait is in the fill task, so
/// it holds nothing else up.
#[cfg(feature = "host_app")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_streaming_set_fill_read_latency_us(us: u32) {
    FILL_READ_LATENCY_US.store(us, core::sync::atomic::Ordering::Relaxed);
}

/// The fill task: on each [`FILL_WAKE`], drains the loader queue with [`ProdOps`], on the storage
/// tier.
///
/// [`ProdOps`] is built only after the first wake. Building it reads `Cluster::size`, which
/// `deluge_app_init` writes during boot on another executor/thread; constructing earlier races
/// that write and could use the default instead of the card's cluster size. Nothing signals
/// [`FILL_WAKE`] until the C++ enqueue path runs, after init, and the signal latches, so no wake
/// is lost.
#[cfg(any(target_os = "none", feature = "host_app"))]
#[embassy_executor::task]
pub async fn streaming_fill_task() {
    // Reports on RTT which tier the device derives here; expected `Storage`.
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
