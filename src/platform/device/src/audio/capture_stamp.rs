//! Capture stamps: where on the play timeline an input event happened.
//!
//! The play timeline counts the codec's frames from the drive's first block: the drive's unwrapped
//! play block times 128, plus how far into that block the DMA is ([`timeline_frame`]). A capture
//! stamp is the timeline frame at the moment of the event, as a wrapping `u32` (it wraps after 27
//! hours). Every input the app times (a MIDI byte, a trigger-clock edge) is stamped on this one
//! timeline, so `deluge_audio_stamp_to_render_offset` can place it in the next render. Each event
//! takes effect the same latency after it happened, so the spacing between events is exact; the
//! timeline does not wrap at the ring, so a stamp read late cannot be mistaken for a recent one.

use crate::audio::block_drive::BLOCK_FRAMES;

/// The oldest an event's stamp may be, in frames (about 6 minutes): older events clamp to it. Far
/// beyond anything that can still take effect, and far inside the `u32` timeline's wrap.
pub const STAMP_MAX_AGE_FRAMES: u32 = 1 << 24;

/// Returns the timeline frame the DMA is at, `play_frame` in the ring (`mask` + 1 frames), given
/// the drive's unwrapped play block at its last plan. The codec cannot have moved a ring past that
/// block without an overrun the drive has yet to see, so the ring position counts on from it.
pub fn timeline_frame(play_frame: u32, played_block: u32, mask: u32) -> u32 {
    let base = played_block.wrapping_mul(BLOCK_FRAMES as u32);
    base.wrapping_add(play_frame.wrapping_sub(base) & mask)
}

/// Returns the stamp for an event that happened at `event_ticks`, read when the timeline is at
/// `play_now` and the Embassy clock reads `now_ticks`. Ticks are at `tick_hz`, frames at
/// `sample_rate`.
///
/// An event from the future (a clock read out of order) counts as now. An event more than
/// [`STAMP_MAX_AGE_FRAMES`] old clamps to that age.
pub fn stamp_at(
    play_now: u32,
    now_ticks: u64,
    event_ticks: u64,
    tick_hz: u64,
    sample_rate: u32,
) -> u32 {
    let elapsed_ticks = now_ticks.saturating_sub(event_ticks);
    let elapsed_frames = elapsed_ticks.saturating_mul(u64::from(sample_rate)) / tick_hz;
    let back = elapsed_frames.min(u64::from(STAMP_MAX_AGE_FRAMES)) as u32;
    play_now.wrapping_sub(back)
}

/// Returns the stamp of the sample `offset` after the next rendered one, which plays at
/// `write_end`. Never 0, which the output service reads as "as soon as possible".
pub fn stamp_for_offset(write_end: u32, offset: u32) -> u32 {
    match write_end.wrapping_add(offset) {
        0 => 1,
        stamp => stamp,
    }
}

/// The frames from an input event to the output sample it takes effect at: the most the write head
/// can lead the play head just after a render (the queue and the block playing), plus one block
/// for the task tier to read the event in.
pub const INPUT_LATENCY_FRAMES: u32 = ((crate::audio::block_drive::QUEUE_BLOCKS + 2)
    * crate::audio::block_drive::BLOCK_FRAMES) as u32;

/// Returns how many samples after the next one the render produces the event at `stamp` takes
/// effect at, so that it plays [`INPUT_LATENCY_FRAMES`] after it happened. `write_end` is the
/// timeline frame the next rendered sample plays at.
///
/// An event read so late that its place is already rendered takes effect at once (0). A stamp at
/// or ahead of `write_end` (an event before the first render set it) takes the full latency.
pub fn input_offset(stamp: u32, write_end: u32) -> u32 {
    let behind = write_end.wrapping_sub(stamp);
    // Ahead of the write head reads as a huge distance behind: treat it as at the write head.
    let behind = if behind > u32::MAX / 2 { 0 } else { behind };
    INPUT_LATENCY_FRAMES.saturating_sub(behind)
}

/// Returns the stamp for an event at `event_ticks`, read now.
#[cfg(target_os = "none")]
pub fn now_for(event_ticks: u64) -> u32 {
    stamp_at(
        crate::audio::device::play_timeline_frame(),
        embassy_time::Instant::now().as_ticks(),
        event_ticks,
        embassy_time::TICK_HZ,
        crate::audio::device::SAMPLE_RATE_HZ,
    )
}

/// Returns 0: the host render has no play position to read, so every event is "now".
///
/// The host's `deluge_audio_stamp_to_render_offset` answers 0 too.
#[cfg(not(target_os = "none"))]
pub fn now_for(_event_ticks: u64) -> u32 {
    0
}
