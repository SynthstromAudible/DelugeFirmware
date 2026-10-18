//! Where a streaming chunk's fill time goes, as histograms: the `libdeluge/streaming_fill.h`
//! fill-trace C ABI.
//!
//! The fill task records each phase of a chunk's service (see [`DelugeFillTrace`]), and every
//! [`with_fs`](super::efatfs) holder records its hold, so a fill that waited on the filesystem
//! mutex can name the tier it waited on. A few clock reads per 32 KB chunk, so it is always on.
//!
//! Recorders run on any tier (the fill task on the storage tier, a filesystem holder on any, the
//! wake on the audio tier); every update and the snapshot take a critical section.
use core::cell::RefCell;

use critical_section::Mutex;
use embassy_time::Instant;

use crate::sys::{DelugeFillTrace, DelugeFillTraceHist};
use crate::tier::Tier;

/// Buckets per histogram: bucket 0 holds 0 µs, bucket `i` holds `[2^(i-1), 2^i)` µs, and the last
/// also holds everything longer.
const BUCKETS: usize = crate::sys::DELUGE_FILL_TRACE_BUCKETS as usize;

/// A filesystem wait at least this long counts as contended, and is attributed to its holder.
const LONG_FS_WAIT_US: u64 = 2_000;

struct State {
    trace: DelugeFillTrace,
    /// Whether the fill task is waiting for a wake, rather than draining.
    waiting: bool,
    /// When the first wake since the fill task started waiting was signalled.
    signalled_at: Option<Instant>,
    /// When the current drain pass's previous read ended; `None` before its first.
    last_read_end: Option<Instant>,
    /// The tier and hold of the most recent filesystem release.
    last_release: Option<(Tier, u64)>,
}

static STATE: Mutex<RefCell<State>> = Mutex::new(RefCell::new(State {
    trace: zeroed(),
    waiting: false,
    signalled_at: None,
    last_read_end: None,
    last_release: None,
}));

const fn zeroed_hist() -> DelugeFillTraceHist {
    DelugeFillTraceHist {
        n: 0,
        max_us: 0,
        total_us: 0,
        buckets: [0; BUCKETS],
    }
}

const fn zeroed() -> DelugeFillTrace {
    DelugeFillTrace {
        wake: zeroed_hist(),
        fs_wait: zeroed_hist(),
        read: zeroed_hist(),
        finish: zeroed_hist(),
        gap: zeroed_hist(),
        pop: zeroed_hist(),
        begin: zeroed_hist(),
        long_fs_waits_by_tier: [0; 3],
        long_fs_wait_max_by_tier: [0; 3],
    }
}

/// Returns the bucket `us` falls in.
fn bucket(us: u64) -> usize {
    let bits = (u64::BITS - us.leading_zeros()) as usize;
    bits.min(BUCKETS - 1)
}

fn add(h: &mut DelugeFillTraceHist, us: u64) {
    h.n = h.n.saturating_add(1);
    h.total_us = h.total_us.saturating_add(us);
    let us32 = u32::try_from(us).unwrap_or(u32::MAX);
    if us32 > h.max_us {
        h.max_us = us32;
    }
    h.buckets[bucket(us)] = h.buckets[bucket(us)].saturating_add(1);
}

fn micros_since(start: Instant, end: Instant) -> u64 {
    end.saturating_duration_since(start).as_micros()
}

/// One chunk read, as [`read`] records it.
#[derive(Default)]
pub struct TimedRead {
    /// Whether the whole buffer was filled.
    pub filled: bool,
    /// When the filesystem mutex was taken; `None` if the read never got that far.
    pub acquired: Option<Instant>,
    /// The tier and hold (µs) of the release the read waited behind.
    pub waited_behind: Option<(Tier, u64)>,
}

/// Records a wake of the fill task. Only the first while it waits is timed: one that arrives during
/// a drain pass finds its chunk picked up by that pass.
pub fn signalled() {
    let now = Instant::now();
    critical_section::with(|cs| {
        let mut s = STATE.borrow_ref_mut(cs);
        if s.waiting && s.signalled_at.is_none() {
            s.signalled_at = Some(now);
        }
    });
}

/// Records the fill task starting to wait for a wake, after a drain pass.
pub fn waiting() {
    critical_section::with(|cs| STATE.borrow_ref_mut(cs).waiting = true);
}

