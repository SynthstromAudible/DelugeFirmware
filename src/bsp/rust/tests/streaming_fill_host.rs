//! Host unit test for `streaming_loader::fill_once` — the async
//! cluster-fill drain loop, exercised against an in-memory [`FakeOps`] double
//! rather than the real C++ resource manager/`StreamedChunk`. See
//! `src/streaming_loader.rs`'s module doc ("Why the drain loop is generic over
//! `FillOps`") for why: constructing real C++ objects (or mocking the
//! `#[no_mangle]` C symbols, which would collide with the real ones under
//! `host_app`) is impractical/unsafe for a host test, so the orchestration is
//! injected instead.
//!
//! `deluge-bsp-rust` is bin-only (no `[lib]` target — see `Cargo.toml`), so this
//! recompiles `src/streaming_loader.rs` unmodified into this test binary via
//! `#[path]`. Only the
//! `async_streaming_loader`-gated `fill_once`/`FillOps` orchestration is needed
//! here — the `host_app`-gated `prod` submodule (the real `extern "C"` wiring)
//! stays uncompiled, so this test never needs the real C++ app linked.

#![cfg(feature = "async_streaming_loader")]
#![cfg(not(target_os = "none"))]

use core::ffi::c_void;
use std::cell::{Cell, RefCell};

#[path = "../src/streaming_loader.rs"]
mod streaming_loader;

use streaming_loader::{FillOps, StreamingFillDescriptor, fill_once};

/// One call `FakeOps` observed, in order — the orchestration assertion surface.
#[derive(Debug, PartialEq, Eq, Clone)]
enum Call {
    Next,
    IsUnloadable(usize),
    Begin(usize),
    Read { byte_offset: u32, count: u32 },
    Finish { chunk: usize, read_ok: bool },
    LeaseCount(usize),
    ReleaseOwned(usize),
    EnqueueLowest(usize),
}

/// A fake queued cluster. Owns its own backing buffer so `begin`'s
/// `StreamingFillDescriptor::dest` points at real, correctly-sized memory —
/// `fill_once` really does write `num_sectors * 512` bytes into it via the
/// unsafe slice it builds from the descriptor.
struct FakeChunk {
    id: usize,
    num_sectors: u32,
    begin_ok: bool,
    /// What `is_unloadable` reports for this chunk.
    unloadable: bool,
    /// What `external_lease_count` reports for this chunk — leases held by anyone
    /// other than the load queue itself (only consulted by `fill_once` after a
    /// failed read).
    lease_count: u32,
    /// Whether the queue holds its own lease on this chunk
    /// (`deluge_resource_loader_enqueue_owned`). `release_owned` clears it, and
    /// tests assert it ends false on every terminal path — a queue lease left
    /// held would pin the real chunk against eviction for the rest of the run.
    queue_leased: Cell<bool>,
    buf: RefCell<Vec<u8>>,
    /// Non-zero selects the efatfs-handle read path in `begin`'s descriptor —
    /// see `fill_once_efatfs_handle_plumbs_descriptor_into_read`. Zero (the
    /// default via `one_chunk`) keeps every other test on the raw-sector path.
    handle: u32,
    /// File-relative byte offset `begin`'s descriptor reports alongside
    /// `handle`; only meaningful when `handle != 0`.
    byte_offset: u32,
}

/// The `FillOps` test double: an in-memory priority queue (lower number = more
/// urgent, matching `deluge_resource_loader_next`'s real ordering) plus a
/// recording of every call made, so tests can assert on the orchestration
/// itself rather than on any real I/O or C++ side effect.
struct FakeOps {
    chunks: Vec<Box<FakeChunk>>,
    /// (chunk id, priority) pairs still queued.
    pending: RefCell<Vec<(usize, u32)>>,
    calls: RefCell<Vec<Call>>,
    /// What the next `read()` call returns.
    read_result: RefCell<bool>,
    /// In-memory stand-in for the file `efatfs_fs::read_at` would read from on
    /// device. Only populated by the efatfs-handle plumbing test; every other
    /// test leaves it empty and never takes the `handle != 0` branch.
    efatfs_file: Vec<u8>,
}

impl FakeOps {
    fn new(chunks: Vec<FakeChunk>, read_result: bool) -> Self {
        let chunks: Vec<Box<FakeChunk>> = chunks.into_iter().map(Box::new).collect();
        Self {
            chunks,
            pending: RefCell::new(Vec::new()),
            calls: RefCell::new(Vec::new()),
            read_result: RefCell::new(read_result),
            efatfs_file: Vec::new(),
        }
    }

    fn enqueue(&self, id: usize, priority: u32) {
        self.pending.borrow_mut().push((id, priority));
    }

    fn chunk_by_id(&self, id: usize) -> &FakeChunk {
        self.chunks
            .iter()
            .find(|c| c.id == id)
            .expect("unknown fake chunk id")
    }

