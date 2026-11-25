//! The audio tier's side of the two USB audio streams: when a stream starts over, what is queued for the host, and how far
//! the host's frames have got. `usb_audio` gives them the DMA streams; nothing here touches hardware.
//!
//! The host enables a stream's endpoint a moment before the class is told which alternate it chose. So a stream starts
//! over when the class has been told ([`epoch`](ToHostSender::write) changes), and packets wait until it is the same
//! stream the DMA has.

use super::usb_audio_stream::Frame;
use super::usb_wire::{Depth, PACKET_FRAMES, PacketPacer, ToHostRing};

/// A stream of packets to the host, as the HAL's DMA stream is.
pub trait PacketSink {
    /// Which enable of the endpoint this is.
    fn generation(&self) -> u32;
    /// Packets queued and not sent.
    fn queued(&mut self) -> usize;
    /// Queues a packet; false if it was not taken.
    fn push(&mut self, packet: &'static [u32]) -> bool;
}

/// What was queued since the last [`take_counts`](ToHostSender::take_counts).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct SendCounts {
    pub packets: u32,
    pub frames: u32,
    /// The fewest and most packets queued when one was added, if any was.
    pub ahead: Option<(u32, u32)>,
}

/// Frames that may wait for a packet while the host takes none, before the oldest are dropped.
const MAX_WAITING: u32 = 256;

/// The render's side of the stream to the host: packs each block and queues its packets.
pub struct ToHostSender<S> {
    sink: S,
    mem: *mut u32,
    frames: usize,
    ring: Option<ToHostRing>,
    pacer: PacketPacer,
    epoch: u32,
    generation: u32,
    /// Frames packed, and queued, since the stream started.
    written: u32,
    sent: u32,
    counts: SendCounts,
}

impl<S: PacketSink> ToHostSender<S> {
    /// A sender on `mem`, a ring of `frames` frames for the deepest alternate.
    ///
    /// # Safety
    /// `mem` is [`ToHostRing::words`] words of uncached memory for 24-bit frames, for as long as the sink may read it, and
    /// nothing else writes it.
    pub unsafe fn new(sink: S, mem: *mut u32, frames: usize) -> Self {
        Self {
            sink,
            mem,
            frames,
            ring: None,
            pacer: PacketPacer::new(),
            epoch: 0,
            generation: 0,
            written: 0,
            sent: 0,
            counts: SendCounts::default(),
        }
    }

    /// The stream it queues on.
    pub fn sink(&self) -> &S {
        &self.sink
    }

    /// What was queued since the last take.
    pub fn take_counts(&mut self) -> SendCounts {
        core::mem::take(&mut self.counts)
    }

    /// Queues `block` for the host. `depth` is the alternate's, or `None` when the host collects nothing; `epoch` changes
    /// whenever the class is told of another alternate, so the stream starts over with this block.
    pub fn write(&mut self, epoch: u32, depth: Option<Depth>, block: &[Frame]) {
        let Some(depth) = depth else { return };
        if epoch != self.epoch || self.ring.is_none() {
            self.epoch = epoch;
            self.generation = self.sink.generation();
            // SAFETY: `new`'s contract: room for 24-bit frames, and a ring of the same memory is used at a time.
            self.ring = Some(unsafe { ToHostRing::new(self.mem, self.frames, depth) });
            self.pacer = PacketPacer::new();
            (self.written, self.sent) = (0, 0);
        }
        if self.sink.generation() != self.generation {
            // The endpoint was enabled again, and the class has not said to what yet.
            return;
        }
        let Some(ring) = &self.ring else { return };
        if self.written.wrapping_sub(self.sent) > MAX_WAITING {
            // The host takes none: what waits is stale.
            self.sent = self.written;
        }
        ring.write(self.written, block);
        self.written = self.written.wrapping_add(block.len() as u32);
        while self.written.wrapping_sub(self.sent) >= PACKET_FRAMES as u32 {
            let ahead = self.sink.queued();
            let n = self.pacer.next_size(ahead);
            if !self.sink.push(ring.packet(self.sent, n)) {
                break;
            }
            self.sent = self.sent.wrapping_add(n as u32);
            let ahead = ahead as u32;
            self.counts.packets += 1;
            self.counts.frames += n as u32;
            self.counts.ahead = Some(match self.counts.ahead {
                None => (ahead, ahead),
                Some((low, high)) => (low.min(ahead), high.max(ahead)),
            });
        }
    }
}

