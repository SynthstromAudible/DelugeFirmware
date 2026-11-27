//! A sample's waveform peak index (`deluge_storage_sample_peaks.h`).
//!
//! One lock-free pyramid per sample, measured in frames so the same file gives the same index on any card: level 0
//! holds one 16-bit peak per 256 frames, and each level up one peak per 16 entries of the level below. The cluster
//! fill, the recorder and the background scan fold peaks in; the waveform columns read them out as a pure function
//! of the sample's data.
//!
//! An index survives on the card as a sidecar ([`sidecar`]): a header naming the file it belongs to, a table of
//! tagged sections, and its level 1 as the `PEAK` section; levels 2 and up are rebuilt by folding, as a scan builds
//! them. Level 0 is kept only around the windows read in the last [`KEEP_MS`] ([`PeakIndex::keep_level0`],
//! [`PeakIndex::trim_level0`]); a trimmed chunk is freed only once no fold that could have loaded it is in flight.
//!
//! The tier rules:
//! - Only thread mode allocates or frees ([`PeakIndex::create`], [`PeakIndex::reserve`],
//!   [`PeakIndex::ensure_level0`], [`PeakIndex::collect`]), through the callbacks [`mem::install`] takes.
//! - A fold ([`PeakIndex::fold_block`], [`PeakIndex::fold_frames`]) runs on any tier. It never allocates, never
//!   blocks and takes no lock: every value is an atomic folded with a compare-and-swap loop that only lowers the
//!   minimum and raises the maximum.
//! - A reference ([`PeakIndex::retain`], [`PeakIndex::release`]) is taken or dropped on any tier; the last drop queues
//!   the index for thread mode to free.
#![no_std]

pub mod abi;
pub mod column;
pub mod crc;
mod dir;
pub mod div;
mod index;
pub mod mem;
mod peak;
pub mod sidecar;
pub mod take;
pub mod tap;

pub use column::{ColumnPlan, FrameReads};
pub use index::{KEEP_MS, PeakIndex};
pub use peak::{Peak, peak_of_frames};

/// Frames per level-0 block, as a power of two: 256.
pub const BLOCK_FRAMES_LOG2: u32 = 8;
/// Children per entry, as a power of two: 16.
pub const FAN_LOG2: u32 = 4;
/// Entries per allocated chunk, as a power of two: 1,024.
pub const CHUNK_LOG2: u32 = 10;
/// The most levels an index has: room for `2^(8 + 4 · 9)` = 2^44 frames.
pub const MAX_LEVELS: u32 = 10;
