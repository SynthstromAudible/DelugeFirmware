//! Frame addressing on the SSI rings, which the SDK lends as `u32` words (2 per stereo frame) and
//! whose positions it reports in words (`deluge_bsp::audio::with_tx` / `with_rx`).

/// The frame a ring position is at, masked to the ring (`mask` = frames - 1). `None`, a ring that
/// isn't there yet, is frame 0.
#[inline]
pub(crate) fn frame_at(position_words: Option<usize>, mask: usize) -> usize {
    (position_words.unwrap_or(0) / 2) & mask
}

/// Write one stereo frame at `frame` of the ring at `ring` (left word, then right). A null `ring`
/// drops it.
///
/// # Safety
/// A non-null `ring` holds at least `2 * (frame + 1)` words, on the uncached alias.
#[inline]
pub(crate) unsafe fn write_frame(ring: *mut u32, frame: usize, l: i32, r: i32) {
    if ring.is_null() {
        return;
    }
    // SAFETY: the caller's bound on `frame`.
    unsafe {
        let p = ring.add(frame * 2);
        p.write_volatile(l as u32);
        p.add(1).write_volatile(r as u32);
    }
}

/// Read the stereo frame at `frame` of the ring at `ring`. A null `ring` reads silence.
///
/// # Safety
/// As [`write_frame`].
#[inline]
pub(crate) unsafe fn read_frame(ring: *const u32, frame: usize) -> (i32, i32) {
    if ring.is_null() {
        return (0, 0);
    }
    // SAFETY: the caller's bound on `frame`.
    unsafe {
        let p = ring.add(frame * 2);
        (p.read_volatile() as i32, p.add(1).read_volatile() as i32)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Before board audio init there is no ring and no position: the play and record heads read as
    /// frame 0, as the MIDI capture stamps expect, rather than panicking.
    #[test]
    fn tx_play_frame_is_zero_before_audio_init() {
        assert_eq!(frame_at(None, 1023), 0);
        assert_eq!(frame_at(None, 2047), 0);
    }

    #[test]
    fn a_word_position_becomes_a_masked_frame() {
        assert_eq!(frame_at(Some(201), 1023), 100);
        assert_eq!(frame_at(Some(2 * 1024 + 6), 1023), 3);
    }

    /// A frame lands at its own two words, left then right, not at the start of the ring.
    #[test]
    fn flush_writes_at_the_play_head() {
        let mut ring = [0u32; 2048];
        unsafe { write_frame(ring.as_mut_ptr(), 100, -1, 7) };
        assert_eq!((ring[200], ring[201]), (u32::MAX, 7));
        assert!(ring[..200].iter().chain(&ring[202..]).all(|&w| w == 0));
        assert_eq!(unsafe { read_frame(ring.as_ptr(), 100) }, (-1, 7));
    }

    /// Before init the cached ring pointers are null: writes are dropped and reads are silence.
    #[test]
    fn a_null_ring_is_silence() {
        unsafe { write_frame(core::ptr::null_mut(), 5, 1, 2) };
        assert_eq!(unsafe { read_frame(core::ptr::null(), 5) }, (0, 0));
    }
}
