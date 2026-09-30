//! The oracle's frame → (cluster, byte offset) mapping, written independently of
//! `deluge_sample_reader`'s own so a bug there cannot hide in both.
//!
//! Byte position is `audio_data_start + frame * frame_stride`; cluster and offset are its quotient
//! and remainder by the cluster size (a power of two in every real geometry).

/// The fields of `DelugeStreamingFillContext` the oracle needs to locate a frame.
#[derive(Clone, Copy, Debug)]
pub struct Geometry {
    pub audio_data_start_bytes: u32,
    pub audio_data_length_bytes: u64,
    pub cluster_size_bytes: u32,
    pub byte_depth: u8,
    pub num_channels: u8,
}

impl Geometry {
    /// Returns the bytes per frame, across all channels.
    #[must_use]
    pub fn frame_stride(&self) -> u64 {
        self.byte_depth as u64 * self.num_channels as u64
    }

    /// Returns the number of whole frames in `[0, audio_data_length_bytes)`; a partial trailing
    /// frame does not count.
    ///
    /// The harness always uses `audio_data_start_bytes == 0`, so this is where the sample ends, to
    /// be compared with where the reader says it ends.
    #[must_use]
    pub fn total_frames(&self) -> u64 {
        self.audio_data_length_bytes / self.frame_stride()
    }
}

/// Returns `(cluster_index, byte_offset_within_cluster)` for `frame`.
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
        // 3 full clusters + 100 bytes = 1636 bytes, which stride 3 leaves a 1-byte partial frame.
        let geo = Geometry {
            audio_data_start_bytes: 0,
            audio_data_length_bytes: 1636,
            cluster_size_bytes: 512,
            byte_depth: 3,
            num_channels: 1,
        };
        // 545 * 3 == 1635 fits; 546 * 3 == 1638 does not.
        assert_eq!(geo.total_frames(), 545);
    }
}
