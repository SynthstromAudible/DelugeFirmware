//! Safe Rust wrappers over the app's `convert.cpp` and `stitch.cpp`, for the sample fill path.
//!
//! - [`convert_cluster`] converts a cluster's raw bytes to native format in place ([`convert_word`]
//!   is the scalar single-word core).
//! - [`stitch_boundaries`] stitches a cluster's boundary bytes against its loaded neighbours.
//!
//! The build script compiles the C++ (see its docs for the host, device and `app_convert` modes).
//! The tests mirror `tests/unit/audio_stream/{convert,stitch,convert_cluster}_spec.cpp` against the
//! host objects, and check the build's armv7a-NEON verify stamp.

#![deny(unsafe_op_in_unsafe_fn)]
// `no_std` on bare metal only; the host tests use `std`.
#![cfg_attr(target_os = "none", no_std)]

/// On-disk raw sample data format. Mirrors the `uint8_t` enum `RawDataFormat` in
/// `storage/audio/audio_file_format.h`.
#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RawDataFormat {
    /// Already native; conversion is a no-op.
    Native = 0,
    /// 32-bit IEEE float in `[-1, 1)`, converted to Q31 (saturating at `|x| >= 1`).
    Float = 1,
    /// Unsigned 8-bit; each byte is XORed with `0x80` to make it signed.
    Unsigned8 = 2,
    /// 16-bit samples with byte-swapped endianness.
    EndiannessWrong16 = 3,
    /// 24-bit samples with byte-swapped endianness (group-wise byte0<->byte2 swap).
    EndiannessWrong24 = 4,
    /// 32-bit samples with byte-swapped endianness.
    EndiannessWrong32 = 5,
}

/// The owning sample's audio-data geometry that [`convert_cluster`] needs. Mirrors the C++
/// `ConvertGeometry`.
#[derive(Clone, Copy, Debug)]
pub struct ConvertGeometry {
    /// Byte offset of the sample's audio data within the file.
    pub audio_data_start_pos_bytes: u32,
    /// Length of the sample's audio data, in bytes.
    pub audio_data_length_bytes: u64,
    /// `sample->getFirstClusterIndexWithNoAudioData()`.
    pub first_cluster_index_with_no_audio_data: i32,
}

/// The boundary bytes at the tail of the cluster before the one being stitched. Mirrors the C++
/// `StitchPrevEdge`.
pub struct PrevEdge<'a> {
    /// 11 bytes: `prev.payload()[cluster_size-4 .. cluster_size+7)`.
    pub tail: &'a mut [u8],
    /// In/out: `prev.extra_bytes_at_end_converted`.
    pub end_boundary_converted: &'a mut bool,
}

/// The boundary bytes at the head of the cluster after the one being stitched, plus its
/// pre-conversion bytes. Mirrors the C++ `StitchNextEdge`.
pub struct NextEdge<'a> {
    /// 7 bytes: `next.payload()[0..7)`.
    pub head: &'a mut [u8],
    /// `next.first_three_bytes_pre_data_conversion`.
    pub unconverted_head: &'a [u8; 3],
    /// In/out: `next.extra_bytes_at_start_converted`.
    pub start_boundary_converted: &'a mut bool,
}

mod ffi {
    extern "C" {
        pub fn deluge_sc_convert_word(word: i32, format: u8) -> i32;

        pub fn deluge_sc_convert_cluster_data(
            data: *mut u8,
            data_len: usize,
            cluster_index: i32,
            format: u8,
            audio_data_start_pos_bytes: u32,
            audio_data_length_bytes: u64,
            first_cluster_index_with_no_audio_data: i32,
            cluster_size: usize,
            cluster_size_magnitude: usize,
            unconverted_head_out: *mut u8, // 3 bytes
            yield_counter: *mut i32,       // nullable
        );

        #[allow(clippy::too_many_arguments)]
        pub fn deluge_sc_stitch_boundaries(
            self_data: *mut u8,
            self_len: usize,
            cluster_index: i32,
            format: u8,
            audio_data_start_pos_bytes: u32,
            cluster_size: usize,
            self_start: *mut bool,
            self_end: *mut bool,
            prev_tail: *mut u8, // null => no prev
            prev_tail_len: usize,
            prev_end_converted: *mut bool,
            next_head: *mut u8, // null => no next
            next_head_len: usize,
            next_unconverted_head: *const u8, // 3 bytes
            next_start_converted: *mut bool,
        );
    }
}

