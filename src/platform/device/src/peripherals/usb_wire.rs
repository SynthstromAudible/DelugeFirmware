//! USB audio in its wire format: frames packed into the words the DMA moves, the sizes of the packets they leave in, and
//! the host's frames read back out of a DMA ring.
//!
//! A stream's frames are whole 32-bit words: one word of two 16-bit samples, or two words of 24-bit samples in the top of
//! each. Nothing here touches hardware, so the host tests cover it.

use super::usb_audio_stream::{Frame, FrameSource};

/// A stream's sample depth, and how its frames lie on the wire.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Depth {
    /// One word a frame: the left 16 bits low, the right high.
    Sixteen,
    /// Two words a frame, left then right, each sample in the top 24 bits.
    TwentyFour,
}

impl Depth {
    /// The depth of a stream of `bits`-bit samples.
    pub fn from_bits(bits: u8) -> Option<Self> {
        match bits {
            16 => Some(Self::Sixteen),
            24 => Some(Self::TwentyFour),
            _ => None,
        }
    }

    /// Words a stereo frame takes.
    pub const fn words(self) -> usize {
        match self {
            Self::Sixteen => 1,
            Self::TwentyFour => 2,
        }
    }

    /// Packs `frames` into `out`, whose length is theirs times [`words`](Self::words). Samples are cut to the depth.
    pub fn pack(self, frames: &[Frame], out: &mut [u32]) {
        match self {
            Self::Sixteen => {
                for (f, o) in frames.iter().zip(out) {
                    *o = (f[0] as u32 >> 16) | (f[1] as u32 & 0xFFFF_0000);
                }
            }
            Self::TwentyFour => {
                for (f, o) in frames.iter().zip(out.chunks_exact_mut(2)) {
                    o[0] = f[0] as u32 & 0xFFFF_FF00;
                    o[1] = f[1] as u32 & 0xFFFF_FF00;
                }
            }
        }
    }

    /// The frame in `words`, full scale, the depth's bits on top.
    pub fn unpack(self, words: &[u32]) -> Frame {
        match self {
            Self::Sixteen => [(words[0] << 16) as i32, (words[0] & 0xFFFF_0000) as i32],
            Self::TwentyFour => [
                (words[0] & 0xFFFF_FF00) as i32,
                (words[1] & 0xFFFF_FF00) as i32,
            ],
        }
    }
}

/// The most frames a packet carries.
pub const PACKET_FRAMES: usize = 6;

/// The ring the packets to the host are read from: the render writes frames, and a packet is a run of them, contiguous
/// in memory even where the ring ends. `frames` is a power of two.
pub struct ToHostRing {
    mem: *mut u32,
    frames: usize,
    depth: Depth,
}

impl ToHostRing {
    /// Frames past the end of the ring that repeat its start, so that any packet is one run.
    const MIRROR: usize = PACKET_FRAMES;

    /// The words a ring of `frames` frames takes.
    pub const fn words(frames: usize, depth: Depth) -> usize {
        (frames + Self::MIRROR) * depth.words()
    }

    /// A ring over `mem`.
    ///
    /// # Safety
    /// `mem` is [`words`](Self::words) words, for as long as any packet of the ring is in use; nothing else writes them.
    pub unsafe fn new(mem: *mut u32, frames: usize, depth: Depth) -> Self {
        debug_assert!(frames.is_power_of_two());
        Self { mem, frames, depth }
    }

    /// Packs `frames` at position `pos`, which is a free-running frame count.
    pub fn write(&self, pos: u32, frames: &[Frame]) {
        let w = self.depth.words();
        for (i, f) in frames.iter().enumerate() {
            let at = (pos as usize).wrapping_add(i) & (self.frames - 1);
            let mut packed = [0u32; 2];
            self.depth.pack(core::slice::from_ref(f), &mut packed[..w]);
            for (k, word) in packed[..w].iter().enumerate() {
                // SAFETY: `at` is in the ring, or in its mirror at `frames + at`; the memory is ours.
                unsafe {
                    self.mem.add(at * w + k).write_volatile(*word);
                    if at < Self::MIRROR {
                        self.mem
                            .add((self.frames + at) * w + k)
                            .write_volatile(*word);
                    }
                }
            }
        }
    }