    /// # Safety
    /// `ptr` must be a `*mut c_void` this `FakeOps` itself handed out via
    /// `next()` (i.e. one of `self.chunks`' addresses).
    unsafe fn chunk_from_ptr(ptr: *mut c_void) -> &'static FakeChunk {
        unsafe { &*(ptr as *const FakeChunk) }
    }
}

impl FillOps for FakeOps {
    fn next(&self) -> *mut c_void {
        self.calls.borrow_mut().push(Call::Next);
        let mut pending = self.pending.borrow_mut();
        let Some((idx, _)) = pending
            .iter()
            .enumerate()
            .min_by_key(|(_, (_, priority))| *priority)
        else {
            return core::ptr::null_mut();
        };
        let (id, _) = pending.remove(idx);
        (self.chunk_by_id(id) as *const FakeChunk) as *mut c_void
    }

    fn is_unloadable(&self, chunk: *mut c_void) -> bool {
        // SAFETY: only ever called with a pointer this test double's `next()`
        // just returned.
        let fc = unsafe { Self::chunk_from_ptr(chunk) };
        self.calls.borrow_mut().push(Call::IsUnloadable(fc.id));
        fc.unloadable
    }

    fn begin(&self, chunk: *mut c_void) -> StreamingFillDescriptor {
        // SAFETY: only ever called with a pointer this test double's `next()`
        // just returned.
        let fc = unsafe { Self::chunk_from_ptr(chunk) };
        self.calls.borrow_mut().push(Call::Begin(fc.id));
        if !fc.begin_ok {
            return StreamingFillDescriptor {
                dest: core::ptr::null_mut(),
                num_sectors: 0,
                ok: false,
                handle: 0,
                byte_offset: 0,
            };
        }
        StreamingFillDescriptor {
            dest: fc.buf.borrow_mut().as_mut_ptr(),
            num_sectors: fc.num_sectors,
            ok: true,
            handle: fc.handle,
            byte_offset: fc.byte_offset,
        }
    }

    async fn read(&self, d: &StreamingFillDescriptor, buf: &mut [u8]) -> bool {
        self.calls.borrow_mut().push(Call::Read {
            byte_offset: d.byte_offset,
            count: d.num_sectors,
        });
        if d.handle != 0 {
            // Fake stand-in for the device-only `efatfs_fs::read_at`: copy
            // straight out of the in-memory "file" at the descriptor's byte
            // offset, proving `fill_once` threads `handle`/`byte_offset`
            // through to `read` untouched. See the module doc atop this file
            // and `fill_once_efatfs_handle_plumbs_descriptor_into_read` below.
            let start = d.byte_offset as usize;
            buf.copy_from_slice(&self.efatfs_file[start..start + buf.len()]);
            return true;
        }
        // Prove the descriptor's byte range really is the one `fill_once` reads
        // into: stamp a recognizable pattern rather than leaving it untouched.
        buf.fill(0xAB);
        *self.read_result.borrow()
    }

    fn finish(&self, chunk: *mut c_void, read_ok: bool) -> bool {
        // SAFETY: same pointer `begin` was just called with.
        let fc = unsafe { Self::chunk_from_ptr(chunk) };
        self.calls.borrow_mut().push(Call::Finish {
            chunk: fc.id,
            read_ok,
        });
        // Mirrors `native_finish`'s early-out: returns false exactly when
        // `read_ok` is false.
        read_ok
    }

    fn external_lease_count(&self, chunk: *mut c_void) -> u32 {
        // SAFETY: same pointer `begin`/`read` were just called with.
        let fc = unsafe { Self::chunk_from_ptr(chunk) };
        self.calls.borrow_mut().push(Call::LeaseCount(fc.id));
        fc.lease_count
    }

    fn release_owned(&self, chunk: *mut c_void) {
        // SAFETY: same pointer the other ops were just called with.
        let fc = unsafe { Self::chunk_from_ptr(chunk) };
        self.calls.borrow_mut().push(Call::ReleaseOwned(fc.id));
        fc.queue_leased.set(false);
    }

    fn enqueue_lowest(&self, chunk: *mut c_void) {
        // SAFETY: same pointer `begin`/`finish` were just called with.
        let fc = unsafe { Self::chunk_from_ptr(chunk) };
        self.calls.borrow_mut().push(Call::EnqueueLowest(fc.id));
        self.pending.borrow_mut().push((fc.id, u32::MAX));
    }
}

