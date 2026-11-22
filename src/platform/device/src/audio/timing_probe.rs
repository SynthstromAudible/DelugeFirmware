//! The audio-timing probe: what each render cost against the queue it had, summed over an interval
//! of audio and reported as one `AUDIO-TIMING key=value ...` line.
//!
//! Pure, so the host tests cover it. The device drive feeds it under the `audio-timing` feature;
//! no shipping image enables that.

use core::fmt;

/// Render-time buckets: bucket 0 holds 0 µs, bucket i holds [2^(i-1), 2^i) µs, the last everything above.
pub const US_BUCKETS: usize = 14;
/// Render-size buckets, in frames, laid out as [`US_BUCKETS`].
pub const FRAME_BUCKETS: usize = 9;
/// The frames an interval covers: two seconds at 44.1 kHz.
pub const INTERVAL_FRAMES: u64 = 88_200;

/// The log2 bucket of `v` among `n` buckets: 0 for 0, else its bit width, capped at n − 1.
pub fn log2_bucket(v: u32, n: usize) -> usize {
    let width = (u32::BITS - v.leading_zeros()) as usize;
    width.min(n - 1)
}

/// One interval's statistics.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct TimingStats {
    /// Renders recorded.
    pub renders: u32,
    /// Frames rendered.
    pub frames: u64,
    /// Time spent rendering, µs.
    pub us: u64,
    /// The sum of each render's frames squared, for a fit of time against size.
    pub frames_sq: u64,
    /// The sum of each render's frames times its µs, for the same fit.
    pub frames_us: u64,
    /// The longest render, µs.
    pub max_us: u32,
    /// The longest render's frames.
    pub max_us_frames: u32,
    /// The fewest frames queued for the codec when a render began.
    pub min_queued: u32,
    /// The fewest frames still queued when a render finished (negative: the codec ran past the write cursor).
    pub min_slack: i32,
    /// Renders that finished after the codec had played everything queued when they began.
    pub overruns: u32,
    /// The latest the render task woke: after its timer's deadline (timer cadence), or after one
    /// block past its previous wake (block signal), µs.
    pub max_wake_late_us: u32,
    /// Blocks the codec reached before the drive wrote them, as the drive counts them (lapped
    /// stalls included).
    pub drive_overruns: u32,
    /// Renders by time.
    pub us_hist: [u32; US_BUCKETS],
    /// Renders by size.
    pub frames_hist: [u32; FRAME_BUCKETS],
}

impl TimingStats {
    /// An interval with nothing recorded.
    pub const EMPTY: Self = Self {
        renders: 0,
        frames: 0,
        us: 0,
        frames_sq: 0,
        frames_us: 0,
        max_us: 0,
        max_us_frames: 0,
        min_queued: u32::MAX,
        min_slack: i32::MAX,
        overruns: 0,
        max_wake_late_us: 0,
        drive_overruns: 0,
        us_hist: [0; US_BUCKETS],
        frames_hist: [0; FRAME_BUCKETS],
    };

    /// Records one render: its `frames`, the frames `queued` for the codec when it began, its time
    /// in `us`, and the frames the codec `played` while it ran.
    pub fn record_render(&mut self, frames: u32, queued: u32, us: u32, played: u32) {
        self.renders += 1;
        self.frames += u64::from(frames);
        self.us += u64::from(us);
        self.frames_sq += u64::from(frames) * u64::from(frames);
        self.frames_us += u64::from(frames) * u64::from(us);
        if us > self.max_us {
            self.max_us = us;
            self.max_us_frames = frames;
        }
        self.min_queued = self.min_queued.min(queued);
        let slack = queued as i32 - played as i32;
        self.min_slack = self.min_slack.min(slack);
        if played >= queued {
            self.overruns += 1;
        }
        self.us_hist[log2_bucket(us, US_BUCKETS)] += 1;
        self.frames_hist[log2_bucket(frames, FRAME_BUCKETS)] += 1;
    }

    /// Records how late the render task woke after its timer's deadline.
    pub fn record_wake_late(&mut self, late_us: u32) {
        self.max_wake_late_us = self.max_wake_late_us.max(late_us);
    }

    /// Adds the overruns the drive counted since `seen`, its count at the last note (its count
    /// wraps), and moves `seen` to `total`.
    pub fn note_drive_overruns(&mut self, total: u32, seen: &mut u32) {
        self.drive_overruns += total.wrapping_sub(*seen);
        *seen = total;
    }

    /// Whether the interval has covered [`INTERVAL_FRAMES`].
    pub fn interval_done(&self) -> bool {
        self.frames >= INTERVAL_FRAMES
    }
}

/// Writes `values` comma-separated, with no spaces, so the line stays `key=value` words.
fn write_list(f: &mut fmt::Formatter<'_>, values: &[u32]) -> fmt::Result {
    for (i, v) in values.iter().enumerate() {
        if i != 0 {
            f.write_str(",")?;
        }
        write!(f, "{v}")?;
    }
    Ok(())
}

impl fmt::Display for TimingStats {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "AUDIO-TIMING renders={} frames={} us={} f2={} fus={} max_us={} max_us_frames={} min_queued={} \
             min_slack={} overruns={} max_wake_late_us={} drive_overruns={} us_hist=",
            self.renders,
            self.frames,
            self.us,
            self.frames_sq,
            self.frames_us,
            self.max_us,
            self.max_us_frames,
            self.min_queued,
            self.min_slack,
            self.overruns,
            self.max_wake_late_us,
            self.drive_overruns,
        )?;
        write_list(f, &self.us_hist)?;
        f.write_str(" frames_hist=")?;
        write_list(f, &self.frames_hist)
    }
}
