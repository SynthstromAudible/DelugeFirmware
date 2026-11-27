//! The timed output service's logic, without hardware: queues of MIDI, CV and gate events keyed by
//! play-timeline stamps, and the decision of what is due. The device runs it from the output
//! timer's interrupt and after every submit; the host after every submit and render block. Both
//! pass their hardware as [`Outputs`].
//!
//! A stamp is a frame on the play timeline (wrapping, compared by signed difference). An event is
//! due once the play position is within a sample of its stamp; a CV word [`Config::cv_lead_micros`]
//! earlier, so the DAC has settled when its gate switches. [`STAMP_NOW`] is due at once and sorts
//! before every real stamp.

use heapless::{Deque, Vec};

/// The stamp for "as soon as possible".
pub const STAMP_NOW: u32 = 0;
/// Microseconds per frame at 44.1 kHz, Q16 (1e6 / 44100 · 65536, rounded).
pub const MICROS_PER_FRAME_Q16: u64 = 1_486_078;
/// How soon to try again when something due could not go out.
pub const RETRY_MICROS: u64 = 100;
/// The DIN port's queue.
pub const DIN: usize = 0;
/// The USB peripheral port's queue.
pub const USB: usize = 1;
/// CV channels; CV channel `n` gates gate channel `n`.
pub const NUM_CV: usize = 2;
pub const NUM_GATES: usize = 4;

const DIN_ENTRIES: usize = 256;
const DIN_BYTES: usize = 1024;
const USB_PACKETS: usize = 1024;
const EDGES: usize = 16;
/// An event sent more than this many frames after its stamp is late.
const LATE_FRAMES: i32 = 1;

pub struct Config {
    /// The furthest ahead a stamp may be: the output latency. One further ahead is stale (the
    /// render hasn't run for a while) and goes at once.
    pub max_ahead_frames: u32,
    /// How long before its stamp a CV word is written.
    pub cv_lead_micros: u64,
    /// How early an event may go: about a sample.
    pub tolerance_micros: u64,
}

impl Config {
    /// Microseconds from `now` until `stamp` is due.
    fn until(&self, stamp: u32, now: Clock) -> u64 {
        micros_for_frames(frames_until(stamp, now.frame, self.max_ahead_frames))
    }

    /// Microseconds from `now` until a CV word stamped `stamp` is written.
    fn cv_until(&self, stamp: u32, now: Clock) -> u64 {
        self.until(stamp, now).saturating_sub(self.cv_lead_micros)
    }
}

impl Default for Service {
    fn default() -> Self {
        Self::new()
    }
}

/// The play position and the clock deadlines are returned on, read together.
#[derive(Clone, Copy, Debug)]
pub struct Clock {
    pub frame: u32,
    pub micros: u64,
}

/// The hardware the service drives. Every call returns at once.
pub trait Outputs {
    /// Put as much of `bytes` in the DIN transmit FIFO as fits; returns how much.
    fn din_write(&mut self, bytes: &[u8]) -> usize;
    /// Queue one USB-MIDI event packet; false if the queue is full.
    fn usb_send(&mut self, packet: [u8; 4]) -> bool;
    /// Set gate `channel`'s pin to `level`.
    fn gate_set(&mut self, channel: u8, level: bool);
    /// Write CV `channel`'s DAC word; false, writing nothing, while the bus is busy.
    fn cv_write(&mut self, channel: u8, value: u16) -> bool;
}

#[derive(Default, Clone, Copy, Debug, PartialEq, Eq)]
pub struct Stats {
    /// Events sent more than a sample after their stamp.
    pub late: u32,
    /// Events refused because their queue was full.
    pub dropped: u32,
    /// CV words among `late`.
    pub cv_late: u32,
}

/// Frames from `play_now` until `stamp`: 0 for [`STAMP_NOW`], a stamp at or behind the play
/// position, or one more than `max_ahead` frames ahead (stale).
pub fn frames_until(stamp: u32, play_now: u32, max_ahead: u32) -> u32 {
    if stamp == STAMP_NOW {
        return 0;
    }
    let ahead = stamp.wrapping_sub(play_now) as i32;
    if ahead <= 0 || ahead as u32 > max_ahead {
        0
    } else {
        ahead as u32
    }
}

/// `frames` in microseconds, without a division.
pub fn micros_for_frames(frames: u32) -> u64 {
    (u64::from(frames) * MICROS_PER_FRAME_Q16) >> 16
}

/// Whether `a` goes no later than `b`: NOW before every real stamp, real stamps by signed
/// difference.
fn no_later(a: u32, b: u32) -> bool {
    match (a == STAMP_NOW, b == STAMP_NOW) {
        (true, _) => true,
        (false, true) => false,
        (false, false) => (b.wrapping_sub(a) as i32) >= 0,
    }
}

