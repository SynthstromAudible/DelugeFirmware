//! Tests the USB-MIDI port's packet reading: whole 4-byte event packets, SysEx included, cross the
//! `midi_io.h` boundary byte-identically.
//!
//! The device's `deluge_midi_read_timed` is this reader over the USB class's packet queue; a bounded
//! `VecDeque` stands in for it here. A read returns one transfer's packets, so the one stamp it
//! gives is exact for all of them.

use std::collections::VecDeque;

use deluge_platform_device::peripherals::midi_packets::TransferReader;

/// A note-on on cable 0, channel 1.
const NOTE_ON: [u8; 4] = [0x09, 0x90, 0x3C, 0x64];
/// `F0 00 21 7B 01 02 03 F7` on cable 1: two SysEx start/continue packets and a 2-byte end.
const SYSEX: [[u8; 4]; 3] = [
    [0x14, 0xF0, 0x00, 0x21],
    [0x14, 0x7B, 0x01, 0x02],
    [0x16, 0x03, 0xF7, 0x00],
];

/// The tick every packet of [`received`]'s one transfer carries.
const TICK: u32 = 7_000;

/// A bounded packet queue, as the USB class keeps: each packet with its transfer's tick.
struct Queue {
    packets: VecDeque<([u8; 4], u32)>,
    capacity: usize,
}

impl Queue {
    fn new(capacity: usize) -> Self {
        Self {
            packets: VecDeque::new(),
            capacity,
        }
    }

    fn push(&mut self, packet: [u8; 4]) -> bool {
        if self.packets.len() == self.capacity {
            return false;
        }
        self.packets.push_back((packet, TICK));
        true
    }
}

fn received() -> Queue {
    let mut q = Queue::new(32);
    for p in [NOTE_ON].iter().chain(&SYSEX) {
        assert!(q.push(*p));
    }
    q
}

fn concat(packets: &[[u8; 4]]) -> Vec<u8> {
    packets.iter().flatten().copied().collect()
}

#[test]
fn a_read_returns_a_note_on_and_a_sysex_byte_identically() {
    let mut rx = received();
    let mut dst = [0u8; 64];
    let read = TransferReader::new().read(&mut dst, || rx.packets.pop_front());
    assert_eq!(read, Some((16, TICK)));
    assert_eq!(dst[..16], concat(&[NOTE_ON, SYSEX[0], SYSEX[1], SYSEX[2]]));
    assert!(rx.packets.is_empty());
}

#[test]
fn a_read_never_splits_a_packet_or_drops_one_that_does_not_fit() {
    let mut rx = received();
    // Room for two packets and three bytes: two packets come out, the rest stay queued.
    let mut reader = TransferReader::new();
    let mut dst = [0u8; 11];
    assert_eq!(
        reader.read(&mut dst, || rx.packets.pop_front()),
        Some((8, TICK))
    );
    assert_eq!(dst[..8], concat(&[NOTE_ON, SYSEX[0]]));
    assert_eq!(rx.packets, [(SYSEX[1], TICK), (SYSEX[2], TICK)]);

    // Less than one packet of room reads nothing.
    let mut dst = [0u8; 3];
    assert_eq!(reader.read(&mut dst, || rx.packets.pop_front()), None);
    assert_eq!(rx.packets.len(), 2);
}

#[test]
fn a_read_stops_at_the_next_transfer_and_keeps_its_first_packet_for_the_next_read() {
    let mut rx: VecDeque<([u8; 4], u32)> =
        VecDeque::from([(NOTE_ON, 10), (SYSEX[0], 10), (SYSEX[1], 20)]);
    let mut reader = TransferReader::new();
    let mut dst = [0u8; 64];
    assert_eq!(reader.read(&mut dst, || rx.pop_front()), Some((8, 10)));
    assert_eq!(dst[..8], concat(&[NOTE_ON, SYSEX[0]]));
    assert_eq!(reader.read(&mut dst, || rx.pop_front()), Some((4, 20)));
    assert_eq!(dst[..4], SYSEX[1]);
    assert_eq!(reader.read(&mut dst, || rx.pop_front()), None);
}

#[test]
fn a_held_packet_is_read_even_when_its_transfer_is_the_last() {
    let mut rx: VecDeque<([u8; 4], u32)> = VecDeque::from([(NOTE_ON, 10), (SYSEX[0], 20)]);
    let mut reader = TransferReader::new();
    let mut dst = [0u8; 64];
    assert_eq!(reader.read(&mut dst, || rx.pop_front()), Some((4, 10)));
    assert!(
        rx.is_empty(),
        "the next transfer's first packet is held, not lost"
    );
    assert_eq!(reader.read(&mut dst, || rx.pop_front()), Some((4, 20)));
    assert_eq!(dst[..4], SYSEX[0]);
}
