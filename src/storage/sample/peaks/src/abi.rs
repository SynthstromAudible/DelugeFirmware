//! The C ABI (`deluge_storage_sample_peaks.h`): thin, null-checked wrappers over [`PeakIndex`].

use crate::column::ColumnPlan;
use crate::{PeakIndex, mem, peak, sidecar, tap};
use core::ptr::NonNull;

/// The C name of a [`PeakIndex`]: opaque.
#[repr(C)]
pub struct DelugePeakIndex {
    _opaque: [u8; 0],
}

/// `DelugePeak`: the lowest and highest 16-bit sample value over some frames.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct DelugePeak {
    /// The lowest value.
    pub min: i16,
    /// The highest value.
    pub max: i16,
}

/// `DelugeMaybePeak`: a peak, or nothing.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct DelugeMaybePeak {
    /// Whether `peak` holds anything.
    pub has: bool,
    /// The peak, when `has`.
    pub peak: DelugePeak,
}

/// `DelugePeaksColumn`: one column's plan.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct DelugePeaksColumn {
    /// A block the span covers wholly is not scanned yet.
    pub pending: bool,
    /// Whether `interior` holds anything.
    pub has_interior: bool,
    /// The stored peaks' fold.
    pub interior: DelugePeak,
    /// How many ranges to read.
    pub read_count: u8,
    /// Each range's first frame.
    pub read_lo: [u64; 2],
    /// Each range's end frame (exclusive).
    pub read_hi: [u64; 2],
}

/// `DelugePeaksAllocFn`.
pub type DelugePeaksAllocFn = Option<unsafe extern "C" fn(usize, usize) -> *mut core::ffi::c_void>;
/// `DelugePeaksFreeFn`.
pub type DelugePeaksFreeFn = Option<unsafe extern "C" fn(*mut core::ffi::c_void)>;

impl From<peak::Peak> for DelugePeak {
    fn from(p: peak::Peak) -> Self {
        Self {
            min: p.min,
            max: p.max,
        }
    }
}

impl From<DelugePeak> for peak::Peak {
    fn from(p: DelugePeak) -> Self {
        Self {
            min: p.min,
            max: p.max,
        }
    }
}

fn maybe(p: Option<peak::Peak>) -> DelugeMaybePeak {
    p.map_or_else(DelugeMaybePeak::default, |p| DelugeMaybePeak {
        has: true,
        peak: p.into(),
    })
}

fn index<'a>(p: *const DelugePeakIndex) -> Option<&'a PeakIndex> {
    // SAFETY: a non-null `DelugePeakIndex*` came from `deluge_peaks_create` and the caller holds a reference.
    NonNull::new(p.cast_mut().cast::<PeakIndex>()).map(|p| unsafe { p.as_ref() })
}

/// The installed C callbacks, adapted to [`mem`]'s.
static C_ALLOC: core::sync::atomic::AtomicPtr<()> =
    core::sync::atomic::AtomicPtr::new(core::ptr::null_mut());
static C_FREE: core::sync::atomic::AtomicPtr<()> =
    core::sync::atomic::AtomicPtr::new(core::ptr::null_mut());

unsafe extern "C" fn adapt_alloc(size: usize, align: usize) -> *mut u8 {
    let f = C_ALLOC.load(core::sync::atomic::Ordering::Acquire);
    if f.is_null() {
        return core::ptr::null_mut();
    }
    // SAFETY: stored from a non-null `DelugePeaksAllocFn` by `deluge_peaks_init`.
    let f: unsafe extern "C" fn(usize, usize) -> *mut core::ffi::c_void =
        unsafe { core::mem::transmute(f) };
    // SAFETY: the callback's contract.
    unsafe { f(size, align).cast::<u8>() }
}

unsafe extern "C" fn adapt_free(p: *mut u8) {
    let f = C_FREE.load(core::sync::atomic::Ordering::Acquire);
    if f.is_null() {
        return;
    }
    // SAFETY: stored from a non-null `DelugePeaksFreeFn` by `deluge_peaks_init`.
    let f: unsafe extern "C" fn(*mut core::ffi::c_void) = unsafe { core::mem::transmute(f) };
    // SAFETY: `p` came from the matching allocation callback.
    unsafe { f(p.cast::<core::ffi::c_void>()) }
}

