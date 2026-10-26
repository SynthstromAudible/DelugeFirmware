//! The board-independent half of USB audio: PCM packing, the two frame rings, the from-host
//! reader's latency window, and the to-host packet sizing.
//!
//! [`usb_audio`](super::usb_audio) wires these to the USB pipes, the SSI clock and the C ABI; the
//! clocking is explained there. Nothing here touches hardware, so the host tests cover it.

use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicU32, Ordering};

/// One stereo frame, each channel a full-scale signed 32-bit sample.
pub type Frame = [i32; 2];

/// The silent frame.
pub const SILENCE: Frame = [0, 0];

/// Each ring's length in frames (a power of two, for the masks): 23 ms at 44.1 kHz.
pub const RING_FRAMES: usize = 1024;
const RING_MASK: usize = RING_FRAMES - 1;

/// How far ahead of the codec the from-host reader holds the host's audio, beyond the output
/// queue's usual depth, in frames. A render block takes up to 128 frames and needs them all there
/// already; this leaves as much again for packet and render jitter.
pub const FROM_HOST_TARGET_FILL: u32 = 256;
/// How far the averaged from-host latency may drift from its target before a frame is dropped or
/// repeated. Packet and render jitter move the average much less than this.
pub const FROM_HOST_TOLERANCE: i32 = 48;
/// At most one drop or repeat per this many frames (23 ms): about 1000 ppm of correction, far more
/// than any crystal mismatch, but never more than one isolated frame at a time.
pub const FROM_HOST_CORRECTION_INTERVAL: u32 = 1024;
/// A fill this close to the ring's length means the reader has fallen a ring behind (the app stopped
/// reading, or a host ignoring our clock raced ahead): it starts over at the target.
const FROM_HOST_OVERRUN_MARGIN: u32 = 64;

/// The to-host backlog, in frames, the packet filler waits for before it starts taking frames from
/// the ring, and trims back to when the ring backs up. The render queues a block of up to 128
/// frames at a time, with up to two blocks per tick; this covers the gap between two of them.
pub const TO_HOST_TARGET_BACKLOG: u32 = 256;
/// How far past [`TO_HOST_TARGET_BACKLOG`] the backlog may grow before the excess is dropped.
pub const TO_HOST_SLACK: u32 = 384;
/// The most frames one to-host packet carries. A high-speed microframe at 44.1 kHz carries 5 or 6;
/// the rest lets a packet catch up after a late interrupt.
pub const PACKET_MAX_FRAMES: usize = 12;
/// More SSI frames than this since the last packet means the stream paused (or never ran): the
/// count restarts from the current position instead of sending the gap.
pub const PACKET_GAP_FRAMES: usize = 64;

/// Bytes per sample for a stream's bit depth (16 or 24); 0 for any other.
pub fn bytes_per_sample(bits: u8) -> usize {
    match bits {
        16 => 2,
        24 => 3,
        _ => 0,
    }
}

/// Decodes one little-endian PCM sample of `bytes.len()` (2 or 3) bytes into the top bits of a
/// full-scale i32.
#[inline]
pub fn decode_sample(bytes: &[u8]) -> i32 {
    match *bytes {
        [b0, b1] => i32::from(i16::from_le_bytes([b0, b1])) << 16,
        [b0, b1, b2] => i32::from_le_bytes([0, b0, b1, b2]),
        _ => 0,
    }
}

/// Encodes a full-scale i32 as a little-endian PCM sample of `out.len()` (2 or 3) bytes: its top
/// bits, truncated.
#[inline]
pub fn encode_sample(sample: i32, out: &mut [u8]) {
    let [_, b1, b2, b3] = sample.to_le_bytes();
    match out {
        [o0, o1] => {
            *o0 = b2;
            *o1 = b3;
        }
        [o0, o1, o2] => {
            *o0 = b1;
            *o1 = b2;
            *o2 = b3;
        }
        _ => {}
    }
}