/// Records the fill task resuming from its wait, at the start of a drain pass.
pub fn pass_begin() {
    let now = Instant::now();
    critical_section::with(|cs| {
        let mut s = STATE.borrow_ref_mut(cs);
        s.waiting = false;
        if let Some(at) = s.signalled_at.take() {
            add(&mut s.trace.wake, micros_since(at, now));
        }
        s.last_read_end = None;
    });
}

/// Records one chunk read: asked for at `start`, the filesystem taken at `acquired` (`None` if it
/// was unmounted), done at `end`. `holder` is the release the read waited behind.
pub fn read(start: Instant, acquired: Option<Instant>, end: Instant, holder: Option<(Tier, u64)>) {
    critical_section::with(|cs| {
        let mut s = STATE.borrow_ref_mut(cs);
        if let Some(prev) = s.last_read_end {
            add(&mut s.trace.gap, micros_since(prev, start));
        }
        s.last_read_end = Some(end);
        let Some(acquired) = acquired else {
            return;
        };
        let wait = micros_since(start, acquired);
        add(&mut s.trace.fs_wait, wait);
        add(&mut s.trace.read, micros_since(acquired, end));
        if wait >= LONG_FS_WAIT_US {
            if let Some((tier, _)) = holder {
                let t = tier as usize;
                s.trace.long_fs_waits_by_tier[t] =
                    s.trace.long_fs_waits_by_tier[t].saturating_add(1);
                let wait32 = u32::try_from(wait).unwrap_or(u32::MAX);
                if wait32 > s.trace.long_fs_wait_max_by_tier[t] {
                    s.trace.long_fs_wait_max_by_tier[t] = wait32;
                }
            }
        }
    });
}

/// Records a chunk's convert/stitch/publish tail, from `start` to `end`.
pub fn finish(start: Instant, end: Instant) {
    critical_section::with(|cs| {
        add(
            &mut STATE.borrow_ref_mut(cs).trace.finish,
            micros_since(start, end),
        );
    });
}

/// Records popping a chunk off the loader queue, from `start` to `end`.
pub fn pop(start: Instant, end: Instant) {
    critical_section::with(|cs| {
        add(
            &mut STATE.borrow_ref_mut(cs).trace.pop,
            micros_since(start, end),
        );
    });
}

/// Records resolving a popped chunk's read, from `start` to `end`.
pub fn begin(start: Instant, end: Instant) {
    critical_section::with(|cs| {
        add(
            &mut STATE.borrow_ref_mut(cs).trace.begin,
            micros_since(start, end),
        );
    });
}

/// Records a filesystem holder letting go, after holding it since `acquired`.
pub fn fs_released(tier: Tier, acquired: Instant) {
    let held = micros_since(acquired, Instant::now());
    critical_section::with(|cs| {
        STATE.borrow_ref_mut(cs).last_release = Some((tier, held));
    });
}

/// Returns the most recent filesystem release, for a holder that has just taken it.
pub fn last_fs_release() -> Option<(Tier, u64)> {
    critical_section::with(|cs| STATE.borrow_ref(cs).last_release)
}

/// Copies the trace since the last reset into `*out`. [task]
///
/// # Safety
///
/// `out` must be null or point to a writable `DelugeFillTrace`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_fill_trace_snapshot(out: *mut DelugeFillTrace) {
    if out.is_null() {
        return;
    }
    let trace = critical_section::with(|cs| STATE.borrow_ref(cs).trace);
    // SAFETY: the caller guarantees `out` is writable.
    unsafe { out.write(trace) };
}

/// Zeroes the trace. [task]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_fill_trace_reset() {
    critical_section::with(|cs| STATE.borrow_ref_mut(cs).trace = zeroed());
}

abi_pin!(deluge_fill_trace_snapshot, deluge_fill_trace_reset);

#[cfg(test)]
mod tests {
    use super::bucket;

    #[test]
    fn buckets_are_powers_of_two() {
        assert_eq!(bucket(0), 0);
        assert_eq!(bucket(1), 1);
        assert_eq!(bucket(2), 2);
        assert_eq!(bucket(3), 2);
        assert_eq!(bucket(4), 3);
        assert_eq!(bucket(1023), 10);
        assert_eq!(bucket(1024), 11);
        assert_eq!(bucket(u64::MAX), super::BUCKETS - 1);
    }
}