/// `deluge_peaks_init`.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub extern "C" fn deluge_peaks_init(alloc: DelugePeaksAllocFn, free: DelugePeaksFreeFn) {
    let (Some(alloc), Some(free)) = (alloc, free) else {
        return;
    };
    C_ALLOC.store(alloc as *mut (), core::sync::atomic::Ordering::Release);
    C_FREE.store(free as *mut (), core::sync::atomic::Ordering::Release);
    mem::install(adapt_alloc, adapt_free);
}

/// `deluge_peaks_create`.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub extern "C" fn deluge_peaks_create(
    frames: u64,
    growing: bool,
    capacity_frames: u64,
) -> *mut DelugePeakIndex {
    PeakIndex::create(frames, growing, capacity_frames).map_or(core::ptr::null_mut(), |p| {
        p.as_ptr().cast::<DelugePeakIndex>()
    })
}

/// `deluge_peaks_retain`.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
pub extern "C" fn deluge_peaks_retain(index_ptr: *mut DelugePeakIndex) {
    if let Some(i) = index(index_ptr) {
        i.retain();
    }
}

/// `deluge_peaks_release`.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
pub extern "C" fn deluge_peaks_release(index_ptr: *mut DelugePeakIndex) {
    if let Some(p) = NonNull::new(index_ptr.cast::<PeakIndex>()) {
        PeakIndex::release(p);
    }
}

/// `deluge_peaks_collect`.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub extern "C" fn deluge_peaks_collect() {
    PeakIndex::collect();
}

/// `deluge_peaks_bind_asset`.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub extern "C" fn deluge_peaks_bind_asset(asset: u32, index_ptr: *mut DelugePeakIndex) {
    match index(index_ptr) {
        Some(i) => tap::bind(asset, i),
        None => tap::unbind(asset),
    }
}

/// `deluge_peaks_unbind_asset`.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub extern "C" fn deluge_peaks_unbind_asset(asset: u32) {
    tap::unbind(asset);
}

/// `deluge_peaks_reserve`.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub extern "C" fn deluge_peaks_reserve(index_ptr: *mut DelugePeakIndex, frames: u64) {
    if let Some(i) = index(index_ptr) {
        i.reserve(frames);
    }
}

/// `deluge_peaks_ensure_level0`.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub extern "C" fn deluge_peaks_ensure_level0(
    index_ptr: *mut DelugePeakIndex,
    first: u64,
    end: u64,
) {
    if let Some(i) = index(index_ptr) {
        i.ensure_level0(first, end);
    }
}

/// `deluge_peaks_fold_frames`.
///
/// # Safety
/// `bytes` points at `length` readable bytes, or is null.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
pub unsafe extern "C" fn deluge_peaks_fold_frames(
    index_ptr: *mut DelugePeakIndex,
    first_frame: u64,
    bytes: *const u8,
    length: u32,
    byte_depth: u8,
    channels: u8,
) {
    let Some(i) = index(index_ptr) else {
        return;
    };
    if bytes.is_null() {
        return;
    }
    // SAFETY: the caller's contract.
    let slice = unsafe { core::slice::from_raw_parts(bytes, length as usize) };
    i.fold_frames(first_frame, slice, byte_depth, channels);
}

/// `deluge_peaks_fold_block`.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
pub extern "C" fn deluge_peaks_fold_block(
    index_ptr: *mut DelugePeakIndex,
    block: u64,
    peak: DelugePeak,
) {
    if let Some(i) = index(index_ptr) {
        i.fold_block(block, peak.into());
    }
}

/// `deluge_peaks_set_frames`.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub extern "C" fn deluge_peaks_set_frames(
    index_ptr: *mut DelugePeakIndex,
    frames: u64,
    is_final: bool,
) {
    if let Some(i) = index(index_ptr) {
        i.set_frames(frames, is_final);
    }
}

/// `deluge_peaks_column`.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub extern "C" fn deluge_peaks_column(
    index_ptr: *const DelugePeakIndex,
    first: u64,
    end: u64,
) -> DelugePeaksColumn {
    let Some(i) = index(index_ptr) else {
        return DelugePeaksColumn {
            pending: true,
            ..Default::default()
        };
    };
    match i.column(first, end) {
        ColumnPlan::Pending => DelugePeaksColumn {
            pending: true,
            ..Default::default()
        },
        ColumnPlan::Ready { interior, reads } => {
            let mut c = DelugePeaksColumn {
                has_interior: interior.is_some(),
                interior: interior.map(DelugePeak::from).unwrap_or_default(),
                read_count: reads.count,
                ..Default::default()
            };
            for (n, &(lo, hi)) in reads
                .ranges
                .iter()
                .enumerate()
                .take(usize::from(reads.count))
            {
                c.read_lo[n] = lo;
                c.read_hi[n] = hi;
            }
            c
        }
    }
}