/// Converts one 4-byte `word` from `format` to native.
///
/// `Native` and `EndiannessWrong24` return the word unchanged; the 24-bit swap is group-wise and
/// done by [`convert_cluster`].
#[must_use]
pub fn convert_word(word: i32, format: RawDataFormat) -> i32 {
    // Pure; no pointers cross the boundary.
    unsafe { ffi::deluge_sc_convert_word(word, format as u8) }
}

/// Converts cluster `data` in place from `format` to native, over only its audio-bearing portion
/// per `geometry`.
///
/// For a non-`Native` format the first 3 bytes are first backed up into `unconverted_head_out`.
/// The C++ cooperative yield is a no-op here. See `convert_cluster_data` in `convert.h`.
///
/// # Panics
///
/// Panics if `data.len() < 3`.
pub fn convert_cluster(
    data: &mut [u8],
    cluster_index: i32,
    format: RawDataFormat,
    geometry: ConvertGeometry,
    cluster_size: usize,
    cluster_size_magnitude: usize,
    unconverted_head_out: &mut [u8; 3],
) {
    assert!(
        data.len() >= 3,
        "convert_cluster: data must have at least 3 bytes"
    );
    let len = data.len();
    // SAFETY: `data` is a uniquely borrowed slice of `len` bytes and `unconverted_head_out` a
    // 3-byte buffer; the C++ writes only within them. A null yield counter selects the no-op yield.
    unsafe {
        ffi::deluge_sc_convert_cluster_data(
            data.as_mut_ptr(),
            len,
            cluster_index,
            format as u8,
            geometry.audio_data_start_pos_bytes,
            geometry.audio_data_length_bytes,
            geometry.first_cluster_index_with_no_audio_data,
            cluster_size,
            cluster_size_magnitude,
            unconverted_head_out.as_mut_ptr(),
            core::ptr::null_mut(),
        );
    }
}

/// Stitches one cluster's boundary bytes against its loaded neighbours, in place.
///
/// Mutates `self_data`, the neighbour edges, and the boundary flags. A `None` neighbour is absent
/// or not loaded. See `stitch_boundaries` in `stitch.h`.
///
/// # Panics
///
/// Panics if `self_data` is shorter than `cluster_size + 7` bytes, the span the C++ writes.
#[allow(clippy::too_many_arguments)]
pub fn stitch_boundaries(
    self_data: &mut [u8],
    cluster_index: i32,
    format: RawDataFormat,
    audio_data_start_pos_bytes: u32,
    cluster_size: usize,
    self_start_boundary_converted: &mut bool,
    self_end_boundary_converted: &mut bool,
    prev: Option<PrevEdge<'_>>,
    next: Option<NextEdge<'_>>,
) {
    let self_len = self_data.len();
    assert!(
        self_len >= cluster_size + 7,
        "stitch_boundaries: {self_len}-byte cluster buffer, needs cluster_size + 7 = {}",
        cluster_size + 7
    );

    // Decompose the edges into raw parts; keep the borrows alive for the whole call.
    let (prev_ptr, prev_len, prev_flag) = match prev {
        Some(e) => (
            e.tail.as_mut_ptr(),
            e.tail.len(),
            e.end_boundary_converted as *mut bool,
        ),
        None => (core::ptr::null_mut(), 0, core::ptr::null_mut()),
    };
    let (next_ptr, next_len, next_unconv, next_flag) = match next {
        Some(e) => (
            e.head.as_mut_ptr(),
            e.head.len(),
            e.unconverted_head.as_ptr(),
            e.start_boundary_converted as *mut bool,
        ),
        None => (
            core::ptr::null_mut(),
            0,
            core::ptr::null(),
            core::ptr::null_mut(),
        ),
    };

    // SAFETY: every pointer comes from a live, uniquely borrowed slice or reference of the stated
    // length, or is null for an absent neighbour; the C++ writes only within those spans and flags.
    unsafe {
        ffi::deluge_sc_stitch_boundaries(
            self_data.as_mut_ptr(),
            self_len,
            cluster_index,
            format as u8,
            audio_data_start_pos_bytes,
            cluster_size,
            self_start_boundary_converted as *mut bool,
            self_end_boundary_converted as *mut bool,
            prev_ptr,
            prev_len,
            prev_flag,
            next_ptr,
            next_len,
            next_unconv,
            next_flag,
        );
    }
}