/// Whether an event stamped `stamp` and sent at `now` is late.
fn is_late(stamp: u32, now: u32) -> bool {
    stamp != STAMP_NOW && (now.wrapping_sub(stamp) as i32) > LATE_FRAMES
}

struct DinEntry {
    stamp: u32,
    len: u16,
    /// Bytes of it already in the FIFO.
    sent: u16,
}

type Edges<T> = Vec<(u32, T), EDGES>;

#[derive(Clone, Copy)]
struct GateEdge {
    level: bool,
    /// Switches as soon as it is due, whatever CV is still to be written ([`Service::gate_now`]).
    ignores_cv: bool,
}

/// Insert `(stamp, value)` after every edge that goes no later than it.
fn insert_edge<T>(edges: &mut Edges<T>, stamp: u32, value: T) -> bool {
    let at = edges
        .iter()
        .position(|(s, _)| !no_later(*s, stamp))
        .unwrap_or(edges.len());
    edges.insert(at, (stamp, value)).is_ok()
}

/// The queues. All-zero bytes is a valid empty service, so it can live in zeroed memory.
pub struct Service {
    din: Deque<DinEntry, DIN_ENTRIES>,
    din_bytes: Deque<u8, DIN_BYTES>,
    usb: Deque<(u32, [u8; 4]), USB_PACKETS>,
    cv: [Edges<u16>; NUM_CV],
    /// Each edge's level, and whether it switches without waiting for its channel's CV.
    gates: [Edges<GateEdge>; NUM_GATES],
    stats: Stats,
}

impl Service {
    pub const fn new() -> Self {
        Self {
            din: Deque::new(),
            din_bytes: Deque::new(),
            usb: Deque::new(),
            cv: [const { Vec::new() }; NUM_CV],
            gates: [const { Vec::new() }; NUM_GATES],
            stats: Stats {
                late: 0,
                dropped: 0,
                cv_late: 0,
            },
        }
    }

    /// Queue a whole message on `port` ([`DIN`]: raw bytes; [`USB`]: whole 4-byte packets).
    /// False, queuing nothing, if it doesn't all fit or isn't whole packets.
    pub fn push_midi(&mut self, port: usize, bytes: &[u8], stamp: u32) -> bool {
        let ok = match port {
            DIN => self.push_din(bytes, stamp),
            USB => self.push_usb(bytes, stamp),
            _ => false,
        };
        if !ok {
            self.stats.dropped += 1;
        }
        ok
    }

    fn push_din(&mut self, bytes: &[u8], stamp: u32) -> bool {
        let fits = !bytes.is_empty()
            && !self.din.is_full()
            && bytes.len() <= self.din_bytes.capacity() - self.din_bytes.len();
        if !fits {
            return false;
        }
        for &b in bytes {
            // Room checked above.
            let _ = self.din_bytes.push_back(b);
        }
        let _ = self.din.push_back(DinEntry {
            stamp,
            len: bytes.len() as u16,
            sent: 0,
        });
        true
    }

    fn push_usb(&mut self, bytes: &[u8], stamp: u32) -> bool {
        let (packets, rest) = bytes.as_chunks::<4>();
        if packets.is_empty()
            || !rest.is_empty()
            || packets.len() > self.usb.capacity() - self.usb.len()
        {
            return false;
        }
        for p in packets {
            let _ = self.usb.push_back((stamp, *p));
        }
        true
    }

    /// Bytes `port`'s queue can take now.
    pub fn midi_space(&self, port: usize) -> usize {
        match port {
            DIN if !self.din.is_full() => self.din_bytes.capacity() - self.din_bytes.len(),
            USB => (self.usb.capacity() - self.usb.len()) * 4,
            _ => 0,
        }
    }

    /// Queue CV `channel`'s word `value` for `stamp`. A word for [`STAMP_NOW`] is the newest
    /// decision for the channel, so it replaces the words still queued.
    pub fn push_cv(&mut self, channel: usize, value: u16, stamp: u32) -> bool {
        let ok = self.cv.get_mut(channel).is_some_and(|edges| {
            if stamp == STAMP_NOW {
                edges.clear();
            }
            insert_edge(edges, stamp, value)
        });
        if !ok {
            self.stats.dropped += 1;
        }
        ok
    }

    /// Queue gate `channel`'s edge to `level` at `stamp`, after the channel's CV for a stamp no
    /// later than it. An edge for [`STAMP_NOW`] is the newest decision for the channel, so it
    /// replaces the edges still queued.
    pub fn push_gate(&mut self, channel: usize, level: bool, stamp: u32) -> bool {
        self.push_gate_edge(
            channel,
            stamp,
            GateEdge {
                level,
                ignores_cv: false,
            },
        )
    }