/// The stereo frames in a packet of interleaved `bps`-byte PCM. A trailing partial frame is
/// ignored.
pub fn unpack(packet: &[u8], bps: usize) -> impl Iterator<Item = Frame> + '_ {
    let frame_bytes = (2 * bps).max(1);
    packet
        .chunks_exact(frame_bytes)
        .filter(move |_| bps != 0)
        .map(move |c| [decode_sample(&c[..bps]), decode_sample(&c[bps..])])
}

/// A single-producer, single-consumer ring of [`RING_FRAMES`] frames.
///
/// Both positions are free-running frame counts; `written - read` is the fill. The producer never
/// overwrites a frame the consumer has not released, so the two sides never touch the same slot:
/// the producer drops what does not fit, and the consumer may skip ahead but never back.
pub struct FrameRing {
    buf: UnsafeCell<[Frame; RING_FRAMES]>,
    written: AtomicU32,
    read: AtomicU32,
}

// SAFETY: the slots are shared by exactly one producer and one consumer, which the `unsafe` methods'
// contracts hold to; the positions that hand slots between them are atomics with release/acquire.
unsafe impl Sync for FrameRing {}

impl FrameRing {
    pub const fn new() -> Self {
        Self {
            buf: UnsafeCell::new([SILENCE; RING_FRAMES]),
            written: AtomicU32::new(0),
            read: AtomicU32::new(0),
        }
    }

    /// Frames the producer has published.
    pub fn written(&self) -> u32 {
        self.written.load(Ordering::Acquire)
    }

    /// Frames the consumer has released.
    pub fn read_pos(&self) -> u32 {
        self.read.load(Ordering::Acquire)
    }

    /// Appends as many of `frames` as fit and publishes them; returns how many.
    ///
    /// # Safety
    ///
    /// Only the ring's one producer calls this, and never reentrantly.
    pub unsafe fn push(&self, frames: impl IntoIterator<Item = Frame>) -> usize {
        let w = self.written.load(Ordering::Relaxed);
        let fill = w.wrapping_sub(self.read.load(Ordering::Acquire)) as usize;
        let space = RING_FRAMES.saturating_sub(fill);
        let base = self.buf.get() as *mut Frame;
        let mut n = 0usize;
        for f in frames.into_iter().take(space) {
            // SAFETY: the slot is past `written` and within a ring of `read`, so the consumer is
            // not reading it; the index is masked into the ring.
            unsafe { base.add((w as usize + n) & RING_MASK).write(f) };
            n += 1;
        }
        self.written
            .store(w.wrapping_add(n as u32), Ordering::Release);
        n
    }

    /// The frame at position `pos`.
    ///
    /// # Safety
    ///
    /// Only the ring's one consumer calls this, with `read_pos() <= pos < written()` (wrapping).
    pub unsafe fn peek(&self, pos: u32) -> Frame {
        // SAFETY: the caller holds the slot (published, not released); the index is masked.
        unsafe {
            (self.buf.get() as *const Frame)
                .add(pos as usize & RING_MASK)
                .read()
        }
    }

    /// Releases every frame before `pos` to the producer.
    ///
    /// # Safety
    ///
    /// Only the ring's one consumer calls this, with `pos` no further than `written()` and never
    /// behind its last release.
    pub unsafe fn release(&self, pos: u32) {
        self.read.store(pos, Ordering::Release);
    }
}

impl Default for FrameRing {
    fn default() -> Self {
        Self::new()
    }
}

/// The consumer of the from-host ring: hands the render the host's frames at a steady latency.
///
/// What it steers is the latency from a frame's arrival to its playback: the ring's fill at the
/// render, plus `lead`, the frames the codec has queued ahead of the block being rendered. The
/// lead swings by a block or more with how busy the render is, and the fill swings against it; only
/// their sum moves with the host's clock against ours. A host that follows the implicit feedback
/// holds the sum still. One that does not drifts it a few frames a second, and the reader drops or
/// repeats one frame at a time to keep it in the window.
pub struct FromHostReader {
    generation: u32,
    primed: bool,
    read: u32,
    /// The latency this stream is held at, in frames: set when it primes.
    target: u32,
    /// The averaged latency, 24.8 fixed point.
    avg_latency: i32,
    /// The averaged lead, 24.8 fixed point; `None` until the first read.
    avg_lead: Option<i32>,
    since_correction: u32,
    /// The last frame handed out, for a repeat.
    last: Frame,
}

