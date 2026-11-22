//! Each CV channel's latest value awaiting the DAC: pure logic, so it is tested on host.
//!
//! A CV output is a level, so only the latest value matters. A value set again before the writer
//! takes it replaces the earlier one rather than queueing behind it, so nothing is dropped however
//! long the shared bus is held.

use core::sync::atomic::{AtomicU32, Ordering};

/// Marks a slot's value as not yet taken; the low 16 bits are the value.
const PENDING: u32 = 1 << 16;

/// The latest value set on each of `N` channels, each taken at most once.
///
/// Safe to share between a setter in any context and one taking task: each slot is one atomic word.
pub(crate) struct CvLatest<const N: usize> {
    slots: [AtomicU32; N],
}

impl<const N: usize> CvLatest<N> {
    /// No channel pending.
    pub(crate) const fn new() -> Self {
        Self {
            slots: [const { AtomicU32::new(0) }; N],
        }
    }

    /// Makes `value` channel `channel`'s pending value, replacing any not yet taken. Returns false,
    /// and ignores the value, for a channel past `N`.
    pub(crate) fn set(&self, channel: u8, value: u16) -> bool {
        match self.slots.get(channel as usize) {
            Some(slot) => {
                slot.store(PENDING | u32::from(value), Ordering::Release);
                true
            }
            None => false,
        }
    }

    /// Takes channel `channel`'s pending value, if one was set since it was last taken.
    pub(crate) fn take(&self, channel: usize) -> Option<u16> {
        let taken = self.slots[channel].swap(0, Ordering::AcqRel);
        (taken & PENDING != 0).then_some(taken as u16)
    }
}

#[cfg(test)]
mod tests {
    use super::CvLatest;

    #[test]
    fn nothing_is_pending_at_first() {
        let latest = CvLatest::<2>::new();
        assert_eq!(latest.take(0), None);
        assert_eq!(latest.take(1), None);
    }

    #[test]
    fn a_value_is_taken_once() {
        let latest = CvLatest::<2>::new();
        assert!(latest.set(1, 1234));
        assert_eq!(latest.take(1), Some(1234));
        assert_eq!(latest.take(1), None);
        assert_eq!(latest.take(0), None);
    }

    #[test]
    fn a_later_value_replaces_one_not_yet_taken() {
        let latest = CvLatest::<2>::new();
        for value in 0..100u16 {
            latest.set(0, value);
        }
        assert_eq!(latest.take(0), Some(99));
        assert_eq!(latest.take(0), None);
    }

    #[test]
    fn zero_is_a_value_like_any_other() {
        let latest = CvLatest::<2>::new();
        latest.set(0, 0);
        assert_eq!(latest.take(0), Some(0));
    }

    #[test]
    fn a_channel_past_the_last_is_ignored() {
        let latest = CvLatest::<2>::new();
        assert!(!latest.set(2, 7));
        assert_eq!(latest.take(0), None);
        assert_eq!(latest.take(1), None);
    }
}
