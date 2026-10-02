#![no_main]

//! Fuzz target: no input panics the decoder, and no caller behaviour makes a walk unbounded.
//!
//! Four drivers over the same bytes:
//! 1. `parse`; on success, wavetable acceptance at both `insist` settings.
//! 2. `HeaderParser` fed honestly, sometimes with far more than was asked for (exercising `feed`'s
//!    satisfy-from-this-window loop).
//! 3. `HeaderParser` fed by a caller that zero-pads short reads to the requested length, with the
//!    declared size overshooting the bytes held. Without the size clamp an all-zero window is a
//!    size-0 chunk and the walk steps 8 bytes at a time until `u32` overflow.
//! 4. `HeaderParser` fed adversarially: arbitrary bytes, lengths and claimed `window_start`s
//!    across the whole `u32` range. It must still terminate.
//!
//! Every `HeaderParser` driver checks each request lies inside the file and fits
//! `MAX_WINDOW_BYTES`, and caps its feed count at a budget linear in the file size.

use deluge_audio_format::{FeedResult, HeaderParser, MAX_WINDOW_BYTES};
use libfuzzer_sys::fuzz_target;

/// Feed budget for a `size`-byte file: each chunk takes at least 8 bytes and at most ~10 requests
/// (a `MARK` chunk's count plus 8 marker records).
fn feed_budget(size: u32) -> u32 {
    64u32.saturating_add((size / 8).saturating_mul(12))
}

/// Asserts that a request lies inside the file and fits `MAX_WINDOW_BYTES`.
fn check_request(start: u32, len: u32, size: u32) {
    assert!(
        u64::from(start) + u64::from(len) <= u64::from(size),
        "asked for {len} bytes at {start}, past the {size}-byte file"
    );
    assert!(len <= MAX_WINDOW_BYTES, "asked for a {len}-byte window");
}

/// Answers each request honestly; `oversupply` adds that many extra bytes (255: the rest of the
/// file).
fn drive_honestly(data: &[u8], oversupply: u8) {
    let size = data.len() as u32;
    let mut parser = HeaderParser::new(size);
    let budget = feed_budget(size);
    let mut feeds = 0;
    while let Some((start, len)) = parser.pending() {
        check_request(start, len, size);
        feeds += 1;
        assert!(feeds <= budget, "no progress: {feeds} feeds on {size} bytes");

        let from = start as usize;
        let to = match oversupply {
            0 => from + len as usize,                              // exactly what was asked
            u8::MAX => data.len(),                                 // everything we hold
            extra => (from + len as usize + usize::from(extra)).min(data.len()),
        };
        match parser.feed(&data[from..to], start) {
            Ok(FeedResult::Done(_)) | Err(_) => return,
            Ok(FeedResult::Need { .. }) => {}
        }
    }
}

/// Answers with a zeroed `len`-byte buffer, so bytes past those held come back as zeros. `slack`
/// makes the declared size overshoot the bytes held, so padding actually happens.
fn drive_zero_padding(data: &[u8], slack: u8) {
    let declared = (data.len() as u32).saturating_add(u32::from(slack));
    let mut parser = HeaderParser::new(declared);
    let budget = feed_budget(declared);
    let mut feeds = 0;
    while let Some((start, len)) = parser.pending() {
        check_request(start, len, declared);
        feeds += 1;
        assert!(
            feeds <= budget,
            "zero-padded caller walked {feeds} feeds on a declared {declared} bytes"
        );

        let mut window = vec![0u8; len as usize];
        let from = (start as usize).min(data.len());
        let available = data.len() - from;
        let copy = available.min(window.len());
        window[..copy].copy_from_slice(&data[from..from + copy]);

        match parser.feed(&window, start) {
            Ok(FeedResult::Done(_)) | Err(_) => return,
            Ok(FeedResult::Need { .. }) => {}
        }
    }
}

/// Answers with arbitrary bytes, at an arbitrary claimed offset, in arbitrary quantity, scripted
/// by `script`.
fn drive_adversarially(data: &[u8], script: &[u8]) {
    if data.is_empty() {
        return;
    }
    let size = data.len() as u32;
    let mut parser = HeaderParser::new(size);
    let budget = feed_budget(size);
    let mut feeds = 0;
    let mut step = 0;
    let byte = |i: usize| usize::from(script.get(i).copied().unwrap_or(0));
    while let Some((start, len)) = parser.pending() {
        check_request(start, len, size);
        feeds += 1;
        assert!(
            feeds <= budget,
            "no progress under a misbehaving caller: {feeds} feeds"
        );

        // A claimed window_start from the whole u32 range.
        let claimed = u32::from_le_bytes([
            script.get(step).copied().unwrap_or(0),
            script.get(step + 1).copied().unwrap_or(0),
            script.get(step + 2).copied().unwrap_or(0),
            script.get(step + 3).copied().unwrap_or(0),
        ]);
        let from = byte(step + 4) % data.len();
        // Deliberately unclamped by the request: sometimes far more than asked.
        let to = (from + byte(step + 5) * 8).min(data.len());
        let window_start = match byte(step + 6) % 3 {
            0 => start,             // the right offset, wrong bytes
            1 => claimed,           // anywhere at all
            _ => start.wrapping_sub(claimed % 64), // just before it: a superset
        };
        step = step.wrapping_add(7);

        match parser.feed(&data[from..to], window_start) {
            Ok(FeedResult::Done(_)) | Err(_) => return,
            Ok(FeedResult::Need { .. }) => {}
        }
    }
}

fuzz_target!(|data: &[u8]| {
    if let Ok(header) = deluge_audio_format::parse(data) {
        let _ = deluge_audio_format::wavetable::accept_as_wavetable(&header, false);
        let _ = deluge_audio_format::wavetable::accept_as_wavetable(&header, true);
    }

    // The first byte sets the honest oversupply and the zero-padding slack; the second half
    // scripts the adversarial caller over the first half.
    let knob = data.first().copied().unwrap_or(0);
    drive_honestly(data, knob);
    drive_zero_padding(data, knob);
    let split = data.len() / 2;
    drive_adversarially(&data[..split], &data[split..]);
});