impl FromHostReader {
    pub const fn new() -> Self {
        Self {
            generation: 0,
            primed: false,
            read: 0,
            target: 0,
            avg_latency: 0,
            avg_lead: None,
            since_correction: 0,
            last: SILENCE,
        }
    }

    /// Fills `out` with the host's next frames and returns how many came from the host; the rest
    /// are silence.
    ///
    /// `generation` changes each time the stream (re)starts, which discards what is left of the
    /// last one. `lead` is how many frames the codec plays before it reaches `out[0]`.
    ///
    /// # Safety
    ///
    /// The caller is `ring`'s one consumer, and calls this for no other ring.
    pub unsafe fn read(
        &mut self,
        ring: &FrameRing,
        generation: u32,
        lead: u32,
        out: &mut [Frame],
    ) -> usize {
        let n = out.len();
        let written = ring.written();
        if generation != self.generation {
            self.generation = generation;
            self.read = written;
            self.primed = false;
        }

        let lead_fp = (lead.min(1 << 20) as i32) << 8;
        let avg_lead = match self.avg_lead {
            Some(avg) => avg + ((lead_fp - avg) >> 5),
            None => lead_fp,
        };
        self.avg_lead = Some(avg_lead);

        let mut fill = written.wrapping_sub(self.read);
        if fill > RING_FRAMES as u32 {
            // Not this stream's frames: start from the newest.
            self.read = written;
            fill = 0;
            self.primed = false;
        }

        if !self.primed || fill > RING_FRAMES as u32 - FROM_HOST_OVERRUN_MARGIN {
            // (Re)start at the target, measured against the lead's average so the swing of the
            // lead at this one block does not set the stream's latency.
            let target = FROM_HOST_TARGET_FILL + (avg_lead >> 8) as u32;
            if fill + lead < target {
                out.fill(SILENCE);
                // SAFETY: our own position, no further than `written`.
                unsafe { ring.release(self.read) };
                return 0;
            }
            let want = (target - lead).min(RING_FRAMES as u32 - FROM_HOST_OVERRUN_MARGIN);
            self.read = written.wrapping_sub(want);
            fill = want;
            self.primed = true;
            self.target = target;
            self.avg_latency = (target as i32) << 8;
            self.since_correction = 0;
        }

        self.avg_latency += ((((fill + lead) as i32) << 8) - self.avg_latency) >> 5;
        self.since_correction = self.since_correction.saturating_add(n as u32);
        let mut repeat = false;
        if self.since_correction >= FROM_HOST_CORRECTION_INTERVAL {
            let error = (self.avg_latency >> 8) - self.target as i32;
            if error > FROM_HOST_TOLERANCE && fill > 0 {
                // The host runs fast: drop a frame.
                self.read = self.read.wrapping_add(1);
                fill -= 1;
                self.since_correction = 0;
            } else if error < -FROM_HOST_TOLERANCE && fill as usize >= n && n > 0 {
                // The host runs slow: play the last frame again.
                repeat = true;
                self.since_correction = 0;
            }
        }

        let mut o = 0;
        if repeat {
            out[0] = self.last;
            o = 1;
        }
        let copy = (n - o).min(fill as usize);
        for (i, slot) in out[o..o + copy].iter_mut().enumerate() {
            // SAFETY: `read + i` is below `read + fill = written` and not yet released.
            *slot = unsafe { ring.peek(self.read.wrapping_add(i as u32)) };
        }
        self.read = self.read.wrapping_add(copy as u32);
        if o + copy > 0 {
            self.last = out[o + copy - 1];
        }
        if o + copy < n {
            // Ran dry: the host stopped or fell behind. Silence until the window refills.
            out[o + copy..].fill(SILENCE);
            self.primed = false;
        }
        // SAFETY: our own position, no further than `written`.
        unsafe { ring.release(self.read) };
        o + copy
    }
}