#[cfg(test)]
mod tests {
    use super::RawDataFormat::*;
    use super::*;

    #[allow(clippy::too_many_arguments)]
    fn conv_cluster(
        data: &mut [u8],
        cluster_index: i32,
        format: RawDataFormat,
        start_pos: u32,
        len_bytes: u64,
        first_no_audio: i32,
        cluster_size: usize,
        magnitude: usize,
    ) -> [u8; 3] {
        let mut head = [0u8; 3];
        convert_cluster(
            data,
            cluster_index,
            format,
            ConvertGeometry {
                audio_data_start_pos_bytes: start_pos,
                audio_data_length_bytes: len_bytes,
                first_cluster_index_with_no_audio_data: first_no_audio,
            },
            cluster_size,
            magnitude,
            &mut head,
        );
        head
    }

    // convert_word: mirrors convert_spec.cpp
    #[test]
    fn convert_word_matches_spec() {
        assert_eq!(
            convert_word(0x1122_3344u32 as i32, Native),
            0x1122_3344u32 as i32
        );
        assert_eq!(
            convert_word(0x1122_3344u32 as i32, EndiannessWrong24),
            0x1122_3344u32 as i32
        );
        assert_eq!(convert_word(0x0102_0304, EndiannessWrong32), 0x0403_0201);
        assert_eq!(convert_word(0x0102_0304, EndiannessWrong16), 0x0201_0403);
        assert_eq!(convert_word(0x0011_2233, Unsigned8), 0x8091_A2B3u32 as i32);
        // FLOAT 0.5 -> Q31 0x40000000.
        assert_eq!(convert_word(0.5f32.to_bits() as i32, Float), 0x4000_0000);
    }

    // FLOAT saturation / NaN: q31_from_float saturates at |x| >= 1 and flushes tiny/NaN
    #[test]
    fn convert_word_float_saturation_and_nan() {
        let f = |x: f32| convert_word(x.to_bits() as i32, Float);
        // |x| >= 1 saturates to INT32_MAX / INT32_MIN.
        assert_eq!(f(1.0), i32::MAX);
        assert_eq!(f(2.0), i32::MAX);
        assert_eq!(f(-1.0), i32::MIN);
        assert_eq!(f(-2.0), i32::MIN);
        // In-range values round toward zero (matches VCVT).
        assert_eq!(f(0.5), 0x4000_0000);
        assert_eq!(f(-0.5), -0x4000_0000);
        // NaN has exponent 0xFF (>= 0) => saturates by sign (positive NaN -> INT32_MAX).
        assert_eq!(f(f32::NAN), i32::MAX);
        // +/-inf likewise saturate.
        assert_eq!(f(f32::INFINITY), i32::MAX);
        assert_eq!(f(f32::NEG_INFINITY), i32::MIN);
        // Zero and |x| < 2^-31 flush to 0.
        assert_eq!(f(0.0), 0);
        assert_eq!(f(1.0e-12), 0);
    }

