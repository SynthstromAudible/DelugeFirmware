//! The storage tier's single-flight request/collect channels, and the answer slot they share.
//!
//! A channel takes one request at a time and publishes one answer, keyed by what was asked and
//! stamped with the card generation ([`crate::storage::card::generation`]) it was asked under:
//! - A take with another key leaves the answer: it belongs to an operation that may be about to
//!   collect it.
//! - A take with this key but another generation removes the answer and reports nothing: it is
//!   from a card that has gone, and leaving it resident would let a later take find it.
//! - An answer that carries a resource (an open handle) and leaves the slot uncollected is an
//!   orphan. Every method that drops an answer returns its orphan, for the channel to release.
//!
//! Every channel's `flush_tables` is called by `deluge_card_flush` when the card is ejected.

/// What a poll found: `libdeluge/channel.h`'s `DelugeChannelStatus`, one of the `DELUGE_CHANNEL_*`
/// values below. `abi::pins` pins each against the header.
pub type DelugeChannelStatus = u8;
/// The request is queued or running: poll again.
pub const DELUGE_CHANNEL_PENDING: DelugeChannelStatus = 0;
/// The answer is here, and this poll took it.
pub const DELUGE_CHANNEL_READY: DelugeChannelStatus = 1;
/// No request for this key and card is queued, running or answered: ask again.
pub const DELUGE_CHANNEL_UNKNOWN: DelugeChannelStatus = 2;
/// The request ran and failed, and this poll took the failure.
pub const DELUGE_CHANNEL_FAILED: DelugeChannelStatus = 3;

/// The longest path any channel takes, including the NUL. A longer path is refused, never
/// truncated.
pub const DELUGE_PATH_MAX: usize = 256;

/// What a channel publishes.
pub trait Answer {
    /// A handle this answer carries, which must be closed if nobody collects the answer.
    fn orphan(&self) -> Option<u32> {
        None
    }
}

/// A handle an uncollected answer carried, stamped with the card generation that answer was
/// published under.
///
/// The generation is the dropped answer's own stamp, not the one passed to the call that dropped
/// it. By the time the channel releases the handle, a remount may have reset the resource table and
/// reused its index for something live, so the channel must compare this generation against the
/// current one before closing the handle — the same check [`AnswerSlot::take`] makes for values.
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
    /// An empty slot.
    pub const fn new() -> Self {
        Self { entry: None }
    }

    /// Whether the slot holds no answer.
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

    /// Holds `key`'s answer, stamped `generation`. Returns the orphan of the answer it replaces.
    pub fn publish(&mut self, key: K, value: V, generation: u32) -> Option<Orphan> {
        let orphan = self.drain();
        self.entry = Some(Entry {
            key,
            value,
            generation,
        });
        orphan
    }

    /// Takes `key`'s answer if it is stamped `generation`.
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

    /// Drops the answer if it is `key`'s. Returns its orphan.
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

    /// Empties the slot. Returns the orphan of what it held.
    pub fn drain(&mut self) -> Option<Orphan> {
        self.entry.take().and_then(|e| {
            let generation = e.generation;
            e.value.orphan().map(|handle| Orphan { handle, generation })
        })
    }
}

pub mod audio_loader;
pub mod file_bytes;
pub mod file_directory;
pub mod file_prefix;
pub mod file_stream;
pub mod file_stream_slot;
