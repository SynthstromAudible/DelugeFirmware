//! The audio tier's render driver: calls the app's `deluge_app_audio_tick` on a fixed poll cadence,
//! and keeps the running average of how long a tick that rendered took.
//!
//! The tick renders only as far as the output DMA has played (`deluge_audio_drive`), so the cadence
//! is a poll rate, not a block size.

use core::sync::atomic::{AtomicU64, Ordering};

use embassy_time::Duration;
#[cfg(has_app)]
use embassy_time::{Instant, Timer};

/// The wait before each tick: 64 frames at 44.1 kHz, in µs.
const PERIOD_US: u64 = 64 * 1_000_000 / 44_100;
/// The pause after each tick: 8 frames at 44.1 kHz, in µs.
const BACKOFF_US: u64 = 8 * 1_000_000 / 44_100;

/// Running average of a rendering tick's duration, in µs: `avg = (avg + sample) / 2` over the ticks
/// that rendered at least one block.
static AVG_TICK_US: AtomicU64 = AtomicU64::new(0);

/// Records one rendering tick's duration into the average.
pub fn record_tick_us(us: u64) {
    let avg = AVG_TICK_US.load(Ordering::Relaxed);
    AVG_TICK_US.store((avg + us) / 2, Ordering::Relaxed);
}

/// The running average of a rendering tick's duration, in µs.
pub fn average_tick_us() -> u64 {
    AVG_TICK_US.load(Ordering::Relaxed)
}

/// Zeroes the average, so a host test starts from a known state.
#[cfg(not(target_os = "none"))]
pub fn reset_average_for_test() {
    AVG_TICK_US.store(0, Ordering::Relaxed);
}

/// The tick cadence override, in µs, for a harness that needs one block per period.
///
/// On the device the tick polls the DMA play head. The host render pump renders a block on every
/// call, so a harness racing playback against modelled storage time (golden_vt_render's
/// `render_song`) sets the real block period instead. Zero period means no override.
#[cfg(not(target_os = "none"))]
static PERIOD_OVERRIDE_US: AtomicU64 = AtomicU64::new(0);
/// The pause-after-tick half of the cadence override, in µs; read only with a nonzero
/// [`PERIOD_OVERRIDE_US`].
#[cfg(not(target_os = "none"))]
static BACKOFF_OVERRIDE_US: AtomicU64 = AtomicU64::new(0);

/// Sets the tick cadence override. Call before spawning [`render_task`].
#[cfg(not(target_os = "none"))]
pub fn set_period_override_us(period_us: u64, backoff_us: u64) {
    PERIOD_OVERRIDE_US.store(period_us, Ordering::Relaxed);
    BACKOFF_OVERRIDE_US.store(backoff_us, Ordering::Relaxed);
}

/// The tick cadence: (wait before, pause after).
fn cadence() -> (Duration, Duration) {
    #[cfg(not(target_os = "none"))]
    {
        let period = PERIOD_OVERRIDE_US.load(Ordering::Relaxed);
        if period != 0 {
            return (
                Duration::from_micros(period),
                Duration::from_micros(BACKOFF_OVERRIDE_US.load(Ordering::Relaxed)),
            );
        }
    }
    (
        Duration::from_micros(PERIOD_US),
        Duration::from_micros(BACKOFF_US),
    )
}

/// Drives the render: waits, ticks, pauses, forever. It holds off until boot has finished, so
/// the first tick follows `deluge_app_init`.
///
/// Spawn it once, on the audio executor (the device's `AUDIO_EXEC`, the host's `deluge-audio`
/// thread), or on a single-executor harness's one executor.
#[cfg(has_app)]
#[embassy_executor::task]
pub async fn render_task() {
    let (period, backoff) = cadence();
    while !crate::boot::finished() {
        Timer::after(period).await;
    }
    loop {
        Timer::after(period).await;
        let start = Instant::now();
        // SAFETY: the app's render tick, called only from this task, on the audio tier.
        let rendered = unsafe { crate::sys::deluge_app_audio_tick() };
        if rendered {
            record_tick_us((Instant::now() - start).as_micros());
        }
        Timer::after(backoff).await;
    }
}

/// The render's average tick time, in seconds; the C-ABI form of [`average_tick_us`].
#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_render_time_average() -> f64 {
    average_tick_us() as f64 / 1_000_000.0
}

abi_pin!(deluge_audio_render_time_average);