/// `deluge_peaks_of_frames`.
///
/// # Safety
/// `bytes` points at `length` readable bytes, or is null.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
pub unsafe extern "C" fn deluge_peaks_of_frames(
    bytes: *const u8,
    length: u32,
    byte_depth: u8,
) -> DelugeMaybePeak {
    if bytes.is_null() {
        return DelugeMaybePeak::default();
    }
    // SAFETY: the caller's contract.
    let slice = unsafe { core::slice::from_raw_parts(bytes, length as usize) };
    let p = peak::peak_of_frames(slice, byte_depth);
    maybe((!p.is_empty()).then_some(p))
}

/// `deluge_peaks_range`.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub extern "C" fn deluge_peaks_range(index_ptr: *const DelugePeakIndex) -> DelugeMaybePeak {
    maybe(index(index_ptr).and_then(PeakIndex::range))
}

/// `deluge_peaks_frames`.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub extern "C" fn deluge_peaks_frames(index_ptr: *const DelugePeakIndex) -> u64 {
    index(index_ptr).map_or(0, PeakIndex::frames)
}

/// `deluge_peaks_next_unscanned`.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub extern "C" fn deluge_peaks_next_unscanned(index_ptr: *const DelugePeakIndex, from: u64) -> u64 {
    index(index_ptr)
        .and_then(|i| i.next_unscanned(from))
        .unwrap_or(u64::MAX)
}

/// `deluge_peaks_fill_uniform` (`include/test/peaks_test_support.h`).
#[cfg(feature = "test_support")]
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
pub extern "C" fn deluge_peaks_fill_uniform(index_ptr: *mut DelugePeakIndex, peak: DelugePeak) {
    if let Some(i) = index(index_ptr) {
        i.fill_uniform(peak.into());
    }
}

/// The C name of a [`take::Take`](crate::take::Take): opaque.
#[repr(C)]
pub struct DelugePeaksTake {
    _opaque: [u8; 0],
}

fn take<'a>(p: *mut DelugePeaksTake) -> Option<&'a mut crate::take::Take> {
    // SAFETY: a non-null `DelugePeaksTake*` came from `deluge_peaks_take_create` and is not yet dropped; the recorder
    // is its only user.
    NonNull::new(p.cast::<crate::take::Take>()).map(|mut p| unsafe { p.as_mut() })
}

/// `deluge_peaks_take_create`.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub extern "C" fn deluge_peaks_take_create() -> *mut DelugePeaksTake {
    let Some(p) = mem::alloc_zeroed::<crate::take::Take>(1) else {
        return core::ptr::null_mut();
    };
    // SAFETY: `p` is memory for one `Take`, which `write` initialises without reading.
    unsafe { p.as_ptr().write(crate::take::Take::default()) };
    p.as_ptr().cast::<DelugePeaksTake>()
}

/// `deluge_peaks_take_drop`.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub extern "C" fn deluge_peaks_take_drop(take_ptr: *mut DelugePeaksTake) {
    if let Some(p) = NonNull::new(take_ptr.cast::<crate::take::Take>()) {
        mem::free(p);
    }
}

/// `deluge_peaks_take_feed`.
///
/// # Safety
/// `bytes` points at `length` readable bytes, or is null.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub unsafe extern "C" fn deluge_peaks_take_feed(
    take_ptr: *mut DelugePeaksTake,
    index_ptr: *mut DelugePeakIndex,
    bytes: *const u8,
    length: u32,
    audio_start: u32,
    channels: u8,
) {
    let Some(t) = take(take_ptr) else {
        return;
    };
    if bytes.is_null() {
        return;
    }
    // SAFETY: the caller's contract.
    let slice = unsafe { core::slice::from_raw_parts(bytes, length as usize) };
    t.feed(index(index_ptr), slice, audio_start, channels);
}

/// `deluge_peaks_take_finish`.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub extern "C" fn deluge_peaks_take_finish(
    take_ptr: *mut DelugePeaksTake,
    index_ptr: *mut DelugePeakIndex,
) {
    if let Some(t) = take(take_ptr) {
        t.finish(index(index_ptr));
    }
}