/// Where the host's stream has got to, as the HAL's DMA stream tells.
pub trait WireTap {
    /// Which enable of the endpoint this is.
    fn generation(&self) -> u32;
    /// Words received since the endpoint was enabled.
    fn position(&mut self) -> u32;
}

/// How recently the host's stream must have moved for it to count as streaming to us, in microseconds.
pub const RECEIVING_WINDOW_US: u32 = 20_000;

/// How far the host's frames have got.
pub struct FromHostTracker<T> {
    tap: T,
    epoch: u32,
    generation: u32,
    /// The stream has started: the class knows its alternate and the DMA is on it.
    streaming: bool,
    /// The words counted into `frames`: whole frames, so the DMA's position may be inside the next.
    last_words: u32,
    frames: u32,
    /// When the stream last moved, and whether it has.
    moved_at: u32,
    moved: bool,
    /// Frames received since the last take.
    received: u32,
}

impl<T: WireTap> FromHostTracker<T> {
    pub const fn new(tap: T) -> Self {
        Self {
            tap,
            epoch: 0,
            generation: 0,
            streaming: false,
            last_words: 0,
            frames: 0,
            moved_at: 0,
            moved: false,
            received: 0,
        }
    }

    /// The frames that arrived since the last take.
    pub fn take_received(&mut self) -> u32 {
        core::mem::take(&mut self.received)
    }

    /// The frames the host has sent in this stream, which start with those in the ring when the class told of it; `None`
    /// when it sends none (no alternate, or one the class has not been told of). `epoch` is the class's count, `now` µs.
    pub fn poll(&mut self, epoch: u32, depth: Option<Depth>, now: u32) -> Option<u32> {
        let Some(depth) = depth else {
            (self.streaming, self.moved) = (false, false);
            return None;
        };
        if epoch != self.epoch || !self.streaming {
            self.epoch = epoch;
            self.generation = self.tap.generation();
            self.streaming = true;
            self.frames = self.tap.position() / depth.words() as u32;
            self.last_words = self.frames * depth.words() as u32;
            self.moved = false;
            return Some(self.frames);
        }
        if self.tap.generation() != self.generation {
            return None;
        }
        let w = depth.words() as u32;
        let new = self.tap.position().wrapping_sub(self.last_words) / w;
        if new != 0 {
            self.last_words = self.last_words.wrapping_add(new * w);
            self.frames = self.frames.wrapping_add(new);
            self.received = self.received.wrapping_add(new);
            (self.moved_at, self.moved) = (now, true);
        }
        Some(self.frames)
    }

    /// Whether the host's packets have been arriving, as of the last [`poll`](Self::poll).
    pub fn receiving(&self, now: u32) -> bool {
        self.streaming && self.moved && now.wrapping_sub(self.moved_at) < RECEIVING_WINDOW_US
    }
}

#[cfg(test)]
mod tests {
    extern crate std;
    use super::super::usb_wire::PACKET_TARGET_AHEAD;
    use super::*;
    use std::cell::RefCell;
    use std::rc::Rc;
    use std::vec;
    use std::vec::Vec;

    /// A stream to the host as the HAL's: what was pushed, how full it is, and which enable it is on.
    #[derive(Default)]
    struct State {
        pushed: Vec<Vec<u32>>,
        queued: usize,
        generation: u32,
        refuse: bool,
    }
    #[derive(Clone, Default)]
    struct Sink(Rc<RefCell<State>>);

    impl PacketSink for Sink {
        fn generation(&self) -> u32 {
            self.0.borrow().generation
        }
        fn queued(&mut self) -> usize {
            self.0.borrow().queued
        }
        fn push(&mut self, packet: &'static [u32]) -> bool {
            let mut s = self.0.borrow_mut();
            if s.refuse {
                return false;
            }
            s.pushed.push(packet.to_vec());
            s.queued += 1;
            true
        }
    }

    const FRAMES: usize = 2048;

    fn sender(sink: &Sink) -> ToHostSender<Sink> {
        let mem: &'static mut [u32] =
            vec![0u32; ToHostRing::words(FRAMES, Depth::TwentyFour)].leak();
        unsafe { ToHostSender::new(sink.clone(), mem.as_mut_ptr(), FRAMES) }
    }

