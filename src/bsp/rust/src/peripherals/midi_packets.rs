//! USB-MIDI event packets across the `midi_io.h` byte boundary.
//!
//! On a USB-MIDI port the app reads and writes whole 4-byte USB-MIDI 1.0 event packets
//! (`[cable << 4 | CIN, midi0, midi1, midi2]`), SysEx included, and the BSP passes them to and from
//! the USB class's packet queues unchanged. These helpers do the copying, generic over the queue,
//! so the device's `midi` module and the host tests share them.

/// Bytes in one USB-MIDI 1.0 event packet.
pub const PACKET_BYTES: usize = 4;

/// Copies whole packets from `next` into `dst` while another whole packet fits.
///
/// A packet is popped only when it fits, so none is ever split or lost. Returns the bytes written,
/// a multiple of [`PACKET_BYTES`].
pub fn read_packets(dst: &mut [u8], mut next: impl FnMut() -> Option<[u8; 4]>) -> usize {
    let mut written = 0;
    while dst.len() - written >= PACKET_BYTES {
        let Some(packet) = next() else {
            break;
        };
        dst[written..written + PACKET_BYTES].copy_from_slice(&packet);
        written += PACKET_BYTES;
    }
    written
}

/// Offers each whole packet of `src` to `send`, stopping at the first one refused (queue full).
///
/// A trailing partial packet is never sent. Returns the bytes accepted, a multiple of
/// [`PACKET_BYTES`].
pub fn write_packets(src: &[u8], mut send: impl FnMut([u8; 4]) -> bool) -> usize {
    let mut accepted = 0;
    for packet in src.as_chunks::<PACKET_BYTES>().0 {
        if !send(*packet) {
            break;
        }
        accepted += PACKET_BYTES;
    }
    accepted
}
