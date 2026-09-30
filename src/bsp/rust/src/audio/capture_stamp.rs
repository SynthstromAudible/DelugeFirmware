//! Capture stamps: where on the TX play timeline an input event happened.
//!
//! A capture stamp is the TX ring's play frame at the moment of the event, in frames at the codec
//! rate, modulo the ring. Every input the app times (a MIDI byte, a trigger-clock edge) is stamped
//! on this one timeline, so `deluge_audio_stamp_to_render_offset` can place it in the next render.
//! Each event is late by the same block, and the spacing between events is exact.

/// Returns the stamp for an event that happened at `event_ticks`, read when the play position is
/// `play_now` and the Embassy clock reads `now_ticks`. Ticks are at `tick_hz`, frames at
/// `sample_rate`.
///
/// An event from the future (a clock read out of order) counts as now. An event more than
/// `max_frames` old clamps to `max_frames` back, because the ring cannot place anything older.
pub fn stamp_at(
    play_now: u32,
    now_ticks: u64,
    event_ticks: u64,
    tick_hz: u64,
    sample_rate: u32,
    max_frames: u32,
) -> u32 {
    let elapsed_ticks = now_ticks.saturating_sub(event_ticks);
    let elapsed_frames = elapsed_ticks.saturating_mul(u64::from(sample_rate)) / tick_hz;
    let back = elapsed_frames.min(u64::from(max_frames)) as u32;
    play_now.wrapping_sub(back)
}

/// Returns how far past `block_start_play` (the play frame when the block being rendered began) the
/// event at `stamp` fell, in frames, modulo the ring (`mask` + 1 frames).
pub fn render_offset(stamp: u32, block_start_play: u32, mask: u32) -> u32 {
    stamp.wrapping_sub(block_start_play) & mask
}

/// Returns the stamp for an event at `event_ticks`, read now.
#[cfg(target_os = "none")]
pub fn now_for(event_ticks: u64) -> u32 {
    stamp_at(
        crate::audio::device::tx_play_frame() as u32,
        embassy_time::Instant::now().as_ticks(),
        event_ticks,
        embassy_time::TICK_HZ,
        crate::audio::device::SAMPLE_RATE_HZ,
        crate::audio::device::TX_MASK as u32,
    )
}

/// Returns 0: the host render has no play position to read, so every event is "now".
///
/// The host's `deluge_audio_stamp_to_render_offset` answers 0 too.
#[cfg(not(target_os = "none"))]
pub fn now_for(_event_ticks: u64) -> u32 {
    0
}
