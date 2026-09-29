//! The write channel's answer slot.
//!
//! Pure, so `tests/file_write_host.rs` tests it without a card. The rules are `file_bytes`'s:
//! - a take with another operation's key leaves the answer for its owner;
//! - a take that finds its own answer stamped for an earlier card removes it and reports nothing.
//!
//! One rule is this channel's own. An `Open` answer carries a live stream handle, and the stream table has 8 slots.
//! Every successful `Open` answer that leaves the slot without being collected is kept as an orphan, and the
//! channel's task closes it.

/// Same bound as every other channel's path: 256 bytes with the NUL.
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
    ReadFile,
    WriteFile,
}

/// Which operation an answer belongs to.
/// - Handle operations carry their handle and an empty path.
/// - `ReadFile` and `WriteFile` carry their path and handle 0.
/// - `Open` and `Stats` carry neither: the channel is single-flight, and their collects name no file.
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

/// What a take found.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Take {
    /// This operation's answer: the handle, byte count or free-cluster count.
    Ready(u32),
    /// This operation failed. The payload is the size a whole-file read needed, or 1 when a whole-file write found
    /// the name taken; 0 otherwise.
    Failed(u32),
    /// No answer for this key and this card.
    Absent,
}

struct Answer {
    key: OpKey,
    result: Result<u32, u32>,
    generation: u32,
}

pub struct AnswerSlot {
    answer: Option<Answer>,
    orphans: heapless::Vec<u32, MAX_ORPHANS>,
}

impl AnswerSlot {
    pub const fn new() -> Self {
        Self {
            answer: None,
            orphans: heapless::Vec::new(),
        }
    }

    pub fn is_empty(&self) -> bool {
        self.answer.is_none()
    }

    /// Hold `key`'s answer, stamped with the card generation its request was made under. Whatever the slot held
    /// before is dropped.
    pub fn publish(&mut self, key: OpKey, result: Result<u32, u32>, generation: u32) {
        self.drain();
        self.answer = Some(Answer {
            key,
            result,
            generation,
        });
    }

    /// Take `key`'s answer if it is stamped `generation`. An answer for another key is left alone. An answer for
    /// this key stamped for another card is removed.
    pub fn take(&mut self, key: &OpKey, generation: u32) -> Take {
        if !self.answer.as_ref().is_some_and(|a| a.key == *key) {
            return Take::Absent;
        }
        let answer = self.answer.take().expect("checked just above");
        if answer.generation != generation {
            self.retire(answer);
            return Take::Absent;
        }
        match answer.result {
            Ok(value) => Take::Ready(value),
            Err(payload) => Take::Failed(payload),
        }
    }

    /// Drop the answer if it is `key`'s.
    pub fn discard(&mut self, key: &OpKey) {
        if self.answer.as_ref().is_some_and(|a| a.key == *key) {
            self.drain();
        }
    }

    /// Empty the slot.
    pub fn drain(&mut self) {
        if let Some(answer) = self.answer.take() {
            self.retire(answer);
        }
    }

    /// A handle an uncollected `Open` answer opened, for the task to close.
    pub fn take_orphan(&mut self) -> Option<u32> {
        self.orphans.pop()
    }

    pub fn has_orphans(&self) -> bool {
        !self.orphans.is_empty()
    }

    fn retire(&mut self, answer: Answer) {
        if let (OpKind::Open, Ok(handle)) = (answer.key.kind, answer.result) {
            let kept = self.orphans.push(handle);
            debug_assert!(
                kept.is_ok(),
                "more orphans than the stream table has handles"
            );
        }
    }
}
