//! Counts the synchronous card calls made after boot on the INTERACTION tier, per bridge entry point, and
//! separately any made on the AUDIO tier.
//!
//! Every synchronous bridge function (the `deluge_efatfs_*` C ABI in `efatfs_fs.rs` and its host mirror, and
//! `sd.rs`'s `deluge_block_read`/`deluge_block_write`) calls [`note`] as its first statement. Blocking on the
//! card from the interaction tier is legal (it waits on an IRQ-completed transfer), but it holds the UI for the
//! transfer; this is the inventory of where that still happens. Boot is excluded: blocking during boot is the
//! design. Nothing here fails or freezes -- the counts are reported, and enforcement comes with each conversion.
//!
//! On host, the first few counted calls of each entry also log a backtrace (`TIER-SYNC-SITE`), which is how a
//! count is traced back to its caller. `DELUGE_TIER_SYNC_TRACE=all` logs every counted call instead.
use core::ffi::c_char;
use core::sync::atomic::{AtomicU32, Ordering};

use crate::tier::{self, Tier};

macro_rules! entries {
    ($($variant:ident => $name:literal),* $(,)?) => {
        /// A synchronous bridge entry point.
        #[repr(u8)]
        #[derive(Clone, Copy, Debug, PartialEq, Eq)]
        pub enum Entry { $($variant),* }

        /// Each entry's short name, NUL-terminated for the C ABI.
        const NAMES: &[&str] = &[$(concat!($name, "\0")),*];
    };
}

entries! {
    Open => "open",
    Close => "close",
    ReadAt => "read_at",
    FileOpen => "file_open",
    FileReadExact => "file_read_exact",
    FileWrite => "file_write",
    FileSeek => "file_seek",
    FileSize => "file_size",
    FileClose => "file_close",
    DirOpen => "dir_open",
    DirRead => "dir_read",
    DirClose => "dir_close",
    Stats => "stats",
    Mount => "mount",
    Unmount => "unmount",
    Remount => "remount",
    ClusterSize => "cluster_size",
    Mkdir => "mkdir",
    Unlink => "unlink",
    Rename => "rename",
    SetTime => "set_time",
    StreamOpen => "stream_open",
    StreamWriteAt => "stream_write_at",
    StreamReadAtVia => "stream_read_at_via",
    StreamTruncate => "stream_truncate",
    StreamSize => "stream_size",
    StreamClose => "stream_close",
    BlockRead => "block_read",
    BlockWrite => "block_write",
}

const N: usize = NAMES.len();
static COUNTS: [AtomicU32; N] = [const { AtomicU32::new(0) }; N];
/// The audio tier must never block, so each of these is a violation; they are counted rather than refused so a
/// harness can inventory them.
static AUDIO_COUNTS: [AtomicU32; N] = [const { AtomicU32::new(0) }; N];

impl Entry {
    /// The entry's short name (`file_open`, `block_read`, ...).
    pub fn name(self) -> &'static str {
        let s = NAMES[self as usize];
        &s[..s.len() - 1]
    }
}

/// Count one synchronous call through `e`, if boot has finished and the caller is on the interaction or audio tier.
pub fn note(e: Entry) {
    if !crate::boot::finished() {
        return;
    }
    let (counts, tag) = match tier::current() {
        Tier::Interaction => (&COUNTS, "TIER-SYNC-SITE"),
        Tier::Audio => (&AUDIO_COUNTS, "TIER-SYNC-AUDIO-SITE"),
        _ => return,
    };
    let n = counts[e as usize].fetch_add(1, Ordering::Relaxed) + 1;
    #[cfg(not(target_os = "none"))]
    sample_site(tag, e, n);
    #[cfg(target_os = "none")]
    let _ = (n, tag);
}

/// How many counted calls of each entry log their backtrace.
#[cfg(not(target_os = "none"))]
const SITES_PER_ENTRY: u32 = 4;

#[cfg(not(target_os = "none"))]
fn sample_site(tag: &str, e: Entry, n: u32) {
    let all = std::env::var_os("DELUGE_TIER_SYNC_TRACE").is_some_and(|v| v == "all");
    if n <= SITES_PER_ENTRY || all {
        log::warn!(
            "{tag} entry={} n={n}\n{}",
            e.name(),
            std::backtrace::Backtrace::force_capture()
        );
    }
}

/// Counted calls through `e`.
pub fn count(e: Entry) -> u32 {
    COUNTS[e as usize].load(Ordering::Relaxed)
}

/// Counted calls over every entry.
pub fn total() -> u32 {
    COUNTS.iter().map(|c| c.load(Ordering::Relaxed)).sum()
}

/// Print `TIER-SYNC total=<N>`, then one `TIER-SYNC-AT entry=<name> n=<k>` per non-zero entry, in entry order.
/// Audio-tier calls follow as `TIER-SYNC-AUDIO total=<N>` and `TIER-SYNC-AUDIO-AT ...`, printed only when there
/// are any, so a run without them prints what it always has. The golden runner calls this at exit.
#[cfg(not(target_os = "none"))]
pub fn print_report() {
    println!("TIER-SYNC total={}", total());
    print_entries("TIER-SYNC-AT", &COUNTS);
    let audio: u32 = AUDIO_COUNTS.iter().map(|c| c.load(Ordering::Relaxed)).sum();
    if audio != 0 {
        println!("TIER-SYNC-AUDIO total={audio}");
        print_entries("TIER-SYNC-AUDIO-AT", &AUDIO_COUNTS);
    }
}

#[cfg(not(target_os = "none"))]
fn print_entries(tag: &str, counts: &[AtomicU32; N]) {
    for (i, c) in counts.iter().enumerate() {
        let n = c.load(Ordering::Relaxed);
        if n != 0 {
            let name = NAMES[i];
            println!("{tag} entry={} n={n}", &name[..name.len() - 1]);
        }
    }
}

/// C ABI (`libdeluge/tier.h`): synchronous card calls made on the audio tier after boot, over every entry.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_tier_sync_audio_total() -> u32 {
    AUDIO_COUNTS.iter().map(|c| c.load(Ordering::Relaxed)).sum()
}

/// C ABI (`libdeluge/tier.h`): how many entries there are.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_tier_sync_entries() -> u8 {
    N as u8
}

/// C ABI (`libdeluge/tier.h`): counted calls through entry index `entry`; 0 out of range.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_tier_sync_count(entry: u8) -> u32 {
    COUNTS
        .get(entry as usize)
        .map_or(0, |c| c.load(Ordering::Relaxed))
}

/// C ABI (`libdeluge/tier.h`): entry index `entry`'s short name, or "" out of range. Static storage.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_tier_sync_name(entry: u8) -> *const c_char {
    NAMES
        .get(entry as usize)
        .map_or(c"".as_ptr(), |s| s.as_ptr().cast())
}
