use crate::residency::{Get, RegionPin, Residency};
use core::ffi::c_void;
use deluge_resource::facade::{Lease, Resource};
use deluge_resource::DelugeResource;

// A C-ABI entry point the APP provides in production
// (`src/deluge/storage/audio/stream/async_fill.cpp`). This crate's own `cargo test` binary links
// `deluge_resource` (and this crate) standalone, with no app around to supply it, so a
// `#[cfg(test)]` stub below stands in — mirroring the `host_critical_section_stubs` pattern in
// `lib.rs`.
unsafe extern "C" {
    /// Wakes the async fill task (mirrors `SampleStream::get_cluster`'s post-enqueue
    /// signal) so an enqueued-but-unwoken chunk doesn't stall on `FILL_WAKE`.
    fn deluge_streaming_signal_fill();
}

// Test-only stand-in for the C-ABI symbol above, so this crate's host unit tests link standalone
// (see the comment on the `extern "C"` block).
#[cfg(test)]
mod host_streaming_stubs {
    #[unsafe(no_mangle)]
    extern "C" fn deluge_streaming_signal_fill() {}
}

/// Production residency provider: composes the `deluge_resource` facade. `Ready` =
/// a cache hit (`acquire_leased`); `Loading` = a fresh reservation (`request` +
/// `loader_enqueue`); `Unavailable` = out of range or the manager could not reserve.
/// Real fill (the loader turning a Loading chunk Ready via efatfs) is a later task —
/// this provider only schedules; tests drive readiness through the manager directly.
///
/// `cluster_size`/`num_clusters` are supplied by the caller (the cursor, which owns
/// the `Geometry` and derives `num_clusters` from it — `ceil((audio_data_start +
/// audio_data_length) / cluster_size)`, the file's clusters, sentinel/zero-length
/// treated as "caller decides"), not recomputed
/// here: this provider does no length math of its own, keeping `resident_bytes_for`
/// in `geometry.rs` the single home of the short-last-cluster arithmetic (and any
/// cluster-count arithmetic that piggybacks on the same geometry).
pub struct ManagerResidency {
    handle: *mut DelugeResource,
    asset: u32,
    cluster_size: usize,
    num_clusters: u32,
}

impl ManagerResidency {
    /// # Safety
    /// `handle` must be a live `deluge_resource` handle valid for this provider's
    /// whole life (a boot singleton in production).
    pub unsafe fn new(
        handle: *mut DelugeResource,
        asset: u32,
        cluster_size: usize,
        num_clusters: u32,
    ) -> Self {
        ManagerResidency {
            handle,
            asset,
            cluster_size,
            num_clusters,
        }
    }

    fn resource(&self) -> Resource<'_> {
        // SAFETY: `self.handle` is live per the `new` contract.
        unsafe { Resource::from_handle(self.handle) }
    }
}

/// A pinned region for `ManagerResidency`: the RAII `Lease` (release on drop) + the
/// handle so readiness + token stay LIVE queries (re-read through the manager, never
/// cached at acquire time).
pub struct ManagerPin {
    lease: Lease,
    index: u32,
    handle: *mut DelugeResource,
}
impl ManagerPin {
    fn resource(&self) -> Resource<'_> {
        // SAFETY: same boot-singleton contract as ManagerResidency::resource.
        unsafe { Resource::from_handle(self.handle) }
    }
}
impl RegionPin for ManagerPin {
    fn index(&self) -> u32 {
        self.index
    }
    fn is_ready(&self) -> bool {
        self.resource().is_ready(self.lease.chunk())
    }
    fn payload(&self) -> *const u8 {
        // SAFETY: the chunk is lease-pinned resident for this pin's lifetime, so its backing is a
        // live StreamedChunk constructed by `deluge_streaming_chunk_construct` (registered from
        // `chunk_residency.cpp`); `deluge_sample_fill::chunk::payload` returns its payload base.
        unsafe {
            deluge_sample_fill::chunk::payload(self.lease.chunk().as_ptr() as *mut c_void)
                as *const u8
        }
    }
    fn token(&self) -> u64 {
        self.resource().pin_token(self.lease.chunk())
    }
}