    /// The packet of `n` frames (at most [`PACKET_FRAMES`]) at position `pos`, which was written and is not yet again.
    pub fn packet(&self, pos: u32, n: usize) -> &'static [u32] {
        debug_assert!(n <= PACKET_FRAMES);
        let w = self.depth.words();
        // SAFETY: `n` frames from a frame of the ring fit the ring and its mirror; `new` made the memory last.
        unsafe {
            core::slice::from_raw_parts(self.mem.add((pos as usize & (self.frames - 1)) * w), n * w)
        }
    }
}

/// How many packets to keep queued ahead of the DMA: two 128-frame blocks of the average 5.5125 frames.
pub const PACKET_TARGET_AHEAD: usize = 46;
/// The share of 6-frame packets at 44.1 kHz and 8000 packets a second: 41 in 80.
const SHARE: i32 = 41;
const SHARE_OF: i32 = 80;
/// How far an error in the queue, in packets, moves the share: one 80th a packet, at most this much.
const MAX_STEER: i32 = 20;

/// The sizes of the packets to the host, 8000 a second, for a stream whose rate is the codec's.
///
/// Every frame the render writes is sent, so the frames follow the codec by themselves. A packet's size (5 or 6
/// frames) only decides how many packets wait ahead of the DMA, which the host drains at 8000 a second: a longer queue
/// means bigger packets until it is back at its target.
pub struct PacketPacer {
    acc: i32,
}

impl PacketPacer {
    pub const fn new() -> Self {
        Self { acc: 0 }
    }

    /// The next packet's frames, given the packets already queued ahead of the DMA.
    pub fn next_size(&mut self, ahead: usize) -> usize {
        let error = ahead as i32 - PACKET_TARGET_AHEAD as i32;
        self.acc += SHARE + error.clamp(-MAX_STEER, MAX_STEER);
        if self.acc >= SHARE_OF {
            self.acc -= SHARE_OF;
            PACKET_FRAMES
        } else {
            PACKET_FRAMES - 1
        }
    }
}

impl Default for PacketPacer {
    fn default() -> Self {
        Self::new()
    }
}

/// The host's frames in a ring a DMA channel fills: a snapshot of how far it has got.
pub struct WireSource {
    mem: *const u32,
    words: usize,
    depth: Depth,
    written: u32,
}

impl WireSource {
    /// `words` words at `mem`, of which the DMA has received `written` frames in all.
    ///
    /// # Safety
    /// `mem` is readable for as long as the source is; `words` is whole frames.
    pub unsafe fn new(mem: *const u32, words: usize, depth: Depth, written: u32) -> Self {
        Self {
            mem,
            words,
            depth,
            written,
        }
    }
}

impl FrameSource for WireSource {
    fn written(&self) -> u32 {
        self.written
    }

    fn capacity(&self) -> u32 {
        (self.words / self.depth.words()) as u32
    }

    unsafe fn peek(&self, pos: u32) -> Frame {
        let w = self.depth.words();
        let at = (pos as usize).wrapping_mul(w) % self.words;
        let mut packed = [0u32; 2];
        for (k, word) in packed[..w].iter_mut().enumerate() {
            // SAFETY: the caller's contract; `at` is a whole frame's first word, so `at + k` is in the ring.
            *word = unsafe { self.mem.add(at + k).read_volatile() };
        }
        self.depth.unpack(&packed[..w])
    }
}

#[cfg(test)]
mod tests {
    extern crate std;
    use super::*;
    use std::vec;
    use std::vec::Vec;

    /// A full-scale sample as little-endian PCM of `out.len()` bytes (2 or 3), as a host reads it: its top bits, truncated.
    fn encode_sample(sample: i32, out: &mut [u8]) {
        let [_, b1, b2, b3] = sample.to_le_bytes();
        match out {
            [o0, o1] => (*o0, *o1) = (b2, b3),
            [o0, o1, o2] => (*o0, *o1, *o2) = (b1, b2, b3),
            _ => unreachable!(),
        }
    }

    /// Frames with no pattern, and the extremes.
    fn frames(n: usize) -> Vec<Frame> {
        let mut x = 0x2545_F491_4F6C_DD1Du64;
        let mut next = move || {
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            x as i32
        };
        let mut f: Vec<Frame> = vec![
            [0, 0],
            [i32::MIN, i32::MAX],
            [i32::MAX, i32::MIN],
            [-1, 1],
            [0x100, -0x100],
        ];
        while f.len() < n {
            f.push([next(), next()]);
        }
        f.truncate(n);
        f
    }