impl Default for FromHostReader {
    fn default() -> Self {
        Self::new()
    }
}

/// The consumer of the to-host ring: sizes and fills each to-host packet.
///
/// Each packet carries as many frames as the codec clock advanced since the previous one, so the
/// stream's rate is the codec's. Its content comes from the ring the render fills, held at
/// [`TO_HOST_TARGET_BACKLOG`]: silence while the backlog builds, the excess dropped if it backs up.
pub struct ToHostPacer {
    epoch: u32,
    anchored: bool,
    /// The codec clock position the last packet counted up to, in frames.
    last_pos: usize,
    primed: bool,
}

impl ToHostPacer {
    pub const fn new() -> Self {
        Self {
            epoch: 0,
            anchored: false,
            last_pos: 0,
            primed: false,
        }
    }

    /// How many frames the next packet carries, given the codec clock's position `pos` in a ring of
    /// `mask + 1` frames, and the packet's room for `room` frames.
    ///
    /// `epoch` changes each time the stream (re)starts; the first packet after it, or after a gap,
    /// carries nothing and restarts the count from `pos`.
    pub fn frames_for_packet(&mut self, epoch: u32, pos: usize, mask: usize, room: usize) -> usize {
        let elapsed = pos.wrapping_sub(self.last_pos) & mask;
        if epoch != self.epoch || !self.anchored || elapsed > PACKET_GAP_FRAMES {
            self.epoch = epoch;
            self.anchored = true;
            self.last_pos = pos & mask;
            self.primed = false;
            return 0;
        }
        let n = elapsed.min(PACKET_MAX_FRAMES).min(room);
        self.last_pos = (self.last_pos + n) & mask;
        n
    }

    /// Fills one packet of `out` with `bps`-byte PCM and returns its length in bytes.
    ///
    /// # Safety
    ///
    /// The caller is `ring`'s one consumer, and calls this for no other ring.
    pub unsafe fn fill_packet(
        &mut self,
        ring: &FrameRing,
        epoch: u32,
        pos: usize,
        mask: usize,
        bps: usize,
        out: &mut [u8],
    ) -> usize {
        if bps == 0 {
            return 0;
        }
        let frame_bytes = 2 * bps;
        let restart = epoch != self.epoch;
        let n = self.frames_for_packet(epoch, pos, mask, out.len() / frame_bytes);
        let written = ring.written();
        let mut read = ring.read_pos();
        if restart {
            // A new stream: what is left in the ring belongs to the last one.
            read = written;
        }
        let mut backlog = written.wrapping_sub(read);
        if !self.primed && backlog >= TO_HOST_TARGET_BACKLOG {
            self.primed = true;
        }
        if self.primed && backlog > TO_HOST_TARGET_BACKLOG + TO_HOST_SLACK {
            read = written.wrapping_sub(TO_HOST_TARGET_BACKLOG);
            backlog = TO_HOST_TARGET_BACKLOG;
        }
        let take = if self.primed {
            n.min(backlog as usize)
        } else {
            0
        };
        for (i, chunk) in out[..n * frame_bytes]
            .chunks_exact_mut(frame_bytes)
            .enumerate()
        {
            let frame = if i < take {
                // SAFETY: `read + i` is below `read + backlog = written` and not yet released.
                unsafe { ring.peek(read.wrapping_add(i as u32)) }
            } else {
                SILENCE
            };
            encode_sample(frame[0], &mut chunk[..bps]);
            encode_sample(frame[1], &mut chunk[bps..]);
        }
        read = read.wrapping_add(take as u32);
        if self.primed && take < n {
            // Ran dry: the render stalled. Silence until the backlog rebuilds.
            self.primed = false;
        }
        // SAFETY: our own position, no further than `written`, never behind the last release (the
        // producer only reads it).
        unsafe { ring.release(read) };
        n * frame_bytes
    }
}