impl Residency for ManagerResidency {
    type Pin = ManagerPin;
    fn acquire(&self, index: u32, priority: u32) -> Get<ManagerPin> {
        if index >= self.num_clusters {
            return Get::Unavailable;
        }
        let res = self.resource();
        // Cache hit (resident + ready) -> Ready, pinned by the returned Lease.
        if let Some(lease) = res.acquire_leased(self.asset, index) {
            return Get::Ready(ManagerPin {
                lease,
                index,
                handle: self.handle,
            });
        }
        // Not ready: reserve (idempotent on an in-flight chunk) + schedule -> Loading.
        match res.request(self.asset, index, self.cluster_size) {
            Some(lease) => {
                let slot = res.slot_of(lease.chunk());
                res.loader_enqueue(slot, priority);
                // SAFETY: no-arg C-ABI wake; mirrors SampleStream::get_cluster's post-enqueue signal.
                unsafe { deluge_streaming_signal_fill() };
                Get::Loading(ManagerPin {
                    lease,
                    index,
                    handle: self.handle,
                })
            }
            None => Get::Unavailable,
        }
    }
    fn num_clusters(&self) -> u32 {
        self.num_clusters
    }
    fn retain_token(&self, token: u64) {
        self.resource().retain_token(token);
    }
    fn release_token(&self, token: u64) {
        self.resource().release_token(token);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::geometry::Geometry;
    use crate::residency::{Get, Residency};
    extern crate std;

    use core::ffi::c_void;
    use deluge_resource::value::COST_IO;
    use deluge_resource::DelugeResource;
    use std::vec::Vec;

    const CLUSTER_SIZE: usize = 16;

    /// `construct` seeds a per-index ramp: `dest[b] = index as u8 + b as u8`.
    unsafe extern "C" fn make_ramp_construct(
        _ctx: *mut c_void,
        _owner: *mut c_void,
        index: u32,
        dest: *mut u8,
    ) {
        for b in 0..CLUSTER_SIZE {
            // SAFETY: `dest` is the manager's just-allocated `CLUSTER_SIZE`-byte backing
            // for this chunk (per `ConstructFn`'s contract).
            unsafe {
                *dest.add(b) = index.wrapping_add(b as u32) as u8;
            }
        }
    }

    unsafe extern "C" fn mock_materialize(
        _ctx: *mut c_void,
        _owner: *mut c_void,
        index: u32,
        dest: *mut u8,
        len: usize,
    ) -> bool {
        for b in 0..len {
            // SAFETY: `dest[..len]` is the manager's just-allocated backing for this
            // chunk (per `MaterializeFn`'s contract).
            unsafe {
                *dest.add(b) = index.wrapping_add(b as u32) as u8;
            }
        }
        true
    }

    /// A manager over a throwaway test heap, mirroring `deluge_resource::facade`'s own
    /// test harness (a 16-aligned arena kept alive alongside the handle).
    struct TestHeap {
        _buf: Vec<u128>,
    }

    /// Build a manager over a fresh test heap and leak its backing arena for the
    /// process's remaining life — mirrors the boot-singleton contract `ManagerResidency`
    /// requires of its handle, and keeps the test helpers infallible/`'static`.
    fn test_manager_handle() -> *mut DelugeResource {
        let words = (256 * 1024usize).div_ceil(16);
        let mut buf: Vec<u128> = std::vec![0u128; words];
        let ptr = buf.as_mut_ptr() as *mut u8;
        // SAFETY: `buf` is leaked below so the arena stays alive for the whole test
        // binary's life, matching `ManagerResidency::new`'s boot-singleton contract.
        let h = unsafe { deluge_alloc::deluge_heap_create(ptr, words * 16) };
        std::mem::forget(TestHeap { _buf: buf });
        // SAFETY: `h` is the live heap handle just created above.
        let handle = unsafe { deluge_resource::deluge_resource_create(h, 4, 16) };
        assert!(!handle.is_null());
        handle
    }

    /// Define a requestable asset whose `construct` seeds `make_ramp(index, 16)`.
    fn define_ramp_asset(h: *mut DelugeResource) -> u32 {
        // SAFETY: `h` is a live handle (from `test_manager_handle`); the callbacks
        // have the required C-ABI signature.
        let asset = unsafe {
            deluge_resource::deluge_resource_define_asset(
                h,
                core::ptr::null_mut(),
                Some(mock_materialize),
                None,
                core::ptr::null_mut(),
                COST_IO,
                deluge_resource::manager::BACKING_HEAP,
            )
        };
        // SAFETY: `h`/`asset` are live/valid per the call above.
        unsafe {
            deluge_resource::deluge_resource_set_construct(h, asset, Some(make_ramp_construct));
        }
        asset
    }

    /// Drive readiness through the manager directly (standing in for the loader this
    /// provider only schedules — real fill is a later task).
    fn mark_index_ready(h: *mut DelugeResource, asset: u32, index: u32) {
        // SAFETY: `h` is a live handle; `try_acquire`/`mark_ready` are the same
        // FFI-safe C-ABI calls the facade wraps.
        let ptr = unsafe { deluge_resource::deluge_resource_try_acquire(h, asset, index) };
        let ptr = if ptr.is_null() {
            // Not resident yet (no in-flight `request`): reserve it first.
            // SAFETY: `h`/`asset` are live/valid.
            let p =
                unsafe { deluge_resource::deluge_resource_request(h, asset, index, CLUSTER_SIZE) };
            assert!(!p.is_null(), "reserve for mark-ready failed");
            p
        } else {
            ptr
        };
        // SAFETY: `h`/`ptr` are live/valid per the calls above.
        unsafe { deluge_resource::deluge_resource_mark_ready(h, ptr) };
        // The lease taken by `try_acquire`/`request` above is ours to drop — release it
        // so it doesn't skew the test's subsequent lease-count-driven acquires.
        // SAFETY: `h`/`ptr` are live/valid.
        unsafe { deluge_resource::deluge_resource_release(h, ptr) };
    }

    #[test]
    fn acquire_reports_ready_loading_unavailable_and_pins() {
        let h = test_manager_handle(); // *mut DelugeResource over a test heap
        let asset = define_ramp_asset(h); // construct seeds make_ramp(index, 16)
        let geo = Geometry {
            audio_data_start_bytes: 0,
            audio_data_length_bytes: 16 * 8,
            cluster_size_bytes: 16,
            byte_depth: 2,
            num_channels: 1,
            raw_data_format: 0,
        };
        // SAFETY: h is a live handle for the test's duration.
        let res = unsafe {
            ManagerResidency::new(
                h,
                asset,
                geo.cluster_size_bytes as usize,
                (geo.audio_data_length_bytes / geo.cluster_size_bytes as u64) as u32,
            )
        };
        // First acquire of index 0: not resident -> request reserves it -> Loading.
        match res.acquire(0, 0) {
            Get::Loading(pin) => {
                assert_eq!(pin.index(), 0);
                assert!(!pin.is_ready());
            }
            _ => panic!("expected Loading"),
        }
        // Mark it ready through the manager, then a fresh acquire hits Ready.
        mark_index_ready(h, asset, 0);
        match res.acquire(0, 0) {
            Get::Ready(pin) => {
                assert_eq!(pin.index(), 0);
                assert!(pin.is_ready());
                assert_ne!(pin.token(), 0);
            }
            _ => panic!("expected Ready"),
        }
        // Out-of-range index -> Unavailable (num_clusters guard).
        assert!(matches!(res.acquire(100, 0), Get::Unavailable));
        assert_eq!(res.num_clusters(), 8);
    }
}
