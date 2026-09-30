//! The answer slot every single-flight storage channel keeps, and the vocabulary its collects answer in.
//!
//! A channel takes one request at a time and publishes one answer, keyed by what was asked and stamped with the
//! card generation it was asked under. The rules:
//! - A take with another key leaves the answer: it belongs to an operation that may be about to collect it.
//! - A take with this key but another generation removes the answer and reports nothing: it is from a card that
//!   has gone, and leaving it resident would let a later take find it.
//! - An answer that carries a resource (an open handle) and leaves the slot uncollected is an orphan. Every
//!   method that drops an answer returns its orphan, for the channel to release.

/// What a poll found: `libdeluge/channel.h`'s `DelugeChannelStatus`, one of the `DELUGE_CHANNEL_*` values below. The
/// BSP's `abi_pins.rs` pins each against the header.
pub type DelugeChannelStatus = u8;
/// The request is queued or running: poll again.
pub const DELUGE_CHANNEL_PENDING: DelugeChannelStatus = 0;
/// The answer is here, and this poll took it.
pub const DELUGE_CHANNEL_READY: DelugeChannelStatus = 1;
/// No request for this key and card is queued, running or answered: ask again.
pub const DELUGE_CHANNEL_UNKNOWN: DelugeChannelStatus = 2;
/// The request ran and failed, and this poll took the failure.
pub const DELUGE_CHANNEL_FAILED: DelugeChannelStatus = 3;

/// The longest path any channel takes, including the NUL. A longer path is refused, never truncated.
pub const DELUGE_PATH_MAX: usize = 256;

/// What a channel publishes.
pub trait Answer {
    /// A handle this answer carries, which must be closed if nobody collects the answer.
    fn orphan(&self) -> Option<u32> {
        None
    }
}

/// A handle an uncollected answer carried, stamped with the card generation that answer was published under.
///
/// The generation is the DROPPED answer's own stamp, not the generation a caller passed to whatever call dropped
/// it. A remount may already have reset the resource table this handle names and reused its index for something
/// live by the time the channel gets to release it, so the channel must compare this generation against the
/// current one before closing the handle — the same check `take` makes for values.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Orphan {
    pub handle: u32,
    pub generation: u32,
}

/// What a take found.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Take<V> {
    /// This key's answer, for this card.
    Ready(V),
    /// Nothing for this key and card. `orphan` is the handle of a stale answer this take removed.
    Absent { orphan: Option<Orphan> },
}

struct Entry<K, V> {
    key: K,
    value: V,
    generation: u32,
}

/// One answer, or none.
pub struct AnswerSlot<K, V> {
    entry: Option<Entry<K, V>>,
}

impl<K, V: Answer> AnswerSlot<K, V> {
    pub const fn new() -> Self {
        Self { entry: None }
    }

    pub fn is_empty(&self) -> bool {
        self.entry.is_none()
    }

    /// The key of the answer held, if any.
    pub fn key(&self) -> Option<&K> {
        self.entry.as_ref().map(|e| &e.key)
    }

    /// The card generation the answer held is stamped with, if any.
    pub fn generation(&self) -> Option<u32> {
        self.entry.as_ref().map(|e| e.generation)
    }

    /// Hold `key`'s answer, stamped `generation`. Returns the orphan of the answer it replaces.
    pub fn publish(&mut self, key: K, value: V, generation: u32) -> Option<Orphan> {
        let orphan = self.drain();
        self.entry = Some(Entry {
            key,
            value,
            generation,
        });
        orphan
    }

    /// Take `key`'s answer if it is stamped `generation`.
    pub fn take<Q: ?Sized>(&mut self, key: &Q, generation: u32) -> Take<V>
    where
        K: PartialEq<Q>,
    {
        if !self.entry.as_ref().is_some_and(|e| e.key == *key) {
            return Take::Absent { orphan: None };
        }
        let entry = self.entry.take().expect("checked just above");
        if entry.generation == generation {
            Take::Ready(entry.value)
        } else {
            let generation = entry.generation;
            Take::Absent {
                orphan: entry
                    .value
                    .orphan()
                    .map(|handle| Orphan { handle, generation }),
            }
        }
    }

    /// Drop the answer if it is `key`'s. Returns its orphan.
    pub fn discard<Q: ?Sized>(&mut self, key: &Q) -> Option<Orphan>
    where
        K: PartialEq<Q>,
    {
        if self.entry.as_ref().is_some_and(|e| e.key == *key) {
            self.drain()
        } else {
            None
        }
    }

    /// Empty the slot. Returns the orphan of what it held.
    pub fn drain(&mut self) -> Option<Orphan> {
        self.entry.take().and_then(|e| {
            let generation = e.generation;
            e.value.orphan().map(|handle| Orphan { handle, generation })
        })
    }
}

/// The async audio-file load task and its request/collect C ABI: owns the AWAITED
/// file I/O of a card sample's load — open a candidate, size it, walk its header
/// — and publishes a plain result the C++ side collects on its own tier. It calls
/// no C++ itself; see its module doc for the two constraints that force that. Compiled
/// on the Embassy BSP unconditionally (device, or host under `host_app`) so
/// `deluge_audio_load_request`/`_collect` are always defined --
/// the app declares no fallback for them, so omitting this module would leave them
/// undefined at link time. The loader has no switch -- see its module doc's "Always on".
pub mod audio_loader;
/// file_bytes.h — the whole-file byte channel: read, write or rename a small settings file on the
/// storage tier and hand the BYTES to a caller that must not wait on the card. Gated identically to
/// `file_directory` above, and to `card` — which reaches this module's `flush_tables` and so must
/// never be compiled without it. The four settings consumers (runtime feature settings, the MIDI
/// device table, MIDI follow's defaults, performance view) always enter this channel after boot: it
/// has no feature switch.
///
/// Unlike `file_prefix` it borrows nothing from `audio_loader`: reading a whole file and writing one
/// back are not `LoadOps`'s four operations, so it declares its own three-operation trait.
pub mod file_bytes;
/// file_directory.h — the single-slot directory channel: open a directory on the storage tier and
/// hand the ready handle to a caller that must not wait on the card. Gated identically to
/// `file_prefix` below, and to `card` — which reaches this module's `flush_tables` and so must
/// never be compiled without it. It has no feature switch: after boot, every browser listing, SysEx directory
/// request and recording-folder scan opens its directory through it.
///
/// Unlike `file_prefix` it borrows nothing from `audio_loader`: a directory open is not one of
/// `LoadOps`'s four file operations, so it declares its own two-operation trait.
pub mod file_directory;
/// file_prefix.h — the single-slot prefix channel: read the first N bytes of a file on the
/// storage tier and hand them to a caller that must not wait on the card. Gated identically to
/// `audio_loader` above, whose `LoadOps`/`ProdOps` its driver reuses, and to `card` — which
/// reaches this module's `flush_tables` and so must never be compiled without it. Its callers are
/// `LoadSongUI`'s song-artwork preview and `DxSyxBrowser`'s cartridge load. The channel has no
/// feature switch.
pub mod file_prefix;
/// The stream channel: request/collect over the recorder's stream-write handles. Compiled
/// unconditionally; the channel has no feature switch.
pub mod file_stream;
/// The stream channel's answer slot: pure, so it is host-tested without a card. Compiled unconditionally,
/// beside `file_stream` above, which it is the storage for.
pub mod file_stream_slot;