    /// The bytes `encode_sample` writes for `frames` at `bytes` a sample, as the host sees them on the wire.
    fn encoded(frames: &[Frame], bytes: usize) -> Vec<u8> {
        let mut out = Vec::new();
        for f in frames {
            for s in f {
                let mut b = vec![0; bytes];
                encode_sample(*s, &mut b);
                out.extend(b);
            }
        }
        out
    }

    fn bytes(words: &[u32]) -> Vec<u8> {
        words.iter().flat_map(|w| w.to_le_bytes()).collect()
    }

    #[test]
    fn sixteen_bit_frames_pack_into_one_word_each() {
        let f = frames(200);
        let mut words = vec![0; f.len()];
        Depth::Sixteen.pack(&f, &mut words);
        assert_eq!(bytes(&words), encoded(&f, 2));
    }

    /// A 24-bit sample in a 4-byte slot is the packed sample's three bytes on top of a zero byte.
    #[test]
    fn twenty_four_bit_frames_pack_into_two_words_each() {
        let f = frames(200);
        let mut words = vec![0; 2 * f.len()];
        Depth::TwentyFour.pack(&f, &mut words);
        let got = bytes(&words);
        for (i, f) in f.iter().enumerate() {
            for (ch, s) in f.iter().enumerate() {
                let slot = &got[(2 * i + ch) * 4..][..4];
                let mut packed = [0; 3];
                encode_sample(*s, &mut packed);
                assert_eq!(slot, [0, packed[0], packed[1], packed[2]]);
            }
        }
    }

    #[test]
    fn a_depth_has_whole_words_a_frame() {
        assert_eq!(Depth::Sixteen.words(), 1);
        assert_eq!(Depth::TwentyFour.words(), 2);
        assert_eq!(Depth::from_bits(16), Some(Depth::Sixteen));
        assert_eq!(Depth::from_bits(24), Some(Depth::TwentyFour));
        assert_eq!(Depth::from_bits(20), None);
    }

    #[test]
    fn reading_a_frame_back_inverts_packing_to_the_depth() {
        for (depth, keep) in [(Depth::Sixteen, !0xFFFF), (Depth::TwentyFour, !0xFFi32)] {
            let f = frames(64);
            let mut words = vec![0; depth.words() * f.len()];
            depth.pack(&f, &mut words);
            for (i, f) in f.iter().enumerate() {
                let w = &words[i * depth.words()..][..depth.words()];
                assert_eq!(depth.unpack(w), [f[0] & keep, f[1] & keep]);
            }
        }
    }

    // ── The ring of packets to the host ─────────────────────────────────────

    fn ring(frames: usize, depth: Depth) -> (Vec<u32>, usize) {
        (vec![0u32; ToHostRing::words(frames, depth)], frames)
    }

    /// Every packet is one run of words holding its frames, wherever the ring's end falls.
    #[test]
    fn a_packet_across_the_end_of_the_ring_is_contiguous() {
        for depth in [Depth::Sixteen, Depth::TwentyFour] {
            let (mut mem, n) = ring(32, depth);
            let ring = unsafe { ToHostRing::new(mem.as_mut_ptr(), n, depth) };
            let w = depth.words();
            let mut pos = 0u32;
            for round in 0..20 {
                let block = frames(40)
                    .into_iter()
                    .map(|f| [f[0] ^ (round << 24), f[1]])
                    .collect::<Vec<Frame>>();
                // Frames 24 at a time, so the ring's end falls in the middle of packets of every size.
                ring.write(pos, &block[..24]);
                for at in 0..24 - 6 {
                    let packet = ring.packet(pos + at, 6);
                    let mut want = vec![0; 6 * w];
                    depth.pack(&block[at as usize..][..6], &mut want);
                    assert_eq!(packet, want, "round {round} frame {at}");
                }
                pos += 24;
            }
        }
    }

    // ── Packet sizes ────────────────────────────────────────────────────────

