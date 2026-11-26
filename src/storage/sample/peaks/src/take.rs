//! A take's peaks, folded into its index as the recorder writes its clusters: whole 256-frame blocks in file order, the
//! header skipped, a frame split across two clusters carried to the next.
//!
//! The recorder writes 24-bit little-endian frames of one or two channels, on the card side (thread mode): each feed
//! reserves the index's room for the frames it folds.

use crate::{BLOCK_FRAMES_LOG2, Peak, PeakIndex};

const BYTE_DEPTH: usize = 3;

/// The fold's state between clusters.
#[derive(Debug)]
pub struct Take {
    file_byte: u64,
    carry: [u8; 2 * BYTE_DEPTH],
    carry_len: usize,
    block: u64,
    block_frames: u32,
    peak: Peak,
}

impl Default for Take {
    fn default() -> Self {
        Self {
            file_byte: 0,
            carry: [0; 2 * BYTE_DEPTH],
            carry_len: 0,
            block: 0,
            block_frames: 0,
            peak: Peak::EMPTY,
        }
    }
}

impl Take {
    /// The frames folded or counted so far.
    #[must_use]
    pub fn frames(&self) -> u64 {
        (self.block << BLOCK_FRAMES_LOG2) + u64::from(self.block_frames)
    }

    /// Folds the file's next bytes, from where the last call stopped (the first call: byte 0). Thread mode: it reserves
    /// the index's room for what it folds. With no index it still counts the bytes.
    #[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
    pub fn feed(
        &mut self,
        index: Option<&PeakIndex>,
        mut bytes: &[u8],
        audio_start: u32,
        channels: u8,
    ) {
        let stride = BYTE_DEPTH * usize::from(channels);
        if stride == 0 || stride > self.carry.len() {
            return;
        }
        // The header.
        if self.file_byte < u64::from(audio_start) {
            let skip =
                usize::try_from((u64::from(audio_start) - self.file_byte).min(bytes.len() as u64))
                    .unwrap_or(0);
            self.file_byte += skip as u64;
            bytes = &bytes[skip..];
        }
        self.file_byte += bytes.len() as u64;
        if let Some(index) = index {
            index.reserve(self.frames() + ((self.carry_len + bytes.len()) / BYTE_DEPTH) as u64 + 1);
        }
        // A frame the last call's bytes began.
        if self.carry_len > 0 {
            let take = (stride - self.carry_len).min(bytes.len());
            self.carry[self.carry_len..self.carry_len + take].copy_from_slice(&bytes[..take]);
            self.carry_len += take;
            bytes = &bytes[take..];
            if self.carry_len < stride {
                return;
            }
            let carried = self.carry;
            self.frame(index, &carried[..stride]);
            self.carry_len = 0;
        }
        let mut frames = bytes.chunks_exact(stride);
        for f in &mut frames {
            self.frame(index, f);
        }
        let rest = frames.remainder();
        self.carry[..rest.len()].copy_from_slice(rest);
        self.carry_len = rest.len();
    }

    #[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
    fn frame(&mut self, index: Option<&PeakIndex>, frame: &[u8]) {
        for s in frame.as_chunks::<BYTE_DEPTH>().0 {
            // A 24-bit sample's top 16 bits: its second and third bytes.
            let v = i16::from_le_bytes([s[1], s[2]]);
            self.peak = self.peak.fold(Peak { min: v, max: v });
        }
        self.block_frames += 1;
        if self.block_frames == 1 << BLOCK_FRAMES_LOG2 {
            if let Some(index) = index {
                index.fold_block(self.block, self.peak);
            }
            self.block += 1;
            self.block_frames = 0;
            self.peak = Peak::EMPTY;
        }
    }

    /// Folds the last, part-filled block. Call once the take's index is final. The count stays: [`Take::frames`] is
    /// still the take's length.
    #[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
    pub fn finish(&self, index: Option<&PeakIndex>) {
        if self.block_frames > 0
            && let Some(index) = index
        {
            index.fold_block(self.block, self.peak);
        }
    }
}