impl Default for ToHostPacer {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    extern crate std;
    use super::*;
    use std::vec::Vec;

    fn push(ring: &FrameRing, frames: impl IntoIterator<Item = Frame>) -> usize {
        unsafe { ring.push(frames) }
    }

    fn ramp(start: i32, n: usize) -> impl Iterator<Item = Frame> {
        (0..n as i32).map(move |i| [start + i, -(start + i)])
    }

    #[test]
    fn sixteen_bit_samples_land_in_the_top_bits() {
        assert_eq!(decode_sample(&[0x34, 0x12]), 0x1234_0000);
        assert_eq!(decode_sample(&[0x00, 0x80]), i32::MIN);
        assert_eq!(decode_sample(&[0xFF, 0x7F]), 0x7FFF_0000);
        assert_eq!(decode_sample(&[0xFF, 0xFF]), -0x1_0000);
    }

    #[test]
    fn twenty_four_bit_samples_land_in_the_top_bits() {
        assert_eq!(decode_sample(&[0x56, 0x34, 0x12]), 0x1234_5600);
        assert_eq!(decode_sample(&[0x00, 0x00, 0x80]), i32::MIN);
        assert_eq!(decode_sample(&[0xFF, 0xFF, 0xFF]), -0x100);
    }

    #[test]
    fn encoding_keeps_the_top_bits() {
        let mut two = [0u8; 2];
        encode_sample(0x1234_5678, &mut two);
        assert_eq!(two, [0x34, 0x12]);
        let mut three = [0u8; 3];
        encode_sample(0x1234_5678, &mut three);
        assert_eq!(three, [0x56, 0x34, 0x12]);
        encode_sample(i32::MIN, &mut three);
        assert_eq!(three, [0x00, 0x00, 0x80]);
    }

    #[test]
    fn decode_inverts_encode_at_each_depth() {
        for s in [0i32, 1 << 16, -(1 << 16), i32::MIN, 0x7FFF_FF00, -0x100] {
            let mut three = [0u8; 3];
            encode_sample(s, &mut three);
            assert_eq!(decode_sample(&three), s & !0xFF);
            let mut two = [0u8; 2];
            encode_sample(s, &mut two);
            assert_eq!(decode_sample(&two), s & !0xFFFF);
        }
    }

    #[test]
    fn unpack_reads_interleaved_frames_and_ignores_a_partial_one() {
        let packet = [0x00, 0x40, 0x00, 0xC0, 0x01, 0x00, 0xAA];
        let frames: Vec<Frame> = unpack(&packet, 2).collect();
        assert_eq!(frames, [[0x4000_0000, -0x4000_0000]]);
        let packet24 = [1, 2, 3, 4, 5, 6];
        let frames: Vec<Frame> = unpack(&packet24, 3).collect();
        assert_eq!(frames, [[0x0302_0100, 0x0605_0400]]);
        assert_eq!(unpack(&packet24, 0).count(), 0);
    }

    #[test]
    fn the_producer_drops_what_does_not_fit() {
        let ring = FrameRing::new();
        assert_eq!(push(&ring, ramp(0, RING_FRAMES - 10)), RING_FRAMES - 10);
        assert_eq!(push(&ring, ramp(0, 30)), 10);
        assert_eq!(ring.written(), RING_FRAMES as u32);
        unsafe { ring.release(5) };
        assert_eq!(push(&ring, ramp(0, 30)), 5);
    }

    #[test]
    fn positions_wrap_around_u32() {
        let ring = FrameRing::new();
        ring.written.store(u32::MAX - 2, Ordering::Relaxed);
        ring.read.store(u32::MAX - 2, Ordering::Relaxed);
        assert_eq!(push(&ring, ramp(7, 6)), 6);
        assert_eq!(ring.written(), 3);
        assert_eq!(unsafe { ring.peek(u32::MAX) }, [9, -9]);
        assert_eq!(unsafe { ring.peek(1) }, [11, -11]);
    }