    /// 600 s of a 128-frame render against a host that takes 8000 packets a second, the codec `ppm` off it: the smallest
    /// and largest queue at the start of any render (after the first second), and the most frames that wait to be
    /// packed at once.
    fn run(ppm: f64) -> (usize, usize, usize) {
        let mut pacer = PacketPacer::new();
        let block_s = 128.0 / (44_100.0 * (1.0 + ppm / 1e6));
        let (mut pushed, mut waiting) = (0i64, 0usize);
        let (mut lowest, mut highest, mut most_waiting) = (usize::MAX, 0, 0);
        let mut started = None;
        for block in 0..(600.0 / block_s) as u64 {
            let t = block as f64 * block_s;
            waiting += 128;
            most_waiting = most_waiting.max(waiting);
            if block == 1 {
                started = Some(t);
            }
            let consumed = started.map_or(0, |s| ((t - s) * 8000.0) as i64);
            if block > 400 {
                let ahead = (pushed - consumed).max(0) as usize;
                lowest = lowest.min(ahead);
                highest = highest.max(ahead);
            }
            while waiting >= PACKET_FRAMES {
                let ahead = (pushed - consumed).max(0) as usize;
                let n = pacer.next_size(ahead);
                assert!(n == 5 || n == PACKET_FRAMES);
                waiting -= n;
                pushed += 1;
            }
        }
        (lowest, highest, most_waiting)
    }

    /// Between one and three 128-frame blocks of packets ahead, whatever the drift, and no frame waits for long.
    fn within_bounds(ppm: f64) {
        let (lowest, highest, waiting) = run(ppm);
        assert!(lowest >= 23, "{ppm} ppm: {lowest} packets ahead");
        assert!(highest <= 70, "{ppm} ppm: {highest} packets ahead");
        assert!(
            waiting < 128 + PACKET_FRAMES,
            "{ppm} ppm: {waiting} frames waiting"
        );
    }

    #[test]
    fn the_queue_holds_with_no_drift() {
        within_bounds(0.0);
    }

    #[test]
    fn the_queue_holds_with_the_codec_fast() {
        within_bounds(200.0);
    }

    #[test]
    fn the_queue_holds_with_the_codec_slow() {
        within_bounds(-200.0);
    }

    #[test]
    fn a_new_pacer_starts_with_packets_of_the_average_mix() {
        let mut pacer = PacketPacer::new();
        let total: usize = (0..80).map(|_| pacer.next_size(PACKET_TARGET_AHEAD)).sum();
        assert_eq!(total, 441);
    }

    // ── The host's frames in a DMA ring ─────────────────────────────────────

    /// A DMA ring the test writes as the DMAC would, and where it has got to.
    struct Wire {
        mem: Vec<u32>,
        depth: Depth,
        written: u32,
    }

    impl Wire {
        fn new(frames: usize, depth: Depth) -> Self {
            Self {
                mem: vec![0; frames * depth.words()],
                depth,
                written: 0,
            }
        }

        /// The host sends `frames`.
        fn receive(&mut self, frames: &[Frame]) {
            let words = self.depth.words();
            let mut packed = vec![0; words * frames.len()];
            self.depth.pack(frames, &mut packed);
            let len = self.mem.len();
            for (i, w) in packed.iter().enumerate() {
                self.mem[(self.written as usize * words + i) % len] = *w;
            }
            self.written += frames.len() as u32;
        }

        fn source(&self) -> WireSource {
            unsafe { WireSource::new(self.mem.as_ptr(), self.mem.len(), self.depth, self.written) }
        }
    }

    fn masked(f: Frame, depth: Depth) -> Frame {
        let keep = if depth == Depth::Sixteen {
            !0xFFFF
        } else {
            !0xFF
        };
        [f[0] & keep, f[1] & keep]
    }

    #[test]
    fn the_source_reads_what_the_host_sent_across_the_end_of_the_ring() {
        for depth in [Depth::Sixteen, Depth::TwentyFour] {
            let mut wire = Wire::new(64, depth);
            let mut at = 0;
            for round in 0..20u32 {
                let sent = frames(40)
                    .into_iter()
                    .map(|f| [f[0] ^ (round as i32) << 20, f[1]])
                    .collect::<Vec<_>>();
                wire.receive(&sent);
                let source = wire.source();
                assert_eq!(source.written(), at + 40);
                for (i, f) in sent.iter().enumerate() {
                    assert_eq!(unsafe { source.peek(at + i as u32) }, masked(*f, depth));
                }
                at += 40;
            }
        }
    }

    #[test]
    fn the_source_holds_a_ring_of_frames() {
        assert_eq!(Wire::new(64, Depth::Sixteen).source().capacity(), 64);
        assert_eq!(Wire::new(64, Depth::TwentyFour).source().capacity(), 64);
    }

    // ── The from-host reader on a DMA ring ──────────────────────────────────
    use super::super::usb_audio_stream::{FROM_HOST_TARGET_FILL, FromHostReader, SILENCE};