    // convert_cluster UNSIGNED_8 across a 16-byte SIMD boundary: mirrors convert_cluster_spec
    #[test]
    fn convert_cluster_unsigned8_simd_region() {
        let cluster_size = 128;
        let mut data = vec![0u8; cluster_size];
        // Region [4, 80): 76 bytes -> every byte XORed with 0x80.
        conv_cluster(&mut data, 0, Unsigned8, 4, 76, 1, cluster_size, 7);
        assert_eq!(data[0], 0x00);
        assert_eq!(data[3], 0x00);
        for b in &data[4..80] {
            assert_eq!(*b, 0x80);
        }
        assert_eq!(data[80], 0x00);
        assert_eq!(data[127], 0x00);
    }

    // convert_cluster ENDIANNESS_WRONG_32 across a SIMD boundary + scalar tail
    #[test]
    fn convert_cluster_wrong32_simd_region() {
        let cluster_size = 128;
        let mut data: Vec<u8> = (0..cluster_size as u16).map(|i| i as u8).collect();
        // Region [4, 72): every word has all 4 bytes reversed.
        conv_cluster(&mut data, 0, EndiannessWrong32, 4, 68, 1, cluster_size, 7);
        assert_eq!(data[0], 0);
        assert_eq!(data[3], 3);
        let mut w = 4usize;
        while w < 72 {
            for k in 0..4 {
                assert_eq!(data[w + k], (w + 3 - k) as u8, "word at {w}, k={k}");
            }
            w += 4;
        }
        assert_eq!(data[72], 72);
        assert_eq!(data[127], 127);
    }

    // convert_cluster ENDIANNESS_WRONG_24 backup + swap: mirrors convert_cluster_spec
    #[test]
    fn convert_cluster_wrong24_swaps_and_backs_up() {
        let cluster_size = 32;
        let mut data: Vec<u8> = (0..cluster_size as u16).map(|i| i as u8).collect();
        let backup = conv_cluster(&mut data, 0, EndiannessWrong24, 2, 1000, 5, cluster_size, 5);
        assert_eq!(backup, [0, 1, 2]);
        assert_eq!(&data[0..2], &[0, 1]); // before audio start untouched
        assert_eq!(&data[2..5], &[4, 3, 2]); // first group [2,3,4] -> [4,3,2]
        assert_eq!(&data[29..32], &[31, 30, 29]); // last group [29,30,31] -> [31,30,29]
    }

    // The yield cadence still fires, observed through the shim's counting-yield path.
    #[test]
    fn convert_cluster_invokes_yield() {
        // convert_cluster()'s public API uses a no-op Yield; drop to the shim to observe the
        // cadence.
        let cluster_size = 4096;
        let mut data = vec![0u8; cluster_size];
        let mut head = [0u8; 3];
        let mut yc = 0i32;
        let dlen = data.len();
        unsafe {
            ffi::deluge_sc_convert_cluster_data(
                data.as_mut_ptr(),
                dlen,
                0,
                EndiannessWrong24 as u8,
                3,
                100_000,
                5,
                cluster_size,
                12,
                head.as_mut_ptr(),
                &mut yc,
            );
        }
        assert!(yc >= 1, "expected at least one yield, got {yc}");
    }

    // stitch_boundaries no neighbours: self_data unchanged, flags untouched
    #[test]
    fn stitch_no_neighbors_unchanged() {
        let cluster_size = 32;
        let mut self_data: Vec<u8> = (0..(cluster_size + 7) as u16).map(|i| i as u8).collect();
        let original = self_data.clone();
        let (mut ss, mut se) = (false, false);
        stitch_boundaries(
            &mut self_data,
            1,
            Unsigned8,
            1,
            cluster_size,
            &mut ss,
            &mut se,
            None,
            None,
        );
        assert_eq!(self_data, original);
        assert!(!ss);
        assert!(!se);
    }

