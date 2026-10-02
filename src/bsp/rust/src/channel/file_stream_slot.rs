//! The stream channel's answer slot: pure, so it is host-tested without a card.
//!
//! It follows the shared slot rules in [`crate::channel`]. A successful `Open` answer carries a
//! live stream handle; one that leaves the slot uncollected is kept as an orphan stamped with its
//! card generation, and the channel's task closes it only while that card is still current. A
//! remount resets the stream table and reuses freed indices, so a stale orphan's handle may name a
//! different, live stream on the new card.

/// The longest path an `Open` takes: [`DELUGE_PATH_MAX`](crate::channel::DELUGE_PATH_MAX) less
/// the NUL.
pub const MAX_PATH_CHARS: usize = crate::channel::DELUGE_PATH_MAX - 1;
/// A path held inline in an [`OpKey`].
pub type WritePath = heapless::String<MAX_PATH_CHARS>;

/// The stream table's size. No more handles than this can be open, so no more can be orphaned.
const MAX_ORPHANS: usize = 8;

/// The stream channel's operations.
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
///
/// - Handle operations carry their handle and an empty path.
/// - `Open` carries the path it opens, so an answer is collected only by the caller that asked for
///   that file.
/// - `Stats` carries neither: the channel is single-flight, and its collect names no file.
#[derive(Clone, PartialEq, Eq, Debug)]
pub struct OpKey {
    pub kind: OpKind,
    pub handle: u32,
    pub path: WritePath,
}

impl OpKey {
    /// Builds a key, or `None` when `path` is longer than [`MAX_PATH_CHARS`].
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

/// Whether an operation a collect found no answer for is still coming, from what the request and
/// in-flight slots showed: `Some(true)` the key is there, `Some(false)` it is not, `None` the
/// slot's lock was contended.
///
/// Contended counts as coming: the task moves a request to the in-flight slot, and an answer to the
/// result slot, under those locks, so a contended lock is a handoff, not an absence. Reporting "not
/// there" would make the caller ask again for an operation already under way — and a `CREATE_NEW`
/// open asked again finds its own file and fails with `EXISTS`.
pub fn still_coming(queued: Option<bool>, running: Option<bool>) -> bool {
    queued != Some(false) || running != Some(false)
}

/// What a take found.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Take {
    /// This operation's answer: the handle, byte count or free-cluster count.
    Ready(u32),
    /// This operation failed. The payload is the `Err` it was published with: for an `Open`, the
    /// `DelugeStatus` byte it failed with; 0 for the others.
    Failed(u32),
    /// No answer for this key and this card.
    Absent,
}

/// A stream answer: the `Ok` payload is a handle, byte count or free-cluster count. Only a
/// successful `Open` carries a handle to release.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct StreamAnswer {
    kind: OpKind,
    result: Result<u32, u32>,
}

impl crate::channel::Answer for StreamAnswer {
    fn orphan(&self) -> Option<u32> {
        match (self.kind, self.result) {
            (OpKind::Open, Ok(handle)) => Some(handle),
            _ => None,
        }
    }
}

/// The stream channel's answer slot, plus the orphans it has retired.
pub struct AnswerSlot {
    inner: crate::channel::AnswerSlot<OpKey, StreamAnswer>,
    /// Retired orphans, each stamped with the card generation its `Open` was answered under.
    orphans: heapless::Vec<crate::channel::Orphan, MAX_ORPHANS>,
}

impl AnswerSlot {
    /// An empty slot with no orphans.
    pub const fn new() -> Self {
        Self {
            inner: crate::channel::AnswerSlot::new(),
            orphans: heapless::Vec::new(),
        }
    }

    /// Whether the slot holds no answer.
    pub fn is_empty(&self) -> bool {
        self.inner.is_empty()
    }

    /// Holds `key`'s answer, stamped with the card generation its request was made under. Whatever
    /// the slot held before is dropped, and orphaned if it was a live `Open`.
    pub fn publish(&mut self, key: OpKey, result: Result<u32, u32>, generation: u32) {
        let kind = key.kind;
        if let Some(orphan) = self
            .inner
            .publish(key, StreamAnswer { kind, result }, generation)
        {
            self.push_orphan(orphan);
        }
    }

    /// Takes `key`'s answer if it is stamped `generation`. An answer for another key is left alone;
    /// one for this key stamped for another card is removed, and orphaned if it was a live `Open`.
    pub fn take(&mut self, key: &OpKey, generation: u32) -> Take {
        match self.inner.take(key, generation) {
            crate::channel::Take::Ready(StreamAnswer {
                result: Ok(value), ..
            }) => Take::Ready(value),
            crate::channel::Take::Ready(StreamAnswer {
                result: Err(error), ..
            }) => Take::Failed(error),
            crate::channel::Take::Absent { orphan } => {
                if let Some(orphan) = orphan {
                    self.push_orphan(orphan);
                }
                Take::Absent
            }
        }
    }

    /// Drops the answer if it is `key`'s.
    pub fn discard(&mut self, key: &OpKey) {
        if let Some(orphan) = self.inner.discard(key) {
            self.push_orphan(orphan);
        }
    }

    /// Empties the slot.
    pub fn drain(&mut self) {
        if let Some(orphan) = self.inner.drain() {
            self.push_orphan(orphan);
        }
    }

    /// Returns an orphaned handle stamped `generation`, for the task to close. Orphans stamped for
    /// an earlier card are dropped unreturned: their index may now name a different, live stream.
    pub fn take_orphan(&mut self, generation: u32) -> Option<u32> {
        while let Some(orphan) = self.orphans.pop() {
            if orphan.generation == generation {
                return Some(orphan.handle);
            }
        }
        None
    }

    /// Whether any orphan is queued, current or stale. Stale ones still need [`Self::take_orphan`]
    /// to drop them, so this does not filter by generation.
    pub fn has_orphans(&self) -> bool {
        !self.orphans.is_empty()
    }

    fn push_orphan(&mut self, orphan: crate::channel::Orphan) {
        let kept = self.orphans.push(orphan);
        debug_assert!(
            kept.is_ok(),
            "more orphans than the stream table has handles"
        );
    }
}
