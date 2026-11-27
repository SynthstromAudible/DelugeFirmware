//! Tests the timed output core: MIDI, CV and gate events delivered at play-timeline stamps.

use deluge_platform_device::timed_output::core::*;

/// A CV lead of about nine frames, long enough to see the word go before its gate.
const CFG: Config = Config {
    max_ahead_frames: 640,
    cv_lead_micros: 200,
    tolerance_micros: 22,
};

/// Hardware that records what the service sent, with room and a bus the test controls.
struct Rec {
    din: Vec<u8>,
    din_room: usize,
    usb: Vec<[u8; 4]>,
    usb_room: usize,
    log: Vec<String>,
    bus_busy: bool,
}

impl Rec {
    fn new() -> Self {
        Self {
            din: Vec::new(),
            din_room: usize::MAX,
            usb: Vec::new(),
            usb_room: usize::MAX,
            log: Vec::new(),
            bus_busy: false,
        }
    }
}

impl Outputs for Rec {
    fn din_write(&mut self, bytes: &[u8]) -> usize {
        let n = bytes.len().min(self.din_room);
        self.din.extend_from_slice(&bytes[..n]);
        self.din_room -= n;
        n
    }
    fn usb_send(&mut self, p: [u8; 4]) -> bool {
        if self.usb_room == 0 {
            return false;
        }
        self.usb_room -= 1;
        self.usb.push(p);
        true
    }
    fn gate_set(&mut self, ch: u8, level: bool) {
        self.log.push(format!("gate{ch}={}", level as u8));
    }
    fn cv_write(&mut self, ch: u8, v: u16) -> bool {
        if self.bus_busy {
            return false;
        }
        self.log.push(format!("cv{ch}={v}"));
        true
    }
}

fn at(frame: u32, micros: u64) -> Clock {
    Clock { frame, micros }
}

#[test]
fn frames_until_counts_ahead_and_reads_past_now_and_stale_as_due() {
    assert_eq!(frames_until(1100, 1000, 640), 100);
    assert_eq!(frames_until(900, 1000, 640), 0);
    assert_eq!(frames_until(STAMP_NOW, 1000, 640), 0);
    assert_eq!(frames_until(2000, 1000, 640), 0, "stale");
    assert_eq!(frames_until(10, u32::MAX - 9, 640), 20, "across the wrap");
}

#[test]
fn frames_convert_to_micros_without_division() {
    assert_eq!(micros_for_frames(44_100), 1_000_000);
    assert_eq!(micros_for_frames(441), 10_000);
    assert_eq!(micros_for_frames(1), 22);
}

#[test]
fn midi_waits_for_its_stamp_then_goes_out_in_order() {
    let mut s = Service::new();
    let mut r = Rec::new();
    assert!(s.push_midi(DIN, &[0x90, 60, 100], 1441));
    assert!(s.push_midi(DIN, &[0xF8], 1441));
    // 441 frames ahead: due in 10 ms.
    assert_eq!(s.fire(&CFG, at(1000, 0), &mut r), Some(10_000));
    assert!(r.din.is_empty());
    assert_eq!(s.fire(&CFG, at(1441, 10_000), &mut r), None);
    assert_eq!(r.din, [0x90, 60, 100, 0xF8]);
}

#[test]
fn a_message_not_yet_due_holds_back_the_ones_behind_it() {
    let mut s = Service::new();
    let mut r = Rec::new();
    s.push_midi(DIN, &[0x90, 60, 100], 1441);
    s.push_midi(DIN, &[0xF8], STAMP_NOW);
    s.fire(&CFG, at(1000, 0), &mut r);
    assert!(r.din.is_empty());
}

#[test]
fn din_short_of_room_sends_what_fits_and_retries() {
    let mut s = Service::new();
    let mut r = Rec::new();
    r.din_room = 2;
    s.push_midi(DIN, &[0x90, 60, 100], STAMP_NOW);
    assert_eq!(
        s.fire(&CFG, at(1000, 5_000), &mut r),
        Some(5_000 + RETRY_MICROS)
    );
    assert_eq!(r.din, [0x90, 60]);
    r.din_room = 16;
    assert_eq!(s.fire(&CFG, at(1005, 5_100), &mut r), None);
    assert_eq!(r.din, [0x90, 60, 100]);
}

#[test]
fn a_message_split_across_the_byte_ring_goes_out_whole() {
    let mut s = Service::new();
    let mut r = Rec::new();
    // Walk the byte ring's start to near its end, so the next message wraps.
    for _ in 0..340 {
        s.push_midi(DIN, &[0x90, 60, 100], STAMP_NOW);
        s.fire(&CFG, at(0, 0), &mut r);
    }
    r.din.clear();
    s.push_midi(DIN, &[0xB0, 7, 99], STAMP_NOW);
    s.push_midi(DIN, &[0xB0, 8, 98], STAMP_NOW);
    s.fire(&CFG, at(0, 0), &mut r);
    assert_eq!(r.din, [0xB0, 7, 99, 0xB0, 8, 98]);
}