    // stitch_boundaries UNSIGNED_8 misaligned, both neighbours: mirrors stitch_spec
    #[test]
    fn stitch_unsigned8_misaligned_both_neighbors() {
        let cluster_size = 32;
        let mut self_data: Vec<u8> = (0..(cluster_size + 7) as u16).map(|i| i as u8).collect();
        let mut prev_tail: Vec<u8> = (0..11u16).map(|i| (100 + i) as u8).collect();
        let mut next_head: Vec<u8> = (0..7u16).map(|i| (200 + i) as u8).collect();
        let next_unconverted_head: [u8; 3] = [50, 51, 52];
        let (mut ss, mut se) = (false, false);
        let mut prev_end = false;
        let mut next_start = false;
        stitch_boundaries(
            &mut self_data,
            1,
            Unsigned8,
            1,
            cluster_size,
            &mut ss,
            &mut se,
            Some(PrevEdge {
                tail: &mut prev_tail,
                end_boundary_converted: &mut prev_end,
            }),
            Some(NextEdge {
                head: &mut next_head,
                unconverted_head: &next_unconverted_head,
                start_boundary_converted: &mut next_start,
            }),
        );
        // Prev half.
        assert_eq!(prev_tail[0], 100);
        assert_eq!(&prev_tail[1..5], &[229, 230, 231, 128]);
        assert_eq!(&self_data[0..3], &[128, 1, 2]);
        assert!(prev_end);
        // Next half.
        assert_eq!(&self_data[29..33], &[157, 158, 159, 72]);
        assert_eq!(next_head[0], 72);
        assert_eq!(&next_head[1..7], &[201, 202, 203, 204, 205, 206]);
        assert!(next_start);
        assert!(ss);
        assert!(se);
    }

    // stitch_boundaries already-converted idempotency: the next half stages from unconverted_head
    // (not head), then falls through to the full overhang copy from head. Mirrors stitch_spec.
    #[test]
    fn stitch_next_already_converted_stages_from_unconverted_head() {
        let cluster_size = 32;
        let mut self_data: Vec<u8> = (0..(cluster_size + 7) as u16).map(|i| i as u8).collect();
        let mut next_head: Vec<u8> = (0..7u16).map(|i| (200 + i) as u8).collect();
        let next_unconverted_head: [u8; 3] = [90, 91, 92];
        let (mut ss, mut se) = (false, false);
        // Already converted: forces the unconverted_head staging branch.
        let mut next_start = true;
        stitch_boundaries(
            &mut self_data,
            1,
            EndiannessWrong16,
            1,
            cluster_size,
            &mut ss,
            &mut se,
            None,
            Some(NextEdge {
                head: &mut next_head,
                unconverted_head: &next_unconverted_head,
                start_boundary_converted: &mut next_start,
            }),
        );
        // Pre-conversion straddling word self_data[29..33) = {29,30,31,unconverted_head[0]=90};
        // rev16 -> {30,29,90,31}. self_data[31] == 90 proves the source was unconverted_head, not
        // head.
        assert_eq!(self_data[29], 30);
        assert_eq!(self_data[30], 29);
        assert_eq!(self_data[31], 90);
        assert!(next_start); // untouched (was already true)
                             // Overhang finalized wholesale from next.head (need_copy7); next.head itself is never
                             // written.
        for k in 0..7usize {
            assert_eq!(self_data[cluster_size + k], (200 + k) as u8);
            assert_eq!(next_head[k], (200 + k) as u8);
        }
        assert!(!ss); // prev absent
        assert!(se);
    }

    // armv7a-NEON compile + real-NEON verify (stamped by build.rs; no QEMU run)
    #[test]
    fn armv7a_neon_compiles_with_real_neon() {
        let stamp = include_str!(concat!(env!("OUT_DIR"), "/arm_compile_result.txt"));
        assert!(
            stamp.starts_with("OK"),
            "armv7a-NEON cc compile/verify failed:\n{stamp}"
        );
        assert!(
            stamp.contains("vcvt.s32.f32"),
            "arm object missing real NEON codegen:\n{stamp}"
        );
    }
}
