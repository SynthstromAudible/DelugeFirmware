//! The fixed-block drive's scheduler: which 128-frame blocks to render each time the codec starts a
//! block, and whether it reached one that was not written (an overrun).
//!
//! Pure, so the host tests cover it; the device drive feeds it the DMA's play position and the
//! time since its last wake. The position alone cannot tell one lap of the ring from several, so a
//! wake late by more than a ring is unwrapped by the elapsed time.

/// Frames per block.
pub const BLOCK_FRAMES: usize = 128;
/// Blocks in the transmit ring.
pub const RING_BLOCKS: usize = 8;
/// Blocks kept written ahead of the one the codec is playing.
pub const QUEUE_BLOCKS: usize = 3;
/// One block's duration at 44.1 kHz, in µs (128 / 44100 s, rounded down).
const BLOCK_US: u64 = 2902;

/// What one wake renders.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Plan {
    /// Blocks to render now, `0..=QUEUE_BLOCKS`.
    pub renders: u32,
    /// The ring block the first of them goes into; the rest follow it, wrapping.
    pub write_block: usize,
    /// Whether this wake found an overrun and skipped the blocks the codec already passed.
    pub resynced: bool,
    /// Blocks the resync skipped: never rendered, so the input those blocks would have read is
    /// skipped too, to keep input and output aligned. 0 without a resync.
    pub skipped: u64,
}

/// The scheduler's state: unwrapped counts of blocks written and started by the codec.
#[derive(Clone, Copy, Debug)]
pub struct BlockDrive {
    /// Blocks `..written` are written (unwrapped block numbers).
    written: u64,
    /// The block the codec is playing (unwrapped).
    played: u64,
    started: bool,
    overruns: u32,
}

impl BlockDrive {
    /// A drive that has written nothing.
    pub const fn new() -> Self {
        Self {
            written: 0,
            played: 0,
            started: false,
            overruns: 0,
        }
    }

    /// Forgets the play position, so the next [`plan`](Self::plan) anchors afresh as the first
    /// did. The overrun count is kept.
    pub fn restart(&mut self) {
        self.started = false;
    }

    /// Plans a wake: `play_frame` is the DMA's position in the ring, `elapsed_us` the time since
    /// the previous wake (ignored on the first).
    pub fn plan(&mut self, play_frame: usize, elapsed_us: u64) -> Plan {
        let play_block = (play_frame / BLOCK_FRAMES) % RING_BLOCKS;
        if !self.started {
            // The block playing now holds whatever the ring held; the queue starts after it.
            self.started = true;
            self.played = play_block as u64;
            self.written = self.played + 1;
        } else {
            let prev = (self.played % RING_BLOCKS as u64) as usize;
            let delta = ((play_block + RING_BLOCKS - prev) % RING_BLOCKS) as u64;
            // Whole rings the position cannot show: the elapsed time beyond the visible delta,
            // rounded to the nearest ring so timer jitter cannot gain or lose one.
            let by_time = elapsed_us / BLOCK_US;
            let ring = RING_BLOCKS as u64;
            let laps = (by_time.saturating_sub(delta) + ring / 2) / ring;
            self.played += delta + laps * ring;
        }
        let mut resynced = false;
        let mut skipped = 0;
        if self.played >= self.written {
            self.overruns = self.overruns.wrapping_add(1);
            skipped = self.played + 1 - self.written;
            self.written = self.played + 1;
            resynced = true;
        }
        // Blocks played + 1 ..= played + QUEUE_BLOCKS written: the next to write is one past them.
        let target = self.played + QUEUE_BLOCKS as u64 + 1;
        let renders = target.saturating_sub(self.written).min(QUEUE_BLOCKS as u64) as u32;
        Plan {
            renders,
            write_block: (self.written % RING_BLOCKS as u64) as usize,
            resynced,
            skipped,
        }
    }

    /// Notes one block rendered into the ring at the write position.
    pub fn rendered(&mut self) {
        self.written += 1;
    }

    /// Blocks written ahead of the one the codec plays, as of the last plan: the output queue.
    pub fn queued_blocks(&self) -> u32 {
        self.written.saturating_sub(self.played + 1) as u32
    }

    /// Overruns counted since start. Wraps.
    pub fn overruns(&self) -> u32 {
        self.overruns
    }
}

impl Default for BlockDrive {
    fn default() -> Self {
        Self::new()
    }
}
