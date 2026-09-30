//! The audio drive's render-window pacing: how many frames one render call takes, given how far the
//! DMA play head has moved.
//!
//! Only the device drive (`audio::device`) runs it; it is platform-independent so the host tests
//! can cover it.

/// Returns the render window for `raw` free frames in a ring of `ring` frames.
///
/// Doubles the amount over a small threshold (smoother under light load), caps at the ring, then
/// rounds up to a multiple of 4 for the NEON kernels.
pub fn pace_window(raw: usize, ring: usize) -> usize {
    const THRESHOLD: i32 = 6;
    let mut n = raw as i32;
    if (n as usize) < ring {
        let over = n - THRESHOLD;
        if over > 0 {
            n = THRESHOLD + (over << 1);
        }
    }
    if (n as usize) > ring {
        n = ring as i32;
    }
    if n >= 3 {
        n = (n + 2) & !3;
    }
    n as usize
}