fn one_chunk(id: usize, begin_ok: bool) -> FakeChunk {
    let num_sectors = 2u32;
    FakeChunk {
        id,
        num_sectors,
        begin_ok,
        unloadable: false,
        lease_count: 1, // still wanted by default; the read-failure-drop test overrides this
        // Every chunk reaching the drain came through `loader_enqueue_owned` (the request path uses
        // it), so the queue lease starts held.
        queue_leased: Cell::new(true),
        buf: RefCell::new(vec![0u8; (num_sectors as usize) * 512]),
        handle: 0,
        byte_offset: 100 + id as u32,
    }
}

/// Happy path: one queued cluster → `begin` resolves it, `read` is awaited,
/// `finish(chunk, true)` runs, then the loop pops the now-empty queue and
/// returns.
#[test]
fn fill_once_happy_path_drains_one_cluster() {
    let ops = FakeOps::new(vec![one_chunk(0, true)], /* read_result = */ true);
    ops.enqueue(0, 10);

    embassy_futures::block_on(fill_once(&ops));

    assert_eq!(
        *ops.calls.borrow(),
        vec![
            Call::Next,
            Call::IsUnloadable(0),
            Call::Begin(0),
            Call::Read {
                byte_offset: 100,
                count: 2
            },
            Call::Finish {
                chunk: 0,
                read_ok: true
            },
            Call::ReleaseOwned(0),
            Call::Next,
        ]
    );
    // The read really did land in the chunk's own buffer.
    assert!(ops.chunk_by_id(0).buf.borrow().iter().all(|&b| b == 0xAB));
    assert!(ops.pending.borrow().is_empty());
    // The queue's own lease is gone: a filled chunk is left ready-but-unpinned, so a
    // fire-and-forget requester takes a cache hit on its next tick and eviction still works.
    assert!(!ops.chunk_by_id(0).queue_leased.get());
}

/// Read failure while still leased: `read` returns false → `finish` is NOT
/// called (it's the success-only convert/stitch/publish tail) → the lease
/// count is checked and found still > 0 → the cluster is re-enqueued at
/// `LOWEST_PRIORITY` (0xFFFF_FFFF) and the loop stops immediately — no
/// second `next()` call in the same `fill_once`.
#[test]
fn fill_once_read_failure_reenqueues_lowest_and_stops() {
    let mut chunk = one_chunk(0, true);
    chunk.lease_count = 1; // still wanted
    let ops = FakeOps::new(vec![chunk], /* read_result = */ false);
    ops.enqueue(0, 10);

    embassy_futures::block_on(fill_once(&ops));

    assert_eq!(
        *ops.calls.borrow(),
        vec![
            Call::Next,
            Call::IsUnloadable(0),
            Call::Begin(0),
            Call::Read {
                byte_offset: 100,
                count: 2
            },
            Call::LeaseCount(0),
            Call::EnqueueLowest(0),
        ]
    );
    assert_eq!(*ops.pending.borrow(), vec![(0, u32::MAX)]);
    // The one path that must NOT release: the queue entry lives on, so its lease must too, or the
    // re-queued chunk would be discarded by the next `loader_next` exactly as an unleased one is.
    assert!(ops.chunk_by_id(0).queue_leased.get());
}

/// Read failure while UNLEASED: `read` returns false, and by the time it's
/// checked the cluster has already dropped to 0 leases (already unwanted) →
/// no `finish`, no `enqueue_lowest` — the cluster is just dropped and the loop
/// keeps draining the next queued cluster (the `lease_count(...) == 0` case
/// keeps draining rather than stopping). Both queued clusters are
/// unleased-and-failing here so the whole queue drains to empty rather than
/// stopping after the first — the "continues" half of the behaviour,
/// complementing the single-chunk case in
/// `fill_once_read_failure_reenqueues_lowest_and_stops`.
#[test]
fn fill_once_read_failure_unleased_drops_and_continues() {
    let mut first = one_chunk(0, true);
    first.lease_count = 0; // dropped to 0 reasons while loading
    let mut second = one_chunk(1, true);
    second.lease_count = 0;
    let ops = FakeOps::new(vec![first, second], /* read_result = */ false);
    ops.enqueue(0, 10); // most urgent — popped (and dropped) first
    ops.enqueue(1, 20);

    embassy_futures::block_on(fill_once(&ops));

    assert_eq!(
        *ops.calls.borrow(),
        vec![
            Call::Next,
            Call::IsUnloadable(0),
            Call::Begin(0),
            Call::Read {
                byte_offset: 100,
                count: 2
            },
            Call::LeaseCount(0),
            Call::ReleaseOwned(0),
            Call::Next,
            Call::IsUnloadable(1),
            Call::Begin(1),
            Call::Read {
                byte_offset: 101,
                count: 2
            },
            Call::LeaseCount(1),
            Call::ReleaseOwned(1),
            Call::Next,
        ]
    );
    // Nothing was re-enqueued for either chunk — both were dropped, not requeued.
    assert!(ops.pending.borrow().is_empty());
    // And neither is left pinned. This is the bound on retrying a permanently failing read (a
    // pulled card): without the release, every abandoned chunk would keep its queue lease and the
    // table would fill with chunks that can never be evicted.
    assert!(!ops.chunk_by_id(0).queue_leased.get());
    assert!(!ops.chunk_by_id(1).queue_leased.get());
}