#[test]
fn usb_sends_whole_packets_until_the_queue_refuses() {
    let mut s = Service::new();
    let mut r = Rec::new();
    r.usb_room = 1;
    assert!(s.push_midi(USB, &[0x09, 0x90, 60, 100, 0x08, 0x80, 60, 0], STAMP_NOW));
    assert_eq!(s.fire(&CFG, at(0, 0), &mut r), Some(RETRY_MICROS));
    assert_eq!(r.usb, [[0x09, 0x90, 60, 100]]);
    r.usb_room = 4;
    s.fire(&CFG, at(5, 100), &mut r);
    assert_eq!(r.usb.len(), 2);
}

#[test]
fn usb_refuses_anything_but_whole_packets() {
    let mut s = Service::new();
    assert!(!s.push_midi(USB, &[0x09, 0x90, 60], STAMP_NOW));
    assert_eq!(s.stats().dropped, 1);
}

#[test]
fn cv_goes_out_its_lead_before_the_stamp_and_its_gate_at_the_stamp() {
    let mut s = Service::new();
    let mut r = Rec::new();
    s.push_cv(0, 1234, 1441);
    s.push_gate(0, true, 1441);
    // The first wake is for the CV, its lead before the stamp's 10 ms.
    assert_eq!(s.fire(&CFG, at(1000, 0), &mut r), Some(9_800));
    // Nine frames (204 µs) before the stamp: the CV goes, the gate waits for its stamp.
    assert_eq!(s.fire(&CFG, at(1432, 9_800), &mut r), Some(9_800 + 204));
    assert_eq!(r.log, ["cv0=1234"]);
    assert_eq!(s.fire(&CFG, at(1441, 10_004), &mut r), None);
    assert_eq!(r.log, ["cv0=1234", "gate0=1"]);
}

#[test]
fn a_gate_waits_for_its_cv_when_the_bus_is_busy() {
    let mut s = Service::new();
    let mut r = Rec::new();
    r.bus_busy = true;
    s.push_cv(1, 7, STAMP_NOW);
    s.push_gate(1, true, STAMP_NOW);
    assert_eq!(s.fire(&CFG, at(0, 0), &mut r), Some(RETRY_MICROS));
    assert!(r.log.is_empty());
    r.bus_busy = false;
    s.fire(&CFG, at(5, 100), &mut r);
    assert_eq!(r.log, ["cv1=7", "gate1=1"]);
    assert_eq!(s.stats().late, 0, "as soon as possible is never late");
}

#[test]
fn a_gate_without_a_cv_channel_never_waits() {
    let mut s = Service::new();
    let mut r = Rec::new();
    r.bus_busy = true;
    s.push_cv(0, 7, STAMP_NOW);
    s.push_gate(3, true, STAMP_NOW);
    s.fire(&CFG, at(0, 0), &mut r);
    assert_eq!(r.log, ["gate3=1"]);
}

#[test]
fn a_gate_does_not_wait_for_a_later_note_s_cv() {
    let mut s = Service::new();
    let mut r = Rec::new();
    s.push_gate(0, false, 1000);
    s.push_cv(0, 9, 1400);
    s.fire(&CFG, at(1000, 0), &mut r);
    assert_eq!(r.log, ["gate0=0"]);
}

#[test]
fn gate_edges_sort_by_stamp_and_equal_stamps_keep_their_order() {
    let mut s = Service::new();
    let mut r = Rec::new();
    s.push_gate(2, true, 1100);
    s.push_gate(2, false, 1050);
    s.push_gate(2, true, 1050);
    s.fire(&CFG, at(1100, 10_000), &mut r);
    assert_eq!(r.log, ["gate2=0", "gate2=1", "gate2=1"]);
}

#[test]
fn an_as_soon_as_possible_edge_replaces_the_channel_s_queued_edges() {
    let mut s = Service::new();
    let mut r = Rec::new();
    // A clock pulse the render queued, then Stop between renders: the off is the last word.
    s.push_gate(2, true, 1100);
    s.push_gate(3, true, 1100);
    s.push_gate(2, false, STAMP_NOW);
    s.fire(&CFG, at(1100, 0), &mut r);
    assert_eq!(r.log, ["gate2=0", "gate3=1"]);
}

