//! The `libdeluge/alloc.h` exports whose C types differ from this crate's Rust API: a block is a `void*` in C and a
//! `*mut u8` here, and C's reclaim hook may be NULL. Each forwards to the function of the same name at the crate root
//! or in [`crate::slab`]. The exports whose types agree are those functions themselves.

use core::ffi::c_void;

use crate::slab::{self, DelugeSlab};
use crate::tlsf::ReclaimFn;
use crate::DelugeHeap;

#[no_mangle]
pub unsafe extern "C" fn deluge_heap_create(base: *mut c_void, size: usize) -> *mut DelugeHeap {
    crate::deluge_heap_create(base.cast(), size)
}

#[no_mangle]
pub unsafe extern "C" fn deluge_alloc(
    h: *mut DelugeHeap,
    size: usize,
    align: usize,
) -> *mut c_void {
    crate::deluge_alloc(h, size, align).cast()
}

/// A NULL `cb` registers nothing.
#[no_mangle]
pub unsafe extern "C" fn deluge_heap_register_reclaim(
    h: *mut DelugeHeap,
    cb: Option<ReclaimFn>,
    ctx: *mut c_void,
) {
    if let Some(cb) = cb {
        crate::deluge_heap_register_reclaim(h, cb, ctx);
    }
}

#[no_mangle]
pub unsafe extern "C" fn deluge_free(h: *mut DelugeHeap, p: *mut c_void) {
    crate::deluge_free(h, p.cast());
}

#[no_mangle]
pub unsafe extern "C" fn deluge_realloc(
    h: *mut DelugeHeap,
    p: *mut c_void,
    new_size: usize,
    align: usize,
) -> *mut c_void {
    crate::deluge_realloc(h, p.cast(), new_size, align).cast()
}

#[no_mangle]
pub unsafe extern "C" fn deluge_usable_size(h: *mut DelugeHeap, p: *mut c_void) -> usize {
    crate::deluge_usable_size(h, p.cast())
}

#[no_mangle]
pub unsafe extern "C" fn deluge_slab_acquire(
    s: *mut DelugeSlab,
    owner: *mut c_void,
) -> *mut c_void {
    slab::deluge_slab_acquire(s, owner).cast()
}

#[no_mangle]
pub unsafe extern "C" fn deluge_slab_pin(s: *mut DelugeSlab, p: *mut c_void) {
    slab::deluge_slab_pin(s, p.cast());
}

#[no_mangle]
pub unsafe extern "C" fn deluge_slab_unpin(s: *mut DelugeSlab, p: *mut c_void) {
    slab::deluge_slab_unpin(s, p.cast());
}

#[no_mangle]
pub unsafe extern "C" fn deluge_slab_touch(s: *mut DelugeSlab, p: *mut c_void) {
    slab::deluge_slab_touch(s, p.cast());
}

#[no_mangle]
pub unsafe extern "C" fn deluge_slab_release(s: *mut DelugeSlab, p: *mut c_void) -> bool {
    slab::deluge_slab_release(s, p.cast())
}