    /// Reads one block of `n` frames.
    fn read_block(
        reader: &mut FromHostReader,
        ring: &FrameRing,
        generation: u32,
        lead: u32,
        n: usize,
    ) -> (usize, Vec<Frame>) {
        let mut out = std::vec![[1, 1]; n];
        let got = unsafe { reader.read(ring, generation, lead, &mut out) };
        (got, out)
    }

    #[test]
    fn the_reader_is_silent_until_the_window_fills() {
        let ring = FrameRing::new();
        let mut reader = FromHostReader::new();
        push(&ring, ramp(0, 100));
        let (got, out) = read_block(&mut reader, &ring, 0, 0, 64);
        assert_eq!(got, 0);
        assert!(out.iter().all(|f| *f == SILENCE));
        // Nothing consumed while waiting.
        assert_eq!(ring.read_pos(), 0);
    }

    #[test]
    fn the_reader_starts_at_the_target_latency() {
        let ring = FrameRing::new();
        let mut reader = FromHostReader::new();
        push(&ring, ramp(0, 400));
        let lead = 100;
        let (got, out) = read_block(&mut reader, &ring, 0, lead, 64);
        assert_eq!(got, 64);
        // Fill + lead = target: the read starts FROM_HOST_TARGET_FILL frames behind the newest.
        let first = 400 - FROM_HOST_TARGET_FILL as i32;
        assert_eq!(out[0], [first, -first]);
        assert_eq!(out[63], [first + 63, -(first + 63)]);
        assert_eq!(ring.read_pos(), (first + 64) as u32);
    }

    #[test]
    fn the_reader_underruns_to_silence_and_reprimes() {
        let ring = FrameRing::new();
        let mut reader = FromHostReader::new();
        push(&ring, ramp(0, 300));
        let (got, _) = read_block(&mut reader, &ring, 0, 0, 200);
        assert_eq!(got, 200);
        let (got, out) = read_block(&mut reader, &ring, 0, 0, 100);
        assert_eq!(got, 56);
        assert!(out[56..].iter().all(|f| *f == SILENCE));
        // Primed again only once the window has refilled.
        push(&ring, ramp(1000, 100));
        let (got, _) = read_block(&mut reader, &ring, 0, 0, 16);
        assert_eq!(got, 0);
    }

    #[test]
    fn a_new_generation_discards_the_last_stream() {
        let ring = FrameRing::new();
        let mut reader = FromHostReader::new();
        push(&ring, ramp(0, 600));
        read_block(&mut reader, &ring, 0, 0, 16);
        let (got, _) = read_block(&mut reader, &ring, 1, 0, 16);
        assert_eq!(got, 0);
        assert_eq!(ring.read_pos(), 600);
    }

    #[test]
    fn a_host_on_our_clock_is_copied_straight_through() {
        let ring = FrameRing::new();
        let mut reader = FromHostReader::new();
        let mut last = None::<i32>;
        let mut from_host = 0;
        for b in 0..400 {
            push(&ring, ramp(b * 128, 128));
            let (got, out) = read_block(&mut reader, &ring, 0, 128, 128);
            from_host += got;
            for f in &out[..got] {
                if let Some(l) = last {
                    assert_eq!(f[0] - l, 1);
                }
                last = Some(f[0]);
            }
        }
        assert_eq!(from_host, 128 * 399);
    }

    /// Streams blocks of 128 frames, the host sending one extra frame (or one fewer) every 16
    /// blocks: about 500 ppm off our clock. Returns the frames dropped and repeated.
    fn drifting_host(extra: i32) -> (usize, usize) {
        let ring = FrameRing::new();
        let mut reader = FromHostReader::new();
        let mut next_in = 0i32;
        let mut last = None::<i32>;
        let (mut drops, mut repeats) = (0, 0);
        for b in 0..4000 {
            let n_in = (128 + if b % 16 == 0 { extra } else { 0 }) as usize;
            push(&ring, ramp(next_in, n_in));
            next_in += n_in as i32;
            let (got, out) = read_block(&mut reader, &ring, 0, 128, 128);
            if b > 4 {
                assert_eq!(got, 128, "underrun at block {b}");
            }
            for f in &out[..got] {
                if let Some(l) = last {
                    match f[0] - l {
                        1 => {}
                        2 => drops += 1,
                        0 => repeats += 1,
                        step => panic!("step {step} at block {b}"),
                    }
                }
                last = Some(f[0]);
            }
        }
        (drops, repeats)
    }