/// `begin` reports `ok = false` (unloadable / geometry error) → that chunk is
/// skipped outright (no `read`, no `finish`, no `enqueue_lowest`), and the loop
/// keeps draining the rest of the queue.
#[test]
fn fill_once_skips_chunk_when_begin_not_ok() {
    let ops = FakeOps::new(
        vec![one_chunk(0, false), one_chunk(1, true)],
        /* read_result = */ true,
    );
    ops.enqueue(0, 10); // most urgent — popped (and skipped) first
    ops.enqueue(1, 20);

    embassy_futures::block_on(fill_once(&ops));

    assert_eq!(
        *ops.calls.borrow(),
        vec![
            Call::Next,
            Call::IsUnloadable(0),
            Call::Begin(0),
            Call::ReleaseOwned(0),
            Call::Next,
            Call::IsUnloadable(1),
            Call::Begin(1),
            Call::Read {
                byte_offset: 101,
                count: 2
            },
            Call::Finish {
                chunk: 1,
                read_ok: true
            },
            Call::ReleaseOwned(1),
            Call::Next,
        ]
    );
    // The skipped chunk isn't left pinned either.
    assert!(!ops.chunk_by_id(0).queue_leased.get());
}

/// Proves the descriptor→read plumbing for the efatfs path: when `begin()`
/// yields a descriptor with `handle != 0` and a `byte_offset`, `fill_once`
/// threads that descriptor into `read()` untouched, and a fake file-backed
/// `read` (copying out of an in-memory `Vec<u8>` "file" at `d.byte_offset`)
/// fills the chunk's buffer with the right bytes.
///
/// Divergence from production: this proves the descriptor plumbing
/// (`handle`/`byte_offset` → `read` → `buf`) only — the REAL
/// `efatfs_fs::read_at` is device-only (`target_os = "none"`) and cannot be
/// called from a host test. It's covered on host by `fs_differential`'s
/// round-trip test, and separately on-device.
#[test]
fn fill_once_efatfs_handle_plumbs_descriptor_into_read() {
    let num_sectors = 2u32;
    let byte_offset = 4096u32;
    let read_len = (num_sectors as usize) * 512;
    let file: Vec<u8> = (0..byte_offset as usize + read_len)
        .map(|i| (i % 251) as u8)
        .collect();
    let expected = file[byte_offset as usize..byte_offset as usize + read_len].to_vec();

    let mut chunk = one_chunk(0, true);
    chunk.handle = 7;
    chunk.byte_offset = byte_offset;
    chunk.num_sectors = num_sectors;
    chunk.buf = RefCell::new(vec![0u8; read_len]);

    let mut ops = FakeOps::new(vec![chunk], /* read_result = */ true);
    ops.efatfs_file = file;
    ops.enqueue(0, 10);

    embassy_futures::block_on(fill_once(&ops));

    assert_eq!(*ops.chunk_by_id(0).buf.borrow(), expected);
    assert_eq!(
        *ops.calls.borrow(),
        vec![
            Call::Next,
            Call::IsUnloadable(0),
            Call::Begin(0),
            Call::Read {
                byte_offset: 4096,
                count: 2
            },
            Call::Finish {
                chunk: 0,
                read_ok: true
            },
            Call::ReleaseOwned(0),
            Call::Next,
        ]
    );
}

/// A chunk marked unloadable (`is_unloadable` reports true) is skipped right
/// after `next()` — no `begin`, no `read`, no `finish` — while a following
/// loadable chunk still drains normally.
#[test]
fn fill_once_skips_unloadable_chunk() {
    let mut unloadable = one_chunk(0, true);
    unloadable.unloadable = true;
    let ops = FakeOps::new(
        vec![unloadable, one_chunk(1, true)],
        /* read_result = */ true,
    );
    ops.enqueue(0, 10); // most urgent — popped (and skipped) first
    ops.enqueue(1, 20);

    embassy_futures::block_on(fill_once(&ops));

    assert_eq!(
        *ops.calls.borrow(),
        vec![
            Call::Next,
            Call::IsUnloadable(0),
            Call::ReleaseOwned(0),
            Call::Next,
            Call::IsUnloadable(1),
            Call::Begin(1),
            Call::Read {
                byte_offset: 101,
                count: 2
            },
            Call::Finish {
                chunk: 1,
                read_ok: true
            },
            Call::ReleaseOwned(1),
            Call::Next,
        ]
    );
    // The unloadable chunk isn't left pinned.
    assert!(!ops.chunk_by_id(0).queue_leased.get());
}