/// `deluge_peaks_take_frames`.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub extern "C" fn deluge_peaks_take_frames(take_ptr: *mut DelugePeaksTake) -> u64 {
    take(take_ptr).map_or(0, |t| t.frames())
}

/// `deluge_peaks_widen_range` (`include/test/peaks_test_support.h`).
#[cfg(feature = "test_support")]
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
pub extern "C" fn deluge_peaks_widen_range(index_ptr: *mut DelugePeakIndex, peak: DelugePeak) {
    if let Some(i) = index(index_ptr) {
        i.widen_range(peak.into());
    }
}

/// `DelugePeakKey`: [`sidecar::Key`] as C sees it.
pub type DelugePeakKey = sidecar::Key;
// The header's layout is checked field by field by the device build's `abi_layout!`.

/// `DelugeSidecarSection`: a section other than the peaks, carried byte for byte.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct DelugeSidecarSection {
    /// Four ASCII bytes, little-endian.
    pub tag: u32,
    /// Its bytes; null only when `len` is 0.
    pub bytes: *const u8,
    /// Their count.
    pub len: u32,
}

/// The caller's `extras` as sections, or `None` if there are too many or one is malformed.
///
/// # Safety
///
/// `extras` points at `count` readable sections whose `bytes` point at `len` readable bytes each, or is null with
/// `count` 0.
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
unsafe fn sections<'a>(
    extras: *const DelugeSidecarSection,
    count: u32,
) -> Option<([sidecar::Section<'a>; sidecar::MAX_SECTIONS - 1], usize)> {
    let n = usize::try_from(count).ok()?;
    let mut out = [sidecar::Section { tag: 0, bytes: &[] }; sidecar::MAX_SECTIONS - 1];
    if n == 0 {
        return Some((out, 0));
    }
    if extras.is_null() || n > out.len() {
        return None;
    }
    // SAFETY: the caller's contract.
    let given = unsafe { core::slice::from_raw_parts(extras, n) };
    for (slot, s) in out.iter_mut().zip(given) {
        if s.tag == sidecar::TAG_PEAK || (s.bytes.is_null() && s.len != 0) {
            return None;
        }
        let bytes = if s.len == 0 {
            &[][..]
        } else {
            // SAFETY: the caller's contract.
            unsafe { core::slice::from_raw_parts(s.bytes, s.len as usize) }
        };
        *slot = sidecar::Section { tag: s.tag, bytes };
    }
    Some((out, n))
}

/// `bytes` as a slice, or `None` if null.
///
/// # Safety
///
/// `bytes` points at `len` readable bytes, or is null.
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
unsafe fn byte_slice<'a>(bytes: *const u8, len: u32) -> Option<&'a [u8]> {
    // SAFETY: the caller's contract.
    (!bytes.is_null()).then(|| unsafe { core::slice::from_raw_parts(bytes, len as usize) })
}

/// The sidecar `bytes` checked against `key`.
///
/// # Safety
///
/// As [`byte_slice`]; `key` is null or points at a readable key.
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
unsafe fn checked<'a>(
    bytes: *const u8,
    len: u32,
    key: *const DelugePeakKey,
) -> Result<sidecar::Sidecar<'a>, i32> {
    // SAFETY: the caller's contract.
    let (Some(b), Some(k)) = (unsafe { byte_slice(bytes, len) }, unsafe { key.as_ref() }) else {
        return Err(-(sidecar::Reject::Short as i32));
    };
    sidecar::open(b, k).map_err(|r| -(r as i32))
}

/// `deluge_peaks_sidecar_len`.
///
/// # Safety
///
/// As [`sections`].
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub unsafe extern "C" fn deluge_peaks_sidecar_len(
    index_ptr: *const DelugePeakIndex,
    extras: *const DelugeSidecarSection,
    extra_count: u32,
) -> u32 {
    // SAFETY: the caller's contract.
    let Some((s, n)) = (unsafe { sections(extras, extra_count) }) else {
        return 0;
    };
    index(index_ptr)
        .and_then(|i| sidecar::len(i, s.get(..n)?))
        .and_then(|l| u32::try_from(l).ok())
        .unwrap_or(0)
}

