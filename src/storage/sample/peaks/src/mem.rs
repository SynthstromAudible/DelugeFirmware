//! The index's memory: two C callbacks the app installs at boot. Only thread mode allocates or frees.

use core::ptr::NonNull;
use core::sync::atomic::{AtomicPtr, Ordering};

/// Allocates `size` bytes aligned to `align`; null on failure.
pub type AllocFn = unsafe extern "C" fn(size: usize, align: usize) -> *mut u8;
/// Frees what [`AllocFn`] returned.
pub type FreeFn = unsafe extern "C" fn(ptr: *mut u8);

static ALLOC: AtomicPtr<()> = AtomicPtr::new(core::ptr::null_mut());
static FREE: AtomicPtr<()> = AtomicPtr::new(core::ptr::null_mut());

/// Installs the callbacks. Call once at boot, before any index is made.
pub fn install(alloc: AllocFn, free: FreeFn) {
    ALLOC.store(alloc as *mut (), Ordering::Release);
    FREE.store(free as *mut (), Ordering::Release);
}

/// `n` zeroed `T`s, or `None` if no allocator is installed, the size overflows or the allocation fails. `T` must be
/// valid when all-zero (the atomics, an encoded peak and an index's fields are).
pub(crate) fn alloc_zeroed<T>(n: usize) -> Option<NonNull<T>> {
    let size = core::mem::size_of::<T>().checked_mul(n)?;
    let f = ALLOC.load(Ordering::Acquire);
    if f.is_null() || size == 0 {
        return None;
    }
    // SAFETY: a non-null `ALLOC` was stored from an `AllocFn` by `install`.
    let alloc: AllocFn = unsafe { core::mem::transmute::<*mut (), AllocFn>(f) };
    // SAFETY: the callback's contract: null, or `size` writable bytes aligned to `align_of::<T>()`.
    let p = unsafe { alloc(size, core::mem::align_of::<T>()) };
    let p = NonNull::new(p.cast::<T>())?;
    // SAFETY: `p` is `size` writable bytes.
    unsafe { core::ptr::write_bytes(p.as_ptr().cast::<u8>(), 0, size) };
    Some(p)
}

/// Frees memory from [`alloc_zeroed`].
pub(crate) fn free<T>(p: NonNull<T>) {
    let f = FREE.load(Ordering::Acquire);
    if f.is_null() {
        return;
    }
    // SAFETY: a non-null `FREE` was stored from a `FreeFn` by `install`.
    let free: FreeFn = unsafe { core::mem::transmute::<*mut (), FreeFn>(f) };
    // SAFETY: `p` came from the matching `AllocFn`.
    unsafe { free(p.as_ptr().cast::<u8>()) };
}

/// `aligned_alloc`/`free` for host tests.
#[cfg(feature = "test_support")]
pub mod host {
    unsafe extern "C" {
        fn aligned_alloc(align: usize, size: usize) -> *mut core::ffi::c_void;
        fn free(p: *mut core::ffi::c_void);
    }

    unsafe extern "C" fn host_alloc(size: usize, align: usize) -> *mut u8 {
        let align = align.max(core::mem::size_of::<usize>());
        let size = size.div_ceil(align) * align;
        // SAFETY: C11 `aligned_alloc` with a size that is a multiple of the alignment.
        unsafe { aligned_alloc(align, size).cast::<u8>() }
    }

    unsafe extern "C" fn host_free(p: *mut u8) {
        // SAFETY: `p` came from `aligned_alloc`.
        unsafe { free(p.cast::<core::ffi::c_void>()) }
    }

    /// Installs `aligned_alloc`/`free`.
    pub fn install() {
        super::install(host_alloc, host_free);
    }
}
