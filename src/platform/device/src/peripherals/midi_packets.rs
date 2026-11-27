//! USB-MIDI event packets across the `midi_io.h` byte boundary.
//!
//! On a USB-MIDI port the app reads and writes whole 4-byte USB-MIDI 1.0 event packets
//! (`[cable << 4 | CIN, midi0, midi1, midi2]`), SysEx included, and the BSP passes them to and from
//! the USB class's packet queues unchanged. These helpers do the copying, generic over the queue,
//! so the device's `midi` module and the host tests share them.

/// Bytes in one USB-MIDI 1.0 event packet.
pub const PACKET_BYTES: usize = 4;

/// A USB-MIDI packet received from the host and the Embassy tick its transfer's read completed at.
pub type StampedPacket = ([u8; 4], u64);

/// Reads received packets one transfer at a time: every packet of a transfer carries its read's
/// tick, so a read that stops at the next transfer gives one stamp exact for all it returns.
pub struct TransferReader {
    /// The first packet of the next transfer, taken from the queue to find the boundary.
    held: Option<StampedPacket>,
}

impl TransferReader {
    pub const fn new() -> Self {
        Self { held: None }
    }

    /// Copies whole packets of the oldest transfer from `next` into `dst` while another whole
    /// packet fits. Returns the bytes written (a multiple of [`PACKET_BYTES`]) and the transfer's
    /// tick, or `None` when nothing was written.
    ///
    /// A packet is taken only when it fits, so none is ever split or lost; the first packet of a
    /// later transfer is held for the next read.
    pub fn read(
        &mut self,
        dst: &mut [u8],
        mut next: impl FnMut() -> Option<StampedPacket>,
    ) -> Option<(usize, u64)> {
        let mut written = 0;
        let mut transfer = None;
        while dst.len() - written >= PACKET_BYTES {
            let Some((packet, tick)) = self.held.take().or_else(&mut next) else {
                break;
            };
            match transfer {
                Some(first) if first != tick => {
                    self.held = Some((packet, tick));
                    break;
                }
                _ => transfer = Some(tick),
            }
            dst[written..written + PACKET_BYTES].copy_from_slice(&packet);
            written += PACKET_BYTES;
        }
        transfer.map(|tick| (written, tick))
    }
}

impl Default for TransferReader {
    fn default() -> Self {
        Self::new()
    }
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