    #[test]
    fn a_fast_host_loses_single_frames() {
        // 250 frames of drift: the window takes up to its tolerance, single drops the rest.
        let (drops, repeats) = drifting_host(1);
        assert_eq!(repeats, 0);
        assert!(drops > 150, "drops {drops}");
    }

    #[test]
    fn a_slow_host_repeats_single_frames() {
        let (drops, repeats) = drifting_host(-1);
        assert_eq!(drops, 0);
        assert!(repeats > 150, "repeats {repeats}");
    }

    #[test]
    fn a_swinging_lead_does_not_move_the_stream() {
        // Every third render runs late and catches up with two blocks: the first sees a block
        // more arrived and a block less queued ahead of it. The sum holds, so nothing is corrected.
        let ring = FrameRing::new();
        let mut reader = FromHostReader::new();
        let mut next_in = 0i32;
        let mut last = None::<i32>;
        let mut from_host = 0;
        for b in 0..3000 {
            push(&ring, ramp(next_in, 128));
            next_in += 128;
            let leads: &[u32] = match b % 3 {
                0 => &[900],
                1 => &[],
                _ => &[772, 900],
            };
            for &lead in leads {
                let (got, out) = read_block(&mut reader, &ring, 0, lead, 128);
                from_host += got;
                for f in &out[..got] {
                    if let Some(l) = last {
                        assert_eq!(f[0] - l, 1, "block {b}");
                    }
                    last = Some(f[0]);
                }
            }
        }
        assert!(from_host > 128 * 1990);
    }

    #[test]
    fn packets_carry_the_frames_the_clock_advanced() {
        let mut pacer = ToHostPacer::new();
        let mask = 2047;
        // The first packet anchors.
        assert_eq!(pacer.frames_for_packet(1, 100, mask, 49), 0);
        assert_eq!(pacer.frames_for_packet(1, 105, mask, 49), 5);
        assert_eq!(pacer.frames_for_packet(1, 111, mask, 49), 6);
        // Across the clock ring's wrap.
        let mut p = ToHostPacer::new();
        p.frames_for_packet(1, 2045, mask, 49);
        assert_eq!(p.frames_for_packet(1, 3, mask, 49), 6);
    }

    #[test]
    fn a_late_packet_catches_up_over_the_next_ones() {
        let mut pacer = ToHostPacer::new();
        let mask = 2047;
        pacer.frames_for_packet(1, 0, mask, 49);
        assert_eq!(pacer.frames_for_packet(1, 20, mask, 49), PACKET_MAX_FRAMES);
        assert_eq!(
            pacer.frames_for_packet(1, 20, mask, 49),
            20 - PACKET_MAX_FRAMES
        );
        // Over a long run the frames sent match the clock exactly.
        let mut pos = 20usize;
        let mut sent = 20usize;
        for i in 0..8000usize {
            pos += 5 + (i % 2) + usize::from(i % 7 == 0);
            sent += pacer.frames_for_packet(1, pos & mask, mask, 49);
        }
        assert!(pos - sent <= PACKET_MAX_FRAMES);
    }

    #[test]
    fn a_gap_or_a_new_epoch_restarts_the_count() {
        let mut pacer = ToHostPacer::new();
        let mask = 2047;
        pacer.frames_for_packet(1, 0, mask, 49);
        assert_eq!(pacer.frames_for_packet(1, 500, mask, 49), 0);
        assert_eq!(pacer.frames_for_packet(1, 506, mask, 49), 6);
        assert_eq!(pacer.frames_for_packet(2, 512, mask, 49), 0);
        assert_eq!(pacer.frames_for_packet(2, 517, mask, 49), 5);
        // The packet's room caps it.
        assert_eq!(pacer.frames_for_packet(2, 527, mask, 3), 3);
    }

