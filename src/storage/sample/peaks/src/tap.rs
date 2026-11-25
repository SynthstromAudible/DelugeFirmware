//! The fill tap's way to a sample's index: a table keyed by stream asset id.

use crate::PeakIndex;
use core::ptr::NonNull;
use core::sync::atomic::{AtomicPtr, Ordering};

/// As many asset ids as the fill-context table holds (`FILL_CONTEXT_CAP` in `deluge_storage_sample_fill`).
pub const ASSET_CAP: usize = 4096;

#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_bss"))]
static TABLE: [AtomicPtr<PeakIndex>; ASSET_CAP] =
    [const { AtomicPtr::new(core::ptr::null_mut()) }; ASSET_CAP];

/// Binds `index` to `asset`, taking a reference; releases any index bound before. Thread mode.
pub fn bind(asset: u32, index: &PeakIndex) {
    let Some(slot) = TABLE.get(asset as usize) else {
        return;
    };
    index.retain();
    let new = core::ptr::from_ref(index).cast_mut();
    let old = critical_section::with(|_| slot.swap(new, Ordering::AcqRel));
    if let Some(old) = NonNull::new(old) {
        PeakIndex::release(old);
    }
}

/// Unbinds `asset`, releasing its reference. Thread mode.
pub fn unbind(asset: u32) {
    let Some(slot) = TABLE.get(asset as usize) else {
        return;
    };
    let old = critical_section::with(|_| slot.swap(core::ptr::null_mut(), Ordering::AcqRel));
    if let Some(old) = NonNull::new(old) {
        PeakIndex::release(old);
    }
}

/// The index bound to `asset`, with a reference the caller must release. Any tier.
pub fn lookup(asset: u32) -> Option<NonNull<PeakIndex>> {
    let slot = TABLE.get(asset as usize)?;
    critical_section::with(|_| {
        let p = NonNull::new(slot.load(Ordering::Acquire))?;
        // SAFETY: a bound index holds the table's reference, and an unbind swaps it out only inside a critical
        // section, so it is alive here.
        unsafe { p.as_ref() }.retain();
        Some(p)
    })
}