/// `deluge_peaks_sidecar_write`.
///
/// # Safety
///
/// As [`sections`]; `key` is null or readable; `out` is null or points at `cap` writable bytes.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub unsafe extern "C" fn deluge_peaks_sidecar_write(
    index_ptr: *const DelugePeakIndex,
    key: *const DelugePeakKey,
    extras: *const DelugeSidecarSection,
    extra_count: u32,
    out: *mut u8,
    cap: u32,
) -> u32 {
    // SAFETY: the caller's contract.
    let Some((s, n)) = (unsafe { sections(extras, extra_count) }) else {
        return 0;
    };
    // SAFETY: the caller's contract.
    let (Some(i), Some(k)) = (index(index_ptr), unsafe { key.as_ref() }) else {
        return 0;
    };
    if out.is_null() {
        return 0;
    }
    // SAFETY: the caller's contract.
    let buf = unsafe { core::slice::from_raw_parts_mut(out, cap as usize) };
    s.get(..n)
        .and_then(|s| sidecar::write(i, k, s, buf))
        .and_then(|l| u32::try_from(l).ok())
        .unwrap_or(0)
}

/// `deluge_peaks_sidecar_check`.
///
/// # Safety
///
/// As [`checked`].
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub unsafe extern "C" fn deluge_peaks_sidecar_check(
    bytes: *const u8,
    len: u32,
    key: *const DelugePeakKey,
) -> i32 {
    // SAFETY: the caller's contract.
    match unsafe { checked(bytes, len, key) } {
        Ok(_) => 0,
        Err(r) => r,
    }
}

/// `deluge_peaks_sidecar_restore`.
///
/// # Safety
///
/// As [`checked`].
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub unsafe extern "C" fn deluge_peaks_sidecar_restore(
    index_ptr: *mut DelugePeakIndex,
    key: *const DelugePeakKey,
    bytes: *const u8,
    len: u32,
) -> i32 {
    let Some(i) = index(index_ptr) else {
        return -(sidecar::Reject::Short as i32);
    };
    // SAFETY: the caller's contract.
    match unsafe { checked(bytes, len, key) }
        .and_then(|s| sidecar::restore_peaks(i, &s).map_err(|r| -(r as i32)))
    {
        Ok(n) => i32::try_from(n).unwrap_or(i32::MAX),
        Err(r) => r,
    }
}

/// `deluge_peaks_sidecar_section`.
///
/// # Safety
///
/// As [`checked`]; `out_bytes` and `out_len` are null or writable.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub unsafe extern "C" fn deluge_peaks_sidecar_section(
    bytes: *const u8,
    len: u32,
    key: *const DelugePeakKey,
    tag: u32,
    out_bytes: *mut *const u8,
    out_len: *mut u32,
) -> bool {
    // SAFETY: the caller's contract.
    let Some(section) = (unsafe { checked(bytes, len, key) })
        .ok()
        .and_then(|s| s.section(tag))
    else {
        return false;
    };
    let Ok(n) = u32::try_from(section.len()) else {
        return false;
    };
    if out_bytes.is_null() || out_len.is_null() {
        return false;
    }
    // SAFETY: both are non-null and writable (the caller's contract).
    unsafe {
        out_bytes.write(section.as_ptr());
        out_len.write(n);
    }
    true
}

/// `deluge_peaks_complete_level1`.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub extern "C" fn deluge_peaks_complete_level1(index_ptr: *const DelugePeakIndex) -> u32 {
    index(index_ptr).map_or(0, |i| {
        u32::try_from(i.complete_level1()).unwrap_or(u32::MAX)
    })
}

/// `deluge_peaks_level1_entries`.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub extern "C" fn deluge_peaks_level1_entries(index_ptr: *const DelugePeakIndex) -> u32 {
    index(index_ptr).map_or(0, |i| u32::try_from(i.level1_entries()).unwrap_or(u32::MAX))
}

/// `deluge_peaks_keep_level0`.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub extern "C" fn deluge_peaks_keep_level0(
    index_ptr: *mut DelugePeakIndex,
    first: u64,
    end: u64,
    now_ms: u32,
) {
    if let Some(i) = index(index_ptr) {
        i.keep_level0(first, end, now_ms);
    }
}

/// `deluge_peaks_trim_level0`.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub extern "C" fn deluge_peaks_trim_level0(index_ptr: *mut DelugePeakIndex, now_ms: u32) -> u32 {
    index(index_ptr).map_or(0, |i| i.trim_level0(now_ms))
}

/// `deluge_peaks_level0_chunks`.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub extern "C" fn deluge_peaks_level0_chunks(index_ptr: *const DelugePeakIndex) -> u32 {
    index(index_ptr).map_or(0, PeakIndex::level0_chunks)
}
