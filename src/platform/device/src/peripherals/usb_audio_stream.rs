//! The board-independent half of playing the host's audio: the reader's latency window over the frames it sends.
//!
//! `usb_audio` gives it the DMA's ring and the codec's queue; the clocking is explained there. Nothing here touches
//! hardware, so the host tests cover it.

/// One stereo frame, each channel a full-scale signed 32-bit sample.
pub type Frame = [i32; 2];

/// The silent frame.
pub const SILENCE: Frame = [0, 0];

/// How far ahead of the codec the from-host reader holds the host's audio, beyond the output
/// queue's depth, in frames. A render block takes 128 frames and needs them all there already;
/// this leaves as much again for packet and render jitter.
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

/// Where the from-host reader takes its frames from: a ring that something else fills.
pub trait FrameSource {
    /// Frames it has been given in all, as a free-running count.
    fn written(&self) -> u32;
    /// How many frames it can hold before it overwrites the oldest.
    fn capacity(&self) -> u32;
    /// The frame at position `pos`.
    ///
    /// # Safety
    /// `pos` is not before `written() - capacity()` and is before `written()` (wrapping).
    unsafe fn peek(&self, pos: u32) -> Frame;
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
    /// The caller is the stream's one reader, and calls this for no other stream.
    pub fn read<S: FrameSource>(
        &mut self,
        ring: &S,
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
        if fill > ring.capacity() {
            // Not this stream's frames: start from the newest.
            self.read = written;
            fill = 0;
            self.primed = false;
        }

        if !self.primed || fill > ring.capacity() - FROM_HOST_OVERRUN_MARGIN {
            // (Re)start at the target, measured against the lead's average so the swing of the
            // lead at this one block does not set the stream's latency.
            let target = FROM_HOST_TARGET_FILL + (avg_lead >> 8) as u32;
            if fill + lead < target {
                out.fill(SILENCE);
                return 0;
            }
            let want = (target - lead).min(ring.capacity() - FROM_HOST_OVERRUN_MARGIN);
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
            // SAFETY: `read + i` is below `read + fill = written`, and `fill` is within the ring.
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
        o + copy
    }
}

impl Default for FromHostReader {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    extern crate std;
    use super::*;
    use core::cell::{Cell, RefCell};
    use std::vec::Vec;

    /// A ring something else fills, overwriting the oldest frames as a DMA does.
    struct Ring {
        frames: RefCell<Vec<Frame>>,
        written: Cell<u32>,
    }

    impl Ring {
        const FRAMES: usize = 1024;

        fn new() -> Self {
            Self {
                frames: RefCell::new(std::vec![SILENCE; Self::FRAMES]),
                written: Cell::new(0),
            }
        }
    }

    impl FrameSource for Ring {
        fn written(&self) -> u32 {
            self.written.get()
        }

        fn capacity(&self) -> u32 {
            Self::FRAMES as u32
        }

        unsafe fn peek(&self, pos: u32) -> Frame {
            assert!(
                self.written.get().wrapping_sub(pos).wrapping_sub(1) < Self::FRAMES as u32,
                "frame {pos} is not in the ring"
            );
            self.frames.borrow()[pos as usize % Self::FRAMES]
        }
    }

    fn push(ring: &Ring, frames: impl IntoIterator<Item = Frame>) {
        for f in frames {
            let w = ring.written.get();
            ring.frames.borrow_mut()[w as usize % Ring::FRAMES] = f;
            ring.written.set(w.wrapping_add(1));
        }
    }

    fn ramp(start: i32, n: usize) -> impl Iterator<Item = Frame> {
        (0..n as i32).map(move |i| [start + i, -(start + i)])
    }

    /// Reads one block of `n` frames.
    fn read_block(
        reader: &mut FromHostReader,
        ring: &Ring,
        generation: u32,
        lead: u32,
        n: usize,
    ) -> (usize, Vec<Frame>) {
        let mut out = std::vec![[1, 1]; n];
        let got = reader.read(ring, generation, lead, &mut out);
        (got, out)
    }

    #[test]
    fn the_reader_is_silent_until_the_window_fills() {
        let ring = Ring::new();
        let mut reader = FromHostReader::new();
        push(&ring, ramp(0, 100));
        let (got, out) = read_block(&mut reader, &ring, 0, 0, 64);
        assert_eq!(got, 0);
        assert!(out.iter().all(|f| *f == SILENCE));
        // Nothing consumed while waiting.
        assert_eq!(reader.read, 0);
    }

    #[test]
    fn the_reader_starts_at_the_target_latency() {
        let ring = Ring::new();
        let mut reader = FromHostReader::new();
        push(&ring, ramp(0, 400));
        let lead = 100;
        let (got, out) = read_block(&mut reader, &ring, 0, lead, 64);
        assert_eq!(got, 64);
        // Fill + lead = target: the read starts FROM_HOST_TARGET_FILL frames behind the newest.
        let first = 400 - FROM_HOST_TARGET_FILL as i32;
        assert_eq!(out[0], [first, -first]);
        assert_eq!(out[63], [first + 63, -(first + 63)]);
        assert_eq!(reader.read, (first + 64) as u32);
    }

    #[test]
    fn the_reader_underruns_to_silence_and_reprimes() {
        let ring = Ring::new();
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
        let ring = Ring::new();
        let mut reader = FromHostReader::new();
        push(&ring, ramp(0, 600));
        read_block(&mut reader, &ring, 0, 0, 16);
        let (got, _) = read_block(&mut reader, &ring, 1, 0, 16);
        assert_eq!(got, 0);
        assert_eq!(reader.read, 600);
    }

    /// The app stopped reading for longer than the ring holds: what the DMA has written over is not played.
    #[test]
    fn a_reader_the_ring_has_lapped_starts_over_at_the_target() {
        for behind in [Ring::FRAMES as i32 - 32, 3 * Ring::FRAMES as i32] {
            let ring = Ring::new();
            let mut reader = FromHostReader::new();
            push(&ring, ramp(0, 400));
            read_block(&mut reader, &ring, 0, 0, 64);
            push(&ring, ramp(400, behind as usize));
            let mut first = None;
            for b in 0..8 {
                let (got, out) = read_block(&mut reader, &ring, 0, 0, 64);
                if got > 0 {
                    first.get_or_insert(out[0][0]);
                }
                push(&ring, ramp(400 + behind + b * 64, 64));
            }
            let first = first.expect("playing again");
            assert!(
                first >= 400 + behind - Ring::FRAMES as i32,
                "{behind}: {first}"
            );
        }
    }

    #[test]
    fn a_host_on_our_clock_is_copied_straight_through() {
        let ring = Ring::new();
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
        let ring = Ring::new();
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
        let ring = Ring::new();
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
}