    /// 128 frames numbered from `from`, in the top 16 bits (both depths keep them).
    fn block(from: i32) -> Vec<Frame> {
        (from..from + 128).map(|v| [v << 16, -(v << 16)]).collect()
    }

    fn on(sink: &Sink, generation: u32) {
        sink.0.borrow_mut().generation = generation;
    }

    /// The frames in every packet pushed, the first channel's top 16 bits.
    fn sent(sink: &Sink, depth: Depth) -> Vec<i32> {
        let s = sink.0.borrow();
        s.pushed
            .iter()
            .flat_map(|p| p.chunks_exact(depth.words()))
            .map(|w| depth.unpack(w)[0] >> 16)
            .collect()
    }

    #[test]
    fn nothing_is_queued_while_the_host_collects_nothing() {
        let sink = Sink::default();
        on(&sink, 1);
        let mut s = sender(&sink);
        s.write(0, None, &block(0));
        assert!(sink.0.borrow().pushed.is_empty());
    }

    /// Every frame written goes out, in order, in packets of 5 or 6, and what does not fill a packet waits.
    #[test]
    fn every_frame_goes_out_once_in_order_in_packets_of_five_or_six() {
        for depth in [Depth::Sixteen, Depth::TwentyFour] {
            let sink = Sink::default();
            on(&sink, 1);
            let mut s = sender(&sink);
            for b in 0..20 {
                s.write(1, Some(depth), &block(b * 128));
                // The host takes two thirds of them.
                let mut st = sink.0.borrow_mut();
                st.queued = st.queued.saturating_sub(16);
            }
            let sizes: Vec<usize> = sink
                .0
                .borrow()
                .pushed
                .iter()
                .map(|p| p.len() / depth.words())
                .collect();
            assert!(
                sizes.iter().all(|n| (5..=6).contains(n)),
                "{depth:?} {sizes:?}"
            );
            let got = sent(&sink, depth);
            assert!(
                got.len() > 20 * 128 - PACKET_FRAMES,
                "{depth:?}: {} of {}",
                got.len(),
                20 * 128
            );
            assert!(
                got.iter().enumerate().all(|(i, v)| *v == i as i32),
                "{depth:?}"
            );
        }
    }

    /// The queue settles at its target: nothing is sent early, and the pacer sees how full it is.
    #[test]
    fn the_pacer_is_told_how_much_is_queued() {
        let sink = Sink::default();
        on(&sink, 1);
        sink.0.borrow_mut().queued = PACKET_TARGET_AHEAD + 30;
        let mut s = sender(&sink);
        s.write(1, Some(Depth::Sixteen), &block(0));
        let full: usize = sink.0.borrow().pushed.iter().map(Vec::len).sum();
        let packets = sink.0.borrow().pushed.len();
        assert!(
            full as f32 / packets as f32 > 5.7,
            "a long queue takes bigger packets: {full} in {packets}"
        );
    }

    /// The endpoint is enabled before the class says which alternate it is: nothing is pushed in between, and what was
    /// written then is not sent in the new stream's depth.
    #[test]
    fn packets_wait_until_the_class_knows_the_alternate() {
        let sink = Sink::default();
        on(&sink, 1);
        let mut s = sender(&sink);
        s.write(1, Some(Depth::TwentyFour), &block(0));
        let before = sink.0.borrow().pushed.len();
        on(&sink, 2); // enabled again: the class still says epoch 1
        s.write(1, Some(Depth::TwentyFour), &block(1000));
        assert_eq!(sink.0.borrow().pushed.len(), before);
        sink.0.borrow_mut().pushed.clear();
        s.write(2, Some(Depth::Sixteen), &block(2000));
        assert!(!sink.0.borrow().pushed.is_empty());
        assert!(
            sent(&sink, Depth::Sixteen)
                .iter()
                .all(|v| (2000..2128).contains(v))
        );
    }

    /// A new epoch starts over, with a fresh ring and the new depth.
    #[test]
    fn a_new_stream_starts_over() {
        let sink = Sink::default();
        on(&sink, 1);
        let mut s = sender(&sink);
        s.write(1, Some(Depth::Sixteen), &block(0));
        sink.0.borrow_mut().pushed.clear();
        sink.0.borrow_mut().queued = 0;
        on(&sink, 2);
        s.write(2, Some(Depth::TwentyFour), &block(5000));
        let first = sent(&sink, Depth::TwentyFour)[0];
        assert_eq!(first, 5000, "nothing left over");
    }