#[test]
fn an_as_soon_as_possible_cv_word_replaces_the_channel_s_queued_words() {
    let mut s = Service::new();
    let mut r = Rec::new();
    // A note's voltage the render queued, then a pitch bend between renders: the bend's value
    // stands, and the note's gate still waits for no older word.
    s.push_cv(0, 100, 1441);
    s.push_gate(0, true, 1441);
    s.push_cv(0, 105, STAMP_NOW);
    s.fire(&CFG, at(1441, 10_000), &mut r);
    assert_eq!(r.log, ["cv0=105", "gate0=1"]);
}

#[test]
fn an_as_soon_as_possible_note_waits_for_its_cv_but_a_gate_switched_now_does_not() {
    let mut s = Service::new();
    let mut r = Rec::new();
    r.bus_busy = true;
    s.push_cv(0, 3, STAMP_NOW);
    s.push_gate(0, true, STAMP_NOW);
    s.push_cv(1, 4, STAMP_NOW);
    assert!(s.gate_now(1, false));
    s.fire(&CFG, at(0, 0), &mut r);
    assert_eq!(r.log, ["gate1=0"]);
}

#[test]
fn a_gate_switched_now_drops_its_queued_edges_only() {
    let mut s = Service::new();
    let mut r = Rec::new();
    s.push_gate(0, true, 1441);
    s.push_gate(1, true, 1441);
    s.gate_now(0, false);
    s.fire(&CFG, at(1000, 0), &mut r);
    assert_eq!(r.log, ["gate0=0"]);
    s.fire(&CFG, at(1441, 10_000), &mut r);
    assert_eq!(r.log, ["gate0=0", "gate1=1"]);
}

/// The device's timing: a CV lead of 50 µs against a tolerance of about a sample.
const DEVICE: Config = Config {
    max_ahead_frames: 640,
    cv_lead_micros: 50,
    tolerance_micros: 22,
};

#[test]
fn with_the_device_s_timing_a_note_s_cv_goes_out_before_its_gate_s_fire() {
    let mut s = Service::new();
    let mut r = Rec::new();
    s.push_cv(0, 7, 1441);
    s.push_gate(0, true, 1441);
    // Three frames (68 µs) before the stamp: within the lead and tolerance for the CV, not for the
    // gate.
    let next = s.fire(&DEVICE, at(1438, 0), &mut r);
    assert_eq!(r.log, ["cv0=7"]);
    assert_eq!(next, Some(68));
    s.fire(&DEVICE, at(1441, 68), &mut r);
    assert_eq!(r.log, ["cv0=7", "gate0=1"]);
}

#[test]
fn a_full_queue_refuses_and_counts_the_drop() {
    let mut s = Service::new();
    for _ in 0..16 {
        assert!(s.push_gate(0, true, 1441));
    }
    assert!(!s.push_gate(0, true, 1441));
    assert!(!s.push_cv(5, 0, 1441), "no such channel");
    assert_eq!(s.stats().dropped, 2);
}

#[test]
fn midi_space_counts_bytes_left_on_din_and_packet_bytes_on_usb() {
    let mut s = Service::new();
    assert_eq!(s.midi_space(DIN), 1024);
    s.push_midi(DIN, &[0x90, 60, 100], 1441);
    assert_eq!(s.midi_space(DIN), 1021);
    assert_eq!(s.midi_space(USB), 1024 * 4);
    assert_eq!(s.midi_space(2), 0);
}

#[test]
fn a_message_larger_than_its_queue_is_refused_whole() {
    let mut s = Service::new();
    assert!(!s.push_midi(DIN, &[0u8; 1025], STAMP_NOW));
    assert_eq!(s.midi_space(DIN), 1024);
}

#[test]
fn an_event_sent_more_than_a_sample_after_its_stamp_counts_as_late() {
    let mut s = Service::new();
    let mut r = Rec::new();
    s.push_gate(3, true, 1000);
    s.push_cv(0, 1, 1000);
    s.fire(&CFG, at(1010, 0), &mut r);
    assert_eq!(r.log, ["cv0=1", "gate3=1"]);
    assert_eq!(s.stats().late, 2);
    assert_eq!(s.stats().cv_late, 1);
}

#[test]
fn next_cv_write_is_the_earliest_word_less_its_lead() {
    let mut s = Service::new();
    let mut r = Rec::new();
    assert_eq!(s.next_cv_write(&CFG, at(1000, 0)), None);
    s.push_cv(1, 5, 1441);
    s.push_cv(0, 5, 1220);
    assert_eq!(
        s.next_cv_write(&CFG, at(1000, 0)),
        Some(micros_for_frames(220) - 200)
    );
    s.fire(&CFG, at(1441, 10_000), &mut r);
    assert_eq!(
        s.next_cv_write(&CFG, at(1441, 10_000)),
        None,
        "written words reserve nothing"
    );
}
