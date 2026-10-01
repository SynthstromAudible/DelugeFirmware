//! `streaming_loader::fill_once`, the cluster-fill drain loop, driven by an in-memory [`FakeOps`]
//! queue: each path's call sequence, and that the queue-owned lease is released on every path but
//! the re-enqueue.

use core::ffi::c_void;
use std::cell::{Cell, RefCell};

use deluge_bsp_rust::app_loop;
use deluge_bsp_rust::storage::streaming_loader;

use streaming_loader::{FillOps, StreamingFillDescriptor, fill_once};

/// One call `FakeOps` observed; tests assert on the sequence.
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

/// A fake queued cluster, owning a real `num_sectors * 512`-byte buffer for `fill_once` to write.
struct FakeChunk {
    id: usize,
    num_sectors: u32,
    begin_ok: bool,
    /// What `is_unloadable` reports for this chunk.
    unloadable: bool,
    /// What `external_lease_count` reports for this chunk.
    lease_count: u32,
    /// Whether the queue holds its own lease on this chunk; `release_owned` clears it.
    queue_leased: Cell<bool>,
    buf: RefCell<Vec<u8>>,
    /// Non-zero makes the fake `read` copy from `FakeOps::efatfs_file`; zero (the default) makes
    /// it stamp a pattern.
    handle: u32,
    /// The file byte offset `begin`'s descriptor reports.
    byte_offset: u32,
}

/// The `FillOps` test double: an in-memory priority queue (lower is more urgent, as in the real
/// loader queue) that records every call.
struct FakeOps {
    chunks: Vec<Box<FakeChunk>>,
    /// (chunk id, priority) pairs still queued.
    pending: RefCell<Vec<(usize, u32)>>,
    calls: RefCell<Vec<Call>>,
    /// What the next `read()` call returns.
    read_result: RefCell<bool>,
    /// The "file" a read with `handle != 0` copies from.
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
    ///
    /// `ptr` must be one this `FakeOps` handed out from `next()`.
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
            // Stand-in for the efatfs `read_at`: copy from the "file" at the descriptor's offset.
            let start = d.byte_offset as usize;
            buf.copy_from_slice(&self.efatfs_file[start..start + buf.len()]);
            return true;
        }
        // Stamp a recognizable pattern, to show `fill_once` read into the descriptor's range.
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
        // Like `native_finish`: false exactly when `read_ok` is false.
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
        // Chunks are enqueued through the owned path, so the queue lease starts held.
        queue_leased: Cell::new(true),
        buf: RefCell::new(vec![0u8; (num_sectors as usize) * 512]),
        handle: 0,
        byte_offset: 100 + id as u32,
    }
}

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
    // A filled chunk is left ready but unpinned: a cache hit for the requester, still evictable.
    assert!(!ops.chunk_by_id(0).queue_leased.get());
}

/// A failed read of a still-wanted chunk skips `finish`, re-enqueues it at the lowest priority and
/// stops without another `next()`.
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
    // The one path that keeps the queue lease: without it the next `loader_next` would discard the
    // re-queued entry.
    assert!(ops.chunk_by_id(0).queue_leased.get());
}

/// A failed read of a chunk nobody else leases drops it and keeps draining; both chunks fail here,
/// so the whole queue drains.
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
    // Neither is left pinned: on a pulled card, abandoned chunks would otherwise fill the table
    // unevictably.
    assert!(!ops.chunk_by_id(0).queue_leased.get());
    assert!(!ops.chunk_by_id(1).queue_leased.get());
}

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

/// `fill_once` passes `begin`'s descriptor (`handle`, `byte_offset`) to `read` unchanged. The real
/// efatfs read is not exercised here.
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

/// `fill_once` wakes the app (`DELUGE_WAKE_STORAGE`) when it leaves the queue drained, but not on
/// the re-enqueue path, where one chunk is still waiting for a retry.
#[test]
fn fill_once_wakes_the_app_only_when_the_queue_drains() {
    let ops = FakeOps::new(vec![one_chunk(0, true)], /* read_result = */ true);
    ops.enqueue(0, 10);
    app_loop::reset_reasons_for_test();

    embassy_futures::block_on(fill_once(&ops));

    assert_ne!(
        app_loop::reasons_for_test() & app_loop::WAKE_STORAGE,
        0,
        "a drained queue must wake the app"
    );

    let mut chunk = one_chunk(1, true);
    chunk.lease_count = 1; // still wanted, so the failed read re-enqueues it
    let ops = FakeOps::new(vec![chunk], /* read_result = */ false);
    ops.enqueue(1, 10);
    app_loop::reset_reasons_for_test();

    embassy_futures::block_on(fill_once(&ops));

    assert_eq!(
        app_loop::reasons_for_test() & app_loop::WAKE_STORAGE,
        0,
        "a re-enqueued chunk awaiting retry must not wake the app"
    );
}
