#![no_main]

//! Fuzz target: assert deluge_audio_format's central promise — no panic on any
//! input, whichever way the parse is driven.
//!
//! Three drivers over the same bytes:
//! 1. `parse`, the whole-file wrapper. A parse that succeeds must also survive
//!    the wavetable-acceptance policy pass at both `insist` settings.
//! 2. `HeaderParser` driven honestly, as a reader on a card would: every request
//!    answered from the same bytes, short at the end. Must terminate.
//! 3. `HeaderParser` driven adversarially: windows taken from arbitrary offsets
//!    and lengths chosen by the fuzzer, i.e. a caller that answers the wrong
//!    question. Must still terminate — a walk that keeps asking without ever
//!    advancing is a hang on the device, which is worse than a wrong answer.
//!
//! Both state-machine drivers bound their feed count against the input's own
//! size: every chunk consumes at least its 8-byte header before the next one, so
//! an honest walk is linear in the input and a runaway is a real defect, not a
//! slow test.

use deluge_audio_format::{FeedResult, HeaderParser};
use libfuzzer_sys::fuzz_target;

/// Feeds are bounded by the file's own chunk count: each chunk costs at least 8
/// bytes and yields at most ~10 requests (a `MARK` chunk's count plus its 8
/// bounded marker records). Generous, but finite — which is the whole point.
fn feed_budget(len: usize) -> u32 {
    64u32.saturating_add((len as u32 / 8).saturating_mul(12))
}

fn drive_honestly(data: &[u8]) {
    let mut parser = HeaderParser::new();
    let budget = feed_budget(data.len());
    let mut feeds = 0;
    while let Some((start, len)) = parser.pending() {
        assert!(
            len <= deluge_audio_format::MAX_WINDOW_BYTES,
            "asked for {len} bytes at {start}, past the window bound"
        );
        feeds += 1;
        assert!(feeds <= budget, "no progress: {feeds} feeds on {} bytes", data.len());
        let from = (start as usize).min(data.len());
        let to = from.saturating_add(len as usize).min(data.len());
        match parser.feed(&data[from..to], start) {
            Ok(FeedResult::Done(_)) | Err(_) => return,
            Ok(FeedResult::Need { .. }) => {}
        }
    }
}

fn drive_adversarially(data: &[u8], script: &[u8]) {
    if data.is_empty() {
        return;
    }
    let mut parser = HeaderParser::new();
    let budget = feed_budget(data.len());
    let mut feeds = 0;
    let mut step = 0;
    while let Some((start, _len)) = parser.pending() {
        feeds += 1;
        assert!(
            feeds <= budget,
            "no progress under a misbehaving caller: {feeds} feeds"
        );
        // Answer with a window from wherever the script says, at whatever offset
        // it claims — including offsets that have nothing to do with the request.
        let claimed_start = u32::from(script.get(step).copied().unwrap_or(0));
        let from = usize::from(script.get(step + 1).copied().unwrap_or(0)) % data.len();
        let take = usize::from(script.get(step + 2).copied().unwrap_or(0));
        step = step.wrapping_add(3);
        let to = from.saturating_add(take).min(data.len());
        let window_start = if step % 2 == 0 { start } else { claimed_start };
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

    drive_honestly(data);
    // Split the input: the tail scripts how badly the caller misbehaves on the head.
    let split = data.len() / 2;
    drive_adversarially(&data[..split], &data[split..]);
});
