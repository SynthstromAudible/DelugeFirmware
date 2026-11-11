//! The audio tier's render driver: calls the app's `deluge_app_audio_tick` each time the codec's
//! transmit ring completes a block (device) or on a fixed cadence (host), and keeps the running
//! average of how long a tick that rendered took.
//!
//! On the device the tick renders the blocks the drive plans (`deluge_audio_drive`): the block
//! signal paces it, and the drive's own position check decides what it renders, so a wake that
//! merges several completions still catches up.

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

/// Sets the tick cadence override. Call before spawning `render_task`.
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

/// Drives the render: waits, ticks, forever. It holds off until boot has finished, so the first
/// tick follows `deluge_app_init`.
///
/// On the device it waits for each transmit block to complete; on the host (and on a device
/// before board audio, which never has the signal) it waits, ticks and pauses on [`cadence`].
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
    #[cfg(target_os = "none")]
    match deluge_bsp::audio::tx_block_signal() {
        Some(signal) => block_signal_loop(signal).await,
        None => log::warn!("audio: no transmit block signal; the render ticks on a timer"),
    }
    loop {
        let deadline = Instant::now() + period;
        Timer::at(deadline).await;
        #[cfg(all(feature = "audio-timing", target_os = "none"))]
        crate::audio::device::note_wake_late((Instant::now() - deadline).as_micros() as u32);
        let start = Instant::now();
        // SAFETY: the app's render tick, called only from this task, on the audio tier.
        let rendered = unsafe { crate::sys::deluge_app_audio_tick() };
        if rendered {
            record_tick_us((Instant::now() - start).as_micros());
        }
        Timer::after(backoff).await;
    }
}

/// Ticks once per completed transmit block, forever. The signal latches, so a block that completes
/// during a tick wakes the next wait at once.
#[cfg(all(has_app, target_os = "none"))]
async fn block_signal_loop(signal: rza1_hal::ring_buffer::BlockSignal) -> ! {
    #[cfg(feature = "audio-timing")]
    let mut last_wake = Instant::now();
    loop {
        signal.wait().await;
        let start = Instant::now();
        #[cfg(feature = "audio-timing")]
        {
            // How much longer than one block this wake came after the last.
            const BLOCK_US: u64 = 2902;
            let gap = (start - last_wake).as_micros();
            crate::audio::device::note_wake_late(gap.saturating_sub(BLOCK_US) as u32);
            last_wake = start;
        }
        // SAFETY: the app's render tick, called only from this task, on the audio tier.
        let rendered = unsafe { crate::sys::deluge_app_audio_tick() };
        if rendered {
            record_tick_us((Instant::now() - start).as_micros());
        }
    }
}

/// The render's average tick time, in seconds; the C-ABI form of [`average_tick_us`].
#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_render_time_average() -> f64 {
    average_tick_us() as f64 / 1_000_000.0
}

abi_pin!(deluge_audio_render_time_average);
