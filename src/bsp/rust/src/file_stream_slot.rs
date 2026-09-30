//! The stream channel's answer slot.
//!
//! Pure, so `tests/file_stream_host.rs` tests it without a card. The rules are `file_bytes`'s:
//! - a take with another operation's key leaves the answer for its owner;
//! - a take that finds its own answer stamped for an earlier card removes it and reports nothing.
//!
//! One rule is this channel's own. An `Open` answer carries a live stream handle, and the stream table has 8 slots.
//! Every successful `Open` answer that leaves the slot without being collected is kept as an orphan, stamped with
//! its card generation, and the channel's task closes it only while that card is still current. A remount already
//! resets the stream table on ejection and reuses freed indices, so a stale orphan's handle may by then name a
//! different, live stream on the new card; closing it would close that stream instead.

/// Same bound as every other channel's path: `DELUGE_PATH_MAX` (256 bytes) less the NUL. Pinned by `abi_pins.rs`.
pub const MAX_PATH_CHARS: usize = 255;
pub type WritePath = heapless::String<MAX_PATH_CHARS>;

/// The stream table's size. No more handles than this can be open, so no more can be orphaned.
const MAX_ORPHANS: usize = 8;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum OpKind {
    Open,
    ReadAt,
    WriteAt,
    Truncate,
    Close,
    Stats,
}

/// Which operation an answer belongs to.
/// - Handle operations carry their handle and an empty path.
/// - `Open` carries the path it opens, so an answer is collected only by the caller that asked for that file.
/// - `Stats` carries neither: the channel is single-flight, and its collect names no file.
#[derive(Clone, PartialEq, Eq, Debug)]
pub struct OpKey {
    pub kind: OpKind,
    pub handle: u32,
    pub path: WritePath,
}

impl OpKey {
    /// `None` when `path` is longer than [`MAX_PATH_CHARS`].
    pub fn new(kind: OpKind, handle: u32, path: &str) -> Option<Self> {
        let mut stored = WritePath::new();
        stored.push_str(path).ok()?;
        Some(Self {
            kind,
            handle,
            path: stored,
        })
    }
}

/// Whether an operation a collect found no answer for is still coming, from what the request and in-flight slots
/// showed: `Some(true)` the operation's key is there, `Some(false)` it is not, `None` the slot's lock was contended.
///
/// Contended counts as coming. The task hands a request from the request slot to the in-flight slot, and an answer
/// from the in-flight slot to the result slot, while holding locks; a collect that meets one of those locks is
/// looking at a handoff, not at an absence. Reporting "not there" would send the caller to ask again for an operation
/// already under way -- and a CREATE_NEW open asked again finds its own file and fails with EXISTS.
pub fn still_coming(queued: Option<bool>, running: Option<bool>) -> bool {
    queued != Some(false) || running != Some(false)
}

/// What a take found.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Take {
    /// This operation's answer: the handle, byte count or free-cluster count.
    Ready(u32),
    /// This operation failed. The payload is the `Err` it was published with: for an `Open`, the `DelugeStatus` it
    /// failed with, as its byte; 0 for the others.
    Failed(u32),
    /// No answer for this key and this card.
    Absent,
}

/// A stream answer: the `Ok` payload is a handle, byte count or free-cluster count. Only a successful `Open` carries a
/// handle to release.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct StreamAnswer {
    kind: OpKind,
    result: Result<u32, u32>,
}

impl deluge_channel::Answer for StreamAnswer {
    fn orphan(&self) -> Option<u32> {
        match (self.kind, self.result) {
            (OpKind::Open, Ok(handle)) => Some(handle),
            _ => None,
        }
    }
}

pub struct AnswerSlot {
    inner: deluge_channel::AnswerSlot<OpKey, StreamAnswer>,
    /// Orphans this slot has retired, each stamped with the card generation its `Open` was answered under. See
    /// `deluge_channel::Orphan`.
    orphans: heapless::Vec<deluge_channel::Orphan, MAX_ORPHANS>,
}

impl AnswerSlot {
    pub const fn new() -> Self {
        Self {
            inner: deluge_channel::AnswerSlot::new(),
            orphans: heapless::Vec::new(),
        }
    }

    pub fn is_empty(&self) -> bool {
        self.inner.is_empty()
    }

    /// Hold `key`'s answer, stamped with the card generation its request was made under. Whatever the slot held
    /// before is dropped, and orphaned if it was a live `Open`.
    pub fn publish(&mut self, key: OpKey, result: Result<u32, u32>, generation: u32) {
        let kind = key.kind;
        if let Some(orphan) = self
            .inner
            .publish(key, StreamAnswer { kind, result }, generation)
        {
            self.push_orphan(orphan);
        }
    }

    /// Take `key`'s answer if it is stamped `generation`. An answer for another key is left alone. An answer for
    /// this key stamped for another card is removed and orphaned if it was a live `Open`.
    pub fn take(&mut self, key: &OpKey, generation: u32) -> Take {
        match self.inner.take(key, generation) {
            deluge_channel::Take::Ready(StreamAnswer {
                result: Ok(value), ..
            }) => Take::Ready(value),
            deluge_channel::Take::Ready(StreamAnswer {
                result: Err(error), ..
            }) => Take::Failed(error),
            deluge_channel::Take::Absent { orphan } => {
                if let Some(orphan) = orphan {
                    self.push_orphan(orphan);
                }
                Take::Absent
            }
        }
    }

    /// Drop the answer if it is `key`'s.
    pub fn discard(&mut self, key: &OpKey) {
        if let Some(orphan) = self.inner.discard(key) {
            self.push_orphan(orphan);
        }
    }

    /// Empty the slot.
    pub fn drain(&mut self) {
        if let Some(orphan) = self.inner.drain() {
            self.push_orphan(orphan);
        }
    }

    /// A handle an uncollected `Open` answer opened, for the task to close — but only when it is still stamped
    /// for `generation`. An orphan stamped for an earlier card is dropped without being returned: a remount
    /// already reset that card's stream table and may have reused its index for a different, live stream, so
    /// closing it now would close the wrong file.
    pub fn take_orphan(&mut self, generation: u32) -> Option<u32> {
        while let Some(orphan) = self.orphans.pop() {
            if orphan.generation == generation {
                return Some(orphan.handle);
            }
        }
        None
    }

    /// Whether any orphan is queued, current or stale. Stale orphans still need a caller to drain them (they are
    /// dropped, not returned, by [`Self::take_orphan`]), so this does not filter by generation.
    pub fn has_orphans(&self) -> bool {
        !self.orphans.is_empty()
    }

    fn push_orphan(&mut self, orphan: deluge_channel::Orphan) {
        let kept = self.orphans.push(orphan);
        debug_assert!(
            kept.is_ok(),
            "more orphans than the stream table has handles"
        );
    }
}
