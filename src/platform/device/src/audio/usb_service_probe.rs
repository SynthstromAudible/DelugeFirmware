//! How late the USB audio streams are serviced: what the USB interrupt's calls say about the packets
//! each stream missed, summed over an interval and reported as one `USB-AUDIO-TIMING key=value ...`
//! line.
//!
//! Pure, so the host tests cover it. `peripherals::usb_audio` feeds it under the `audio-timing`
//! feature; no shipping image enables that.

use core::fmt;

/// A high-speed microframe: each stream moves one packet in it.
pub const MICROFRAME_US: u32 = 125;
/// Both hardware buffers: how long a stream can go unserviced without losing a packet.
pub const BUFFERED_US: u32 = 250;

/// One stream's services over an interval.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct StreamGaps {
    /// Packets serviced.
    pub packets: u32,
    /// Audio frames they carried.
    pub frames: u32,
    /// Services more than [`BUFFERED_US`] after the one before.
    pub late: u32,
    /// Microframes those gaps left with no packet once both buffers had gone: an estimate, true of
    /// a high-speed link.
    pub missed: u32,
    /// The longest gap between two services, µs.
    pub max_gap_us: u32,
    /// When the last service was, unless the stream has (re)started since.
    last_us: Option<u32>,
}

impl StreamGaps {
    /// An interval with nothing recorded.
    pub const EMPTY: Self = Self {
        packets: 0,
        frames: 0,
        late: 0,
        missed: 0,
        max_gap_us: 0,
        last_us: None,
    };

    /// Records a service at `now_us` (a wrapping clock) of a packet carrying `frames`.
    pub fn record(&mut self, now_us: u32, frames: u32) {
        self.packets += 1;
        self.frames += frames;
        if let Some(last) = self.last_us.replace(now_us) {
            let gap = now_us.wrapping_sub(last);
            self.max_gap_us = self.max_gap_us.max(gap);
            if gap > BUFFERED_US {
                self.late += 1;
                self.missed += gap / MICROFRAME_US - BUFFERED_US / MICROFRAME_US;
            }
        }
    }

    /// The stream (re)starts: the time until its next service is not a gap.
    pub fn restart(&mut self) {
        self.last_us = None;
    }

    /// The interval's counts, starting the next one. The last service carries over.
    pub fn take(&mut self) -> Self {
        let taken = *self;
        *self = Self {
            last_us: self.last_us,
            ..Self::EMPTY
        };
        taken
    }
}

/// Both streams' services over an interval.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct UsbServiceStats {
    /// The capture stream: packets to the host.
    pub to_host: StreamGaps,
    /// The speaker stream: packets from the host.
    pub from_host: StreamGaps,
    /// Time in the USB interrupt, µs.
    pub interrupt_us: u32,
}

impl fmt::Display for UsbServiceStats {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("USB-AUDIO-TIMING")?;
        for (name, s) in [("in", &self.to_host), ("out", &self.from_host)] {
            write!(
                f,
                " {name}_packets={} {name}_frames={} {name}_late={} {name}_missed={} \
                 {name}_max_gap_us={}",
                s.packets, s.frames, s.late, s.missed, s.max_gap_us,
            )?;
        }
        write!(f, " interrupt_us={}", self.interrupt_us)
    }
}