    /// A host that sends 128 frames a block for `blocks` blocks, and the reader's blocks after them.
    fn play(
        wire: &mut Wire,
        reader: &mut FromHostReader,
        generation: u32,
        blocks: u32,
        from: i32,
    ) -> Vec<Vec<Frame>> {
        let mut out = Vec::new();
        for b in 0..blocks as i32 {
            let sent: Vec<Frame> = (0..128)
                .map(|i| {
                    let v = (from + b * 128 + i) << 16;
                    [v, !v & !0xFFFF]
                })
                .collect();
            wire.receive(&sent);
            let mut block = vec![[1, 1]; 128];
            reader.read(&wire.source(), generation, 0, &mut block);
            out.push(block);
        }
        out
    }

    #[test]
    fn the_reader_copies_a_host_on_our_clock_straight_through() {
        for depth in [Depth::Sixteen, Depth::TwentyFour] {
            // The ring's end falls inside blocks, over and over.
            let mut wire = Wire::new(1024, depth);
            let mut reader = FromHostReader::new();
            let blocks = play(&mut wire, &mut reader, 1, 40, 0);
            // Silence until the window fills, then every frame once, in order.
            let played: Vec<Frame> = blocks.concat();
            let first = played.iter().position(|f| *f != SILENCE).unwrap();
            assert!(
                first % 128 == 0 && first <= 4 * 128,
                "{depth:?}: started at {first}"
            );
            let audio = &played[first..];
            for pair in audio.windows(2) {
                let (a, b) = (pair[0][0] >> 16, pair[1][0] >> 16);
                assert_eq!(b, a + 1, "{depth:?}");
            }
            // A stream starts with what arrives after the reader first looks, which was one block in.
            assert_eq!(audio[0][0] >> 16, 128);
        }
    }

    #[test]
    fn the_reader_is_silent_when_the_host_stops_and_resumes_with_the_next_frames() {
        let mut wire = Wire::new(1024, Depth::Sixteen);
        let mut reader = FromHostReader::new();
        play(&mut wire, &mut reader, 1, 20, 0);
        let mut dry = vec![[1, 1]; 128];
        for _ in 0..6 {
            reader.read(&wire.source(), 1, 0, &mut dry);
        }
        assert!(dry.iter().all(|f| *f == SILENCE), "ran dry");
        let blocks = play(&mut wire, &mut reader, 1, 20, 5000);
        let audio: Vec<Frame> = blocks
            .concat()
            .into_iter()
            .filter(|f| *f != SILENCE)
            .collect();
        assert!(audio.len() > 1000);
        for pair in audio.windows(2) {
            assert_eq!(pair[1][0] >> 16, (pair[0][0] >> 16) + 1);
        }
    }

    /// A reader so late that the ring was filled more than once over starts from the newest, not from what the DMA has
    /// since written.
    #[test]
    fn a_lapped_reader_starts_over() {
        let mut wire = Wire::new(1024, Depth::Sixteen);
        let mut reader = FromHostReader::new();
        play(&mut wire, &mut reader, 1, 20, 0);
        for b in 0..20 {
            let sent: Vec<Frame> = (0..128).map(|i| [(90_000 + b * 128 + i) << 8, 0]).collect();
            wire.receive(&sent);
        }
        let mut block = vec![[1, 1]; 128];
        reader.read(&wire.source(), 1, 0, &mut block);
        let newest = (90_000 + 20 * 128 - 1) << 8;
        let (oldest_kept, masked_newest) = ((90_000 + 20 * 128 - 1024) << 8, newest & !0xFFFF);
        for f in block.iter().filter(|f| **f != SILENCE) {
            assert!(
                f[0] >= oldest_kept & !0xFFFF && f[0] <= masked_newest,
                "{:#x}",
                f[0]
            );
        }
    }

    #[test]
    fn a_new_stream_starts_at_the_target_latency() {
        let mut wire = Wire::new(4096, Depth::Sixteen);
        let mut reader = FromHostReader::new();
        play(&mut wire, &mut reader, 1, 20, 0);
        let blocks = play(&mut wire, &mut reader, 2, 20, 20_000);
        let audio: Vec<Frame> = blocks.concat();
        let first = audio.iter().position(|f| *f != SILENCE).unwrap() as u32;
        assert!(first <= 3 * 128 && FROM_HOST_TARGET_FILL >= 128);
        assert!(
            audio[first as usize][0] >> 16 >= 20_000,
            "the last stream's frames are gone"
        );
    }
}
