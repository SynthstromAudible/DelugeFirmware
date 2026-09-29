//! The differential's OWN, independent reimplementation of the current C++ read path's
//! frame -> (cluster, byte-offset) mapping — deliberately NOT shared with
//! `deluge_sample_reader::reader`'s own (private) `locate`/`Geometry`, the very code this crate's
//! differential exists to check: an oracle that borrowed the unit under test's own mapping code
//! would prove nothing about a bug IN that mapping. Mirrors the derivation
//! `deluge_sample_reader::reader::Geometry`'s own doc cites — `sample.cpp`'s
//! `sourceBytePos = audioDataStartPosBytes + startPosSamples * bytesPerSample`,
//! `sourceClusterIndex = sourceBytePos >> Cluster::size_magnitude`,
//! `bytePosWithinCluster = sourceBytePos & (Cluster::size - 1)` — using plain `/`/`%` here too
//! (equivalent for the power-of-two `Cluster::size` every real geometry uses).

/// The subset of `DelugeStreamingFillContext` this crate's oracle needs to map a frame index to a
/// cluster + byte offset — independent of both `deluge_sample_reader::reader::Geometry` and
/// `deluge_sample_fill::FillContext` (each side of this workspace's several forks re-mirrors only
/// the fields it needs; see either of those types' own doc for the convention).
#[derive(Clone, Copy, Debug)]
pub struct Geometry {
    pub audio_data_start_bytes: u32,
    pub audio_data_length_bytes: u64,
    pub cluster_size_bytes: u32,
    pub byte_depth: u8,
    pub num_channels: u8,
}

impl Geometry {
    #[must_use]
    pub fn frame_stride(&self) -> u64 {
        self.byte_depth as u64 * self.num_channels as u64
    }

    /// The count of frames whose ENTIRE `frame_stride`-byte span lies within
    /// `[0, audio_data_length_bytes)` — i.e. `frame < total_frames() <=> (frame+1)*frame_stride <=
    /// audio_data_length_bytes`. Every real geometry this crate's harness builds sets
    /// `audio_data_start_bytes == 0` (see `ChunkHarness`'s own doc for why), so this is the whole
    /// EOF/short-last-cluster boundary the differential needs: no partial-frame tail is ever
    /// counted valid, matching `deluge_sample_reader::reader::Reader::window`'s own "a straddling
    /// frame is real audio only if the next cluster genuinely has more data" contract exactly
    /// (their agreement — or disagreement — on WHERE the sample ends is exactly what this crate's
    /// per-case assertions are checking).
    #[must_use]
    pub fn total_frames(&self) -> u64 {
        self.audio_data_length_bytes / self.frame_stride()
    }
}

/// `(cluster_index, byte_offset_within_cluster)` for `frame` — the current read path's own
/// mapping, reproduced independently (see this module's doc).
#[must_use]
pub fn locate(frame: u64, geo: &Geometry) -> (u32, u32) {
    let abs_byte_pos = geo.audio_data_start_bytes as u64 + frame * geo.frame_stride();
    let cluster_size = geo.cluster_size_bytes as u64;
    (
        (abs_byte_pos / cluster_size) as u32,
        (abs_byte_pos % cluster_size) as u32,
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn locate_and_total_frames_match_hand_worked_geometry() {
        let geo = Geometry {
            audio_data_start_bytes: 0,
            audio_data_length_bytes: 1536, // 3 * 512
            cluster_size_bytes: 512,
            byte_depth: 3,
            num_channels: 1,
        };
        assert_eq!(geo.frame_stride(), 3);
        assert_eq!(geo.total_frames(), 512);
        assert_eq!(locate(0, &geo), (0, 0));
        assert_eq!(locate(170, &geo), (0, 510)); // last whole-in-cluster-0 frame start
        assert_eq!(locate(171, &geo), (1, 1)); // 171*3 = 513 -> cluster 1, offset 1
        assert_eq!(locate(511, &geo), (2, 509)); // last frame: 511*3=1533 -> cluster 2, offset 509
    }

    #[test]
    fn total_frames_excludes_a_partial_tail_frame() {
        // 3 full clusters (1536 bytes) + 100 bytes of a short last cluster = 1636 bytes; stride 3
        // does not divide 1636 evenly (1636 / 3 == 545, remainder 1) -- the trailing partial frame
        // must not count as valid.
        let geo = Geometry {
            audio_data_start_bytes: 0,
            audio_data_length_bytes: 1636,
            cluster_size_bytes: 512,
            byte_depth: 3,
            num_channels: 1,
        };
        // 545 * 3 == 1635 <= 1636 (the last valid frame's span fits); 546 * 3 == 1638 > 1636 (the
        // next frame's span does not) -- confirms `total_frames()` lands exactly on that boundary.
        assert_eq!(geo.total_frames(), 545);
    }
}
