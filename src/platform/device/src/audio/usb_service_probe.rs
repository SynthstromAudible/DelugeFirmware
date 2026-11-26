//! What the USB audio streams did over an interval, as one `USB-AUDIO-TIMING key=value ...` line: what was queued for the
//! host and how often its DMA ran dry, what came from it, and the USB interrupt's time.
//!
//! Pure, so the host tests cover it. `peripherals::usb_audio` fills it under the `audio-timing` feature; no shipping image
//! enables that.

use core::fmt;

/// Both streams over an interval.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct UsbDmaStats {
    /// Packets and frames queued for the host.
    pub in_packets: u32,
    pub in_frames: u32,
    /// Times the DMA to the host ran dry and was started again.
    pub in_stops: u32,
    /// The fewest and most packets queued ahead of the DMA when a packet was added, if any was.
    pub in_ahead: Option<(u32, u32)>,
    /// Frames that came from the host.
    pub out_frames: u32,
    /// Time in the USB interrupt, µs.
    pub interrupt_us: u32,
}

impl UsbDmaStats {
    /// An interval with nothing in it.
    pub const EMPTY: Self = Self {
        in_packets: 0,
        in_frames: 0,
        in_stops: 0,
        in_ahead: None,
        out_frames: 0,
        interrupt_us: 0,
    };
}

impl fmt::Display for UsbDmaStats {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "USB-AUDIO-TIMING in_packets={} in_frames={} in_stops={}",
            self.in_packets, self.in_frames, self.in_stops
        )?;
        if let Some((min, max)) = self.in_ahead {
            write!(f, " in_ahead_min={min} in_ahead_max={max}")?;
        }
        write!(
            f,
            " out_frames={} interrupt_us={}",
            self.out_frames, self.interrupt_us
        )
    }
}