    #[test]
    fn packets_are_silent_until_the_backlog_builds_then_carry_the_ring() {
        let ring = FrameRing::new();
        let mut pacer = ToHostPacer::new();
        let mask = 2047;
        let mut buf = [0xEEu8; 294];
        unsafe {
            assert_eq!(pacer.fill_packet(&ring, 1, 0, mask, 3, &mut buf), 0);
            push(&ring, ramp(0x100, 100));
            assert_eq!(pacer.fill_packet(&ring, 1, 6, mask, 3, &mut buf), 36);
            assert!(buf[..36].iter().all(|b| *b == 0));
            assert_eq!(ring.read_pos(), 0);
            push(&ring, ramp(0x200, 200));
            assert_eq!(pacer.fill_packet(&ring, 1, 11, mask, 2, &mut buf), 20);
        }
        assert_eq!(ring.read_pos(), 5);
        let mut buf24 = [0u8; 294];
        unsafe {
            assert_eq!(pacer.fill_packet(&ring, 1, 13, mask, 3, &mut buf24), 12);
        }
        assert_eq!(decode_sample(&buf24[0..3]), 0x105 & !0xFF);
        assert_eq!(ring.read_pos(), 7);
    }

    #[test]
    fn a_backed_up_ring_is_trimmed_to_the_target() {
        let ring = FrameRing::new();
        let mut pacer = ToHostPacer::new();
        let mask = 2047;
        let mut buf = [0u8; 294];
        unsafe {
            pacer.fill_packet(&ring, 1, 0, mask, 3, &mut buf);
            push(&ring, ramp(0, 1000));
            pacer.fill_packet(&ring, 1, 6, mask, 3, &mut buf);
        }
        assert_eq!(ring.written() - ring.read_pos(), TO_HOST_TARGET_BACKLOG - 6);
    }

    #[test]
    fn an_underrun_pads_with_silence_and_waits_for_the_backlog() {
        let ring = FrameRing::new();
        let mut pacer = ToHostPacer::new();
        let mask = 2047;
        let mut buf = [0xEEu8; 294];
        let backlog = |r: &FrameRing| r.written().wrapping_sub(r.read_pos());
        unsafe {
            pacer.fill_packet(&ring, 1, 0, mask, 3, &mut buf);
            push(&ring, ramp(1 << 20, TO_HOST_TARGET_BACKLOG as usize + 4));
            let mut pos = 0;
            while backlog(&ring) >= 6 {
                pos += 6;
                assert_eq!(pacer.fill_packet(&ring, 1, pos, mask, 3, &mut buf), 36);
            }
            // What is left goes out, padded with silence.
            let left = backlog(&ring) as usize;
            assert!(left > 0);
            pos += 6;
            assert_eq!(pacer.fill_packet(&ring, 1, pos, mask, 3, &mut buf), 36);
            assert_eq!(backlog(&ring), 0);
            assert!(buf[left * 6..36].iter().all(|b| *b == 0));
            assert!(buf[..left * 6].iter().any(|b| *b != 0));
            // A little more is not enough to restart.
            push(&ring, ramp(0, 10));
            pos += 6;
            pacer.fill_packet(&ring, 1, pos, mask, 3, &mut buf);
            assert_eq!(backlog(&ring), 10);
        }
    }

    #[test]
    fn a_new_epoch_discards_the_ring() {
        let ring = FrameRing::new();
        let mut pacer = ToHostPacer::new();
        let mut buf = [0u8; 294];
        push(&ring, ramp(0, 500));
        unsafe { pacer.fill_packet(&ring, 3, 0, 2047, 3, &mut buf) };
        assert_eq!(ring.read_pos(), 500);
    }
}