    /// A host that stops taking packets: what does not go out is dropped a little at a time rather than kept, so the
    /// ring is never written over a packet that waits.
    #[test]
    fn a_full_queue_drops_the_oldest_waiting_frames() {
        let sink = Sink::default();
        on(&sink, 1);
        let mut s = sender(&sink);
        s.write(1, Some(Depth::Sixteen), &block(0));
        sink.0.borrow_mut().refuse = true;
        for b in 1..40 {
            s.write(1, Some(Depth::Sixteen), &block(b * 128));
        }
        sink.0.borrow_mut().refuse = false;
        sink.0.borrow_mut().pushed.clear();
        s.write(1, Some(Depth::Sixteen), &block(40 * 128));
        let got = sent(&sink, Depth::Sixteen);
        assert!(!got.is_empty());
        assert!(
            got[0] >= 38 * 128,
            "the stale frames are gone: first {}",
            got[0]
        );
        assert!(got.windows(2).all(|w| w[1] == w[0] + 1));
    }

    /// A packet the queue refused is not lost: it goes out once there is room.
    #[test]
    fn a_refused_packet_goes_out_next_time() {
        let sink = Sink::default();
        on(&sink, 1);
        let mut s = sender(&sink);
        s.write(1, Some(Depth::Sixteen), &block(0));
        sink.0.borrow_mut().refuse = true;
        s.write(1, Some(Depth::Sixteen), &block(128));
        sink.0.borrow_mut().refuse = false;
        s.write(1, Some(Depth::Sixteen), &block(256));
        let got = sent(&sink, Depth::Sixteen);
        assert!(got.len() > 3 * 128 - PACKET_FRAMES);
        assert!(got.iter().enumerate().all(|(i, v)| *v == i as i32));
    }

    /// What was queued since the last take: packets, frames, and how full the queue was.
    #[test]
    fn the_sender_counts_what_it_queued_and_how_full_the_queue_was() {
        let sink = Sink::default();
        on(&sink, 1);
        let mut s = sender(&sink);
        assert_eq!(s.take_counts(), SendCounts::default());
        s.write(1, Some(Depth::Sixteen), &block(0));
        let counts = s.take_counts();
        assert_eq!(counts.packets as usize, sink.0.borrow().pushed.len());
        let frames: usize = sink.0.borrow().pushed.iter().map(Vec::len).sum();
        assert_eq!(counts.frames as usize, frames);
        let (low, high) = counts.ahead.unwrap();
        assert!(
            low == 0 && high as usize == sink.0.borrow().queued - 1,
            "{low}..{high}"
        );
        assert_eq!(
            s.take_counts(),
            SendCounts::default(),
            "each take starts a new count"
        );
    }

    // ── The host's stream ───────────────────────────────────────────────────

    #[derive(Default)]
    struct Tap {
        generation: u32,
        position: u32,
    }
    #[derive(Clone, Default)]
    struct TapRc(Rc<RefCell<Tap>>);
    impl WireTap for TapRc {
        fn generation(&self) -> u32 {
            self.0.borrow().generation
        }
        fn position(&mut self) -> u32 {
            self.0.borrow().position
        }
    }

    fn tap(generation: u32, position: u32) -> TapRc {
        let t = TapRc::default();
        *t.0.borrow_mut() = Tap {
            generation,
            position,
        };
        t
    }

    #[test]
    fn frames_count_the_words_the_dma_has_received() {
        for depth in [Depth::Sixteen, Depth::TwentyFour] {
            let w = depth.words() as u32;
            let t = tap(1, 0);
            let mut h = FromHostTracker::new(t.clone());
            assert_eq!(h.poll(1, Some(depth), 0), Some(0));
            t.0.borrow_mut().position = 128 * w;
            assert_eq!(h.poll(1, Some(depth), 1000), Some(128));
            t.0.borrow_mut().position = 300 * w;
            assert_eq!(h.poll(1, Some(depth), 2000), Some(300));
        }
    }