    /// Switch gate `channel` to `level` as soon as possible, dropping its queued edges and not
    /// waiting for its CV: a gate going off at once.
    pub fn gate_now(&mut self, channel: usize, level: bool) -> bool {
        self.push_gate_edge(
            channel,
            STAMP_NOW,
            GateEdge {
                level,
                ignores_cv: true,
            },
        )
    }

    fn push_gate_edge(&mut self, channel: usize, stamp: u32, edge: GateEdge) -> bool {
        let ok = self.gates.get_mut(channel).is_some_and(|edges| {
            if stamp == STAMP_NOW {
                edges.clear();
            }
            insert_edge(edges, stamp, edge)
        });
        if !ok {
            self.stats.dropped += 1;
        }
        ok
    }

    pub fn stats(&self) -> Stats {
        self.stats
    }

    /// Send everything due at `now`. Returns when to fire again, on `now.micros`'s clock: the
    /// earliest event still queued, or [`RETRY_MICROS`] from now if something due couldn't go out;
    /// `None` when nothing is queued.
    pub fn fire(&mut self, config: &Config, now: Clock, out: &mut impl Outputs) -> Option<u64> {
        let tolerance = config.tolerance_micros;
        let mut blocked = false;
        let mut next = u64::MAX;

        for ch in 0..NUM_CV {
            while let Some(&(stamp, value)) = self.cv[ch].first() {
                let until = config.cv_until(stamp, now);
                if until > tolerance {
                    next = next.min(until);
                    break;
                }
                if !out.cv_write(ch as u8, value) {
                    blocked = true;
                    break;
                }
                if is_late(stamp, now.frame) {
                    self.stats.late += 1;
                    self.stats.cv_late += 1;
                }
                self.cv[ch].remove(0);
            }
        }

        for ch in 0..NUM_GATES {
            while let Some(&(stamp, edge)) = self.gates[ch].first() {
                let until = config.until(stamp, now);
                if until > tolerance {
                    next = next.min(until);
                    break;
                }
                // The gate's CV for this note, or an earlier one, hasn't reached the DAC.
                let cv_pending = !edge.ignores_cv
                    && self
                        .cv
                        .get(ch)
                        .and_then(|cv| cv.first())
                        .is_some_and(|&(cv_stamp, _)| no_later(cv_stamp, stamp));
                if cv_pending {
                    blocked = true;
                    break;
                }
                out.gate_set(ch as u8, edge.level);
                if is_late(stamp, now.frame) {
                    self.stats.late += 1;
                }
                self.gates[ch].remove(0);
            }
        }

        while let Some(entry) = self.din.front() {
            let (stamp, len, sent) = (entry.stamp, entry.len as usize, entry.sent as usize);
            let until = config.until(stamp, now);
            if sent == 0 && until > tolerance {
                next = next.min(until);
                break;
            }
            // The message's unsent bytes, at the front of the byte queue, in at most two pieces.
            let remaining = len - sent;
            let (a, b) = self.din_bytes.as_slices();
            let first = &a[..remaining.min(a.len())];
            let mut written = out.din_write(first);
            if written == first.len() && first.len() < remaining {
                written += out.din_write(&b[..remaining - first.len()]);
            }
            for _ in 0..written {
                self.din_bytes.pop_front();
            }
            if sent == 0 && written > 0 && is_late(stamp, now.frame) {
                self.stats.late += 1;
            }
            if written < remaining {
                if let Some(entry) = self.din.front_mut() {
                    entry.sent += written as u16;
                }
                blocked = true;
                break;
            }
            self.din.pop_front();
        }

        while let Some(&(stamp, packet)) = self.usb.front() {
            let until = config.until(stamp, now);
            if until > tolerance {
                next = next.min(until);
                break;
            }
            if !out.usb_send(packet) {
                blocked = true;
                break;
            }
            if is_late(stamp, now.frame) {
                self.stats.late += 1;
            }
            self.usb.pop_front();
        }

        if blocked {
            next = next.min(RETRY_MICROS);
        }
        (next != u64::MAX).then(|| now.micros + next)
    }

    /// When the earliest CV word still queued will be written, on `now.micros`'s clock.
    pub fn next_cv_write(&self, config: &Config, now: Clock) -> Option<u64> {
        self.cv
            .iter()
            .filter_map(|edges| edges.first())
            .map(|&(stamp, _)| now.micros + config.cv_until(stamp, now))
            .min()
    }
}
