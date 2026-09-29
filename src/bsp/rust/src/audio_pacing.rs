//! The audio drive's render-window pacing: how many frames one render call takes, given how far the DMA play head has
//! moved. Its own module so the host can test it (`tests/audio_pacing_host.rs`); `audio.rs` runs it on the device.

/// Round/expand a raw free-frame count into a render window: double the amount over a small threshold (smoother under
/// light load), cap at the ring, then round up to a multiple of 4 for the NEON kernels.
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