    /// The DMA's word count wraps after hours; the frame count does not jump.
    #[test]
    fn the_word_count_wrapping_does_not_move_the_frames() {
        let t = tap(1, u32::MAX - 63);
        let mut h = FromHostTracker::new(t.clone());
        // 24 bits, two words a frame: the wrap falls between two frames.
        let first = h.poll(1, Some(Depth::TwentyFour), 0).unwrap();
        t.0.borrow_mut().position = 64;
        assert_eq!(h.poll(1, Some(Depth::TwentyFour), 1).unwrap(), first + 64);
    }

    /// A stream that started over counts from its own first word, and the class must have been told.
    #[test]
    fn a_stream_that_starts_over_counts_from_its_own_beginning() {
        let t = tap(1, 1000);
        let mut h = FromHostTracker::new(t.clone());
        assert_eq!(h.poll(1, Some(Depth::Sixteen), 0), Some(1000));
        *t.0.borrow_mut() = Tap {
            generation: 2,
            position: 10,
        };
        assert_eq!(
            h.poll(1, Some(Depth::Sixteen), 1),
            None,
            "enabled again, the class not told"
        );
        assert_eq!(h.poll(2, Some(Depth::Sixteen), 2), Some(10));
    }

    #[test]
    fn receiving_lasts_while_the_position_moves_and_ends_within_its_window() {
        let t = tap(1, 0);
        let mut h = FromHostTracker::new(t.clone());
        h.poll(1, Some(Depth::Sixteen), 0);
        assert!(!h.receiving(0), "no packet yet");
        t.0.borrow_mut().position = 6;
        h.poll(1, Some(Depth::Sixteen), 5_000);
        assert!(h.receiving(5_000));
        h.poll(1, Some(Depth::Sixteen), 20_000);
        assert!(h.receiving(24_999), "20 ms after the last packet");
        h.poll(1, Some(Depth::Sixteen), 25_001);
        assert!(!h.receiving(25_001));
        t.0.borrow_mut().position = 12;
        h.poll(1, Some(Depth::Sixteen), 30_000);
        assert!(h.receiving(30_000));
    }

    /// The DMA may be between a frame's two words when asked: the half frame counts once its other half is there.
    #[test]
    fn a_position_inside_a_frame_loses_nothing() {
        let t = tap(1, 1);
        let mut h = FromHostTracker::new(t.clone());
        assert_eq!(h.poll(1, Some(Depth::TwentyFour), 0), Some(0));
        for words in [2, 5, 6, 7, 12] {
            t.0.borrow_mut().position = words;
            assert_eq!(h.poll(1, Some(Depth::TwentyFour), words), Some(words / 2));
        }
        assert_eq!(h.take_received(), 6);
    }

    /// What arrived since the last take, whatever the stream did in between.
    #[test]
    fn the_tracker_counts_the_frames_that_arrived() {
        let t = tap(1, 100);
        let mut h = FromHostTracker::new(t.clone());
        h.poll(1, Some(Depth::Sixteen), 0);
        assert_eq!(
            h.take_received(),
            0,
            "what was there at the start is not counted"
        );
        t.0.borrow_mut().position = 400;
        h.poll(1, Some(Depth::Sixteen), 1);
        assert_eq!(h.take_received(), 300);
        assert_eq!(h.take_received(), 0);
    }

    /// Another alternate chosen: the last one's packets do not count as this one's.
    #[test]
    fn a_stream_that_starts_over_has_received_nothing() {
        let t = tap(1, 0);
        let mut h = FromHostTracker::new(t.clone());
        h.poll(1, Some(Depth::Sixteen), 0);
        t.0.borrow_mut().position = 6;
        h.poll(1, Some(Depth::Sixteen), 1_000);
        assert!(h.receiving(1_000));
        *t.0.borrow_mut() = Tap {
            generation: 2,
            position: 0,
        };
        h.poll(2, Some(Depth::TwentyFour), 2_000);
        assert!(!h.receiving(2_000));
    }

    #[test]
    fn an_idle_stream_receives_nothing() {
        let t = tap(1, 0);
        let mut h = FromHostTracker::new(t.clone());
        h.poll(1, Some(Depth::Sixteen), 0);
        t.0.borrow_mut().position = 6;
        h.poll(1, Some(Depth::Sixteen), 1_000);
        assert_eq!(h.poll(2, None, 2_000), None, "alternate 0");
        assert!(!h.receiving(2_000));
    }
}
