#![no_main]

//! Fuzz target: assert deluge_audio_format's central promise — no panic on any
//! input, whichever way the parse is driven — and, for the streaming API, that
//! no caller behaviour can turn a decode into an unbounded walk.
//!
//! Four drivers over the same bytes:
//! 1. `parse`, the whole-file wrapper. A parse that succeeds must also survive
//!    the wavetable-acceptance policy pass at both `insist` settings.
//! 2. `HeaderParser` driven the way a reader on a card would, with windows that
//!    sometimes hand back far more than was asked for — the recommended
//!    behaviour, and the path through `feed`'s "consume the rest of this window"
//!    loop that an exactly-sized answer never reaches.
//! 3. `HeaderParser` driven by a caller that **zero-pads** every window to the
//!    requested length instead of returning a short one. This is the single most
//!    natural adapter mistake (`read_exact` into a zeroed buffer), and before
//!    the parser was told the file's size it was fatal: an all-zero window reads
//!    as a chunk tagged `\0\0\0\0` of size 0, the walk steps 8 bytes, and it
//!    repeats until `u32` overflow — 536,870,912 feeds, i.e. a hang on a device
//!    doing one SD read per feed. It is now unreachable by construction (a
//!    request never names a byte past the end, so there is nothing to pad), and
//!    the assertions below hold that line.
//! 4. `HeaderParser` driven adversarially: windows taken from arbitrary offsets,
//!    of arbitrary length, at arbitrary claimed `window_start`s across the whole
//!    `u32` range — a caller answering a question nobody asked. Must still
//!    terminate; a walk that keeps asking without advancing is worse than a
//!    wrong answer.
//!
//! Every state-machine driver bounds its feed count against the file size the
//! parser was given: each chunk consumes at least its 8-byte header before the
//! next one, so an honest walk is linear in the file and a runaway is a real
//! defect, not a slow test.

use deluge_audio_format::{FeedResult, HeaderParser, MAX_WINDOW_BYTES};
use libfuzzer_sys::fuzz_target;

/// Feeds are bounded by the file's own chunk count: each chunk costs at least 8
/// bytes and yields at most ~10 requests (a `MARK` chunk's count plus its 8
/// bounded marker records). Generous, but finite — which is the whole point.
fn feed_budget(size: u32) -> u32 {
    64u32.saturating_add((size / 8).saturating_mul(12))
}

/// Assert the promise `pending` makes: the window it names lies wholly inside
/// the file, and is small enough to hold on a stack.
fn check_request(start: u32, len: u32, size: u32) {
    assert!(
        u64::from(start) + u64::from(len) <= u64::from(size),
        "asked for {len} bytes at {start}, past the {size}-byte file"
    );
    assert!(len <= MAX_WINDOW_BYTES, "asked for a {len}-byte window");
}

/// A reader that answers each request honestly, sometimes with far more than was
/// asked for.
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

/// The adapter mistake: `read_exact` into a zeroed `len`-sized buffer, so bytes
/// past the end of the file come back as zeros rather than as a short window.
/// `slack` makes the size the parser is told overshoot the bytes actually held,
/// so the padding really happens rather than being a no-op.
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

/// A caller answering the wrong question: arbitrary bytes, at an arbitrary
/// claimed offset, in arbitrary quantity.
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

        // A claimed window_start drawn from the whole u32 range, not just the
        // low byte: an offset near the request, wildly past it, and everything
        // between all have to terminate.
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

    // The first byte steers how much the honest reader oversupplies and how far
    // the declared size overshoots the bytes held; the tail scripts how badly
    // the adversarial caller misbehaves on the head.
    let knob = data.first().copied().unwrap_or(0);
    drive_honestly(data, knob);
    drive_zero_padding(data, knob);
    let split = data.len() / 2;
    drive_adversarially(&data[..split], &data[split..]);
});
