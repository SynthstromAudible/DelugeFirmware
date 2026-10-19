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
    /// The card reads of the fill read now holding the filesystem, while one does.
    card: Option<CardReads>,
    /// The card reads of the audio loader's open by path now holding the filesystem, while one does.
    open: Option<OpenReads>,
}

/// One open by path's card reads: how many, the sectors they moved, and their time.
#[derive(Clone, Copy, Default)]
pub struct OpenReads {
    pub reads: u32,
    pub sectors: u32,
    pub us: u64,
}

/// One fill read's card reads, split by what they fetch.
#[derive(Clone, Copy, Default)]
struct CardReads {
    meta_us: u64,
    data_us: u64,
}

static STATE: Mutex<RefCell<State>> = Mutex::new(RefCell::new(State {
    trace: zeroed(),
    waiting: false,
    signalled_at: None,
    last_read_end: None,
    last_release: None,
    card: None,
    open: None,
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
        interaction_fs_wait: zeroed_hist(),
        interaction_fs_hold: zeroed_hist(),
        read_meta: zeroed_hist(),
        read_data: zeroed_hist(),
        read_meta_sectors: 0,
        read_data_calls: 0,
        read_data_sectors: 0,
        loader_requests: 0,
        loader_request_us: 0,
        loader_request_max_us: 0,
        loader_open_hits: 0,
        loader_open_hit_us: 0,
        loader_open_misses: 0,
        loader_open_miss_us: 0,
        loader_reads: 0,
        loader_read_us: 0,
        loader_other_us: 0,
        loader_open_card_reads: 0,
        loader_open_card_sectors: 0,
        longest_fs_hold_us: 0,
        longest_fs_hold_line: 0,
        longest_fs_hold_file: [0; 48],
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

/// Marks a fill read taking the filesystem: the card reads until [`fill_read_held_end`] are its own,
/// since nothing else can reach the card while it holds the filesystem.
pub fn fill_read_held() {
    critical_section::with(|cs| STATE.borrow_ref_mut(cs).card = Some(CardReads::default()));
}

/// Ends the span [`fill_read_held`] began, returning the µs its card reads spent on FAT and
/// directory lookups and on data.
pub fn fill_read_held_end() -> (u64, u64) {
    critical_section::with(|cs| {
        let card = STATE.borrow_ref_mut(cs).card.take().unwrap_or_default();
        (card.meta_us, card.data_us)
    })
}

/// Records one fill read's split: `meta_us` in FAT and directory lookups, `data_us` in its data.
pub fn read_split(meta_us: u64, data_us: u64) {
    critical_section::with(|cs| {
        let mut s = STATE.borrow_ref_mut(cs);
        add(&mut s.trace.read_meta, meta_us);
        add(&mut s.trace.read_data, data_us);
    });
}

/// Records a data read of `sectors` sectors made straight from the card, outside any span.
pub fn direct_data(sectors: u32) {
    critical_section::with(|cs| {
        let mut s = STATE.borrow_ref_mut(cs);
        s.trace.read_data_calls = s.trace.read_data_calls.saturating_add(1);
        s.trace.read_data_sectors = s.trace.read_data_sectors.saturating_add(sectors);
    });
}

/// Records a card read of `sectors` sectors from `start` to `end`, if a fill read holds the
/// filesystem: a single sector is a FAT or directory lookup, more is the chunk's data.
pub fn card_read(sectors: u32, start: Instant, end: Instant) {
    let us = micros_since(start, end);
    critical_section::with(|cs| {
        let mut s = STATE.borrow_ref_mut(cs);
        if let Some(open) = s.open.as_mut() {
            open.reads += 1;
            open.sectors += sectors;
            open.us += us;
        }
        let Some(mut card) = s.card else {
            return;
        };
        if sectors == 1 {
            card.meta_us += us;
            s.trace.read_meta_sectors = s.trace.read_meta_sectors.saturating_add(1);
        } else {
            card.data_us += us;
            s.trace.read_data_calls = s.trace.read_data_calls.saturating_add(1);
            s.trace.read_data_sectors = s.trace.read_data_sectors.saturating_add(sectors);
        }
        s.card = Some(card);
    });
}

/// One kind of the audio loader's work, as [`loader`] records it.
#[derive(Clone, Copy)]
pub enum LoaderWork {
    /// A sample's whole header load.
    Request,
    /// An open by path that found the file.
    OpenHit,
    /// An open by path that did not.
    OpenMiss,
    /// A header read.
    Read,
    /// Sizing, the location, the close.
    Other,
}

/// Marks the audio loader's open by path starting: the card reads until [`open_span_end`] are its
/// own, since it holds the filesystem.
pub fn open_span_begin() {
    critical_section::with(|cs| STATE.borrow_ref_mut(cs).open = Some(OpenReads::default()));
}

/// Ends the span [`open_span_begin`] began, returning its card reads, which are also added to the
/// loader's totals.
pub fn open_span_end() -> OpenReads {
    critical_section::with(|cs| {
        let mut s = STATE.borrow_ref_mut(cs);
        let open = s.open.take().unwrap_or_default();
        s.trace.loader_open_card_reads = s.trace.loader_open_card_reads.saturating_add(open.reads);
        s.trace.loader_open_card_sectors = s
            .trace
            .loader_open_card_sectors
            .saturating_add(open.sectors);
        open
    })
}

/// Records the audio loader doing `work` from `start` to now.
pub fn loader(work: LoaderWork, start: Instant) {
    let us = u32::try_from(micros_since(start, Instant::now())).unwrap_or(u32::MAX);
    critical_section::with(|cs| {
        let t = &mut STATE.borrow_ref_mut(cs).trace;
        let (n, total) = match work {
            LoaderWork::Request => {
                t.loader_request_max_us = t.loader_request_max_us.max(us);
                (&mut t.loader_requests, &mut t.loader_request_us)
            }
            LoaderWork::OpenHit => (&mut t.loader_open_hits, &mut t.loader_open_hit_us),
            LoaderWork::OpenMiss => (&mut t.loader_open_misses, &mut t.loader_open_miss_us),
            LoaderWork::Read => (&mut t.loader_reads, &mut t.loader_read_us),
            LoaderWork::Other => {
                t.loader_other_us = t.loader_other_us.saturating_add(us);
                return;
            }
        };
        *n = n.saturating_add(1);
        *total = total.saturating_add(us);
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

/// Records a filesystem holder letting go: it asked at `asked`, took the mutex at `acquired`, and
/// releases it now. An interaction-tier caller's wait and hold are histogrammed.
pub fn fs_released(
    tier: Tier,
    asked: Instant,
    acquired: Instant,
    at: &'static core::panic::Location<'static>,
) {
    let held = micros_since(acquired, Instant::now());
    critical_section::with(|cs| {
        let mut s = STATE.borrow_ref_mut(cs);
        s.last_release = Some((tier, held));
        let held32 = u32::try_from(held).unwrap_or(u32::MAX);
        if held32 > s.trace.longest_fs_hold_us {
            s.trace.longest_fs_hold_us = held32;
            s.trace.longest_fs_hold_line = at.line();
            // The file's tail is the part that names it.
            let file = at.file().as_bytes();
            let room = s.trace.longest_fs_hold_file.len() - 1;
            let tail = &file[file.len().saturating_sub(room)..];
            let mut name = [0 as core::ffi::c_char; 48];
            for (dst, &b) in name.iter_mut().zip(tail) {
                *dst = b as core::ffi::c_char;
            }
            s.trace.longest_fs_hold_file = name;
        }
        if tier == Tier::Interaction {
            add(
                &mut s.trace.interaction_fs_wait,
                micros_since(asked, acquired),
            );
            add(&mut s.trace.interaction_fs_hold, held);
        }
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
