//! The C ABI in `deluge_storage_resource.h`: one shim per declaration, in the header's own types,
//! forwarding to its typed function in [`crate::manager`]. The handle is untyped (`c_void`), as
//! bindgen binds it, and a chunk pointer is `void*`; the typed functions take the crate's
//! [`DelugeResource`] and `*mut u8`. `abi_pin!` checks each shim against the header.
//!
//! The safety contract of each shim is its typed function's.

use core::ffi::c_void;
use core::mem::transmute;

use deluge_base_alloc::slab::DelugeSlab;
use deluge_base_alloc::DelugeHeap;

use crate::manager::*;

/// `DelugeResourceMaterializeFn`, as bindgen generates it.
pub type DelugeResourceMaterializeFn = Option<
    unsafe extern "C" fn(
        ctx: *mut c_void,
        owner: *mut c_void,
        index: u32,
        dest: *mut c_void,
        len: usize,
    ) -> bool,
>;
/// `DelugeResourceEvictFn`, as bindgen generates it.
pub type DelugeResourceEvictFn = Option<EvictFn>;
/// `DelugeResourceConstructFn`, as bindgen generates it.
pub type DelugeResourceConstructFn = Option<
    unsafe extern "C" fn(ctx: *mut c_void, owner: *mut c_void, index: u32, dest: *mut c_void),
>;
/// `DelugeResourceAdoptEvictFn`, as bindgen generates it.
pub type DelugeResourceAdoptEvictFn =
    Option<unsafe extern "C" fn(ctx: *mut c_void, ptr: *mut c_void)>;

/// [`resource_create`] in the header's types.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_create(
    heap: *mut DelugeHeap,
    asset_capacity: usize,
    chunk_capacity: usize,
) -> *mut c_void {
    resource_create(heap, asset_capacity, chunk_capacity).cast()
}

/// [`resource_set_slab`] in the header's types.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_set_slab(mgr: *mut c_void, slab: *mut DelugeSlab) {
    resource_set_slab(mgr.cast(), slab)
}

/// [`resource_define_asset`] in the header's types.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_define_asset(
    mgr: *mut c_void,
    owner: *mut c_void,
    materialize: DelugeResourceMaterializeFn,
    on_evict: DelugeResourceEvictFn,
    ctx: *mut c_void,
    cost: u32,
    backing: u32,
) -> u32 {
    resource_define_asset(
        mgr.cast(),
        owner,
        // SAFETY: same arity and `extern "C"` convention; only `dest`'s pointee differs.
        transmute::<DelugeResourceMaterializeFn, Option<MaterializeFn>>(materialize),
        on_evict,
        ctx,
        cost,
        backing,
    )
}

/// [`resource_release_asset`] in the header's types.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_release_asset(mgr: *mut c_void, asset: u32) {
    resource_release_asset(mgr.cast(), asset)
}

/// [`resource_set_construct`] in the header's types.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_set_construct(
    mgr: *mut c_void,
    asset: u32,
    construct: DelugeResourceConstructFn,
) {
    resource_set_construct(
        mgr.cast(),
        asset,
        // SAFETY: same arity and `extern "C"` convention; only `dest`'s pointee differs.
        transmute::<DelugeResourceConstructFn, Option<ConstructFn>>(construct),
    )
}

/// [`resource_set_evict_tail_first`] in the header's types.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_set_evict_tail_first(
    mgr: *mut c_void,
    asset: u32,
    on: bool,
) {
    resource_set_evict_tail_first(mgr.cast(), asset, on)
}

/// [`resource_set_self_protect`] in the header's types.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_set_self_protect(mgr: *mut c_void, asset: u32, on: bool) {
    resource_set_self_protect(mgr.cast(), asset, on)
}

/// [`resource_request`] in the header's types.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_request(
    mgr: *mut c_void,
    asset: u32,
    index: u32,
    size: usize,
) -> *mut c_void {
    resource_request(mgr.cast(), asset, index, size).cast()
}

/// [`resource_acquire`] in the header's types.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_acquire(
    mgr: *mut c_void,
    asset: u32,
    index: u32,
    size: usize,
) -> *mut c_void {
    resource_acquire(mgr.cast(), asset, index, size).cast()
}

/// [`resource_try_acquire`] in the header's types.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_try_acquire(
    mgr: *mut c_void,
    asset: u32,
    index: u32,
) -> *mut c_void {
    resource_try_acquire(mgr.cast(), asset, index).cast()
}

/// [`resource_mark_ready`] in the header's types.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_mark_ready(mgr: *mut c_void, ptr: *mut c_void) {
    resource_mark_ready(mgr.cast(), ptr.cast())
}

/// [`resource_slot_of`] in the header's types.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_slot_of(mgr: *mut c_void, ptr: *mut c_void) -> u32 {
    resource_slot_of(mgr.cast(), ptr.cast())
}

/// [`resource_chunk_ident`] in the header's types.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_chunk_ident(
    mgr: *mut c_void,
    ptr: *mut c_void,
    out_asset: *mut u32,
    out_index: *mut u32,
) -> bool {
    resource_chunk_ident(mgr.cast(), ptr.cast(), out_asset, out_index)
}

/// [`resource_lease_count_by_slot`] in the header's types.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_lease_count_by_slot(mgr: *mut c_void, slot: u32) -> u32 {
    resource_lease_count_by_slot(mgr.cast(), slot)
}

/// [`resource_external_lease_count_by_slot`] in the header's types.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_external_lease_count_by_slot(
    mgr: *mut c_void,
    slot: u32,
) -> u32 {
    resource_external_lease_count_by_slot(mgr.cast(), slot)
}

/// [`resource_loader_enqueue`] in the header's types.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_loader_enqueue(
    mgr: *mut c_void,
    slot: u32,
    priority: u32,
) {
    resource_loader_enqueue(mgr.cast(), slot, priority)
}

/// [`resource_loader_enqueue_owned`] in the header's types.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_loader_enqueue_owned(
    mgr: *mut c_void,
    slot: u32,
    priority: u32,
) {
    resource_loader_enqueue_owned(mgr.cast(), slot, priority)
}

/// [`resource_loader_release_owned`] in the header's types.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_loader_release_owned(mgr: *mut c_void, slot: u32) -> bool {
    resource_loader_release_owned(mgr.cast(), slot)
}

/// [`resource_loader_next`] in the header's types.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_loader_next(mgr: *mut c_void) -> *mut c_void {
    resource_loader_next(mgr.cast()).cast()
}

/// [`resource_loader_has_lowest`] in the header's types.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_loader_has_lowest(mgr: *mut c_void) -> bool {
    resource_loader_has_lowest(mgr.cast())
}

/// [`resource_loader_has_any`] in the header's types.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_loader_has_any(mgr: *mut c_void) -> bool {
    resource_loader_has_any(mgr.cast())
}

/// [`resource_stats`] in the header's types.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_stats(mgr: *mut c_void, out: *mut Stats) {
    resource_stats(mgr.cast(), out)
}

/// [`resource_stats_reset`] in the header's types.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_stats_reset(mgr: *mut c_void) {
    resource_stats_reset(mgr.cast())
}

/// [`resource_adopt`] in the header's types.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_adopt(
    mgr: *mut c_void,
    ptr: *mut c_void,
    size: usize,
    cost: u32,
    ctx: *mut c_void,
    on_evict: DelugeResourceAdoptEvictFn,
) -> *mut c_void {
    resource_adopt(
        mgr.cast(),
        ptr.cast(),
        size,
        cost,
        ctx,
        // SAFETY: same arity and `extern "C"` convention; only `ptr`'s pointee differs.
        transmute::<DelugeResourceAdoptEvictFn, Option<AdoptEvictFn>>(on_evict),
    )
    .cast()
}

/// [`resource_add_lease`] in the header's types.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_add_lease(mgr: *mut c_void, ptr: *mut c_void) {
    resource_add_lease(mgr.cast(), ptr.cast())
}

/// [`resource_evict_chunk`] in the header's types.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_evict_chunk(mgr: *mut c_void, ptr: *mut c_void) {
    resource_evict_chunk(mgr.cast(), ptr.cast())
}

/// [`resource_release`] in the header's types.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_release(mgr: *mut c_void, ptr: *mut c_void) {
    resource_release(mgr.cast(), ptr.cast())
}

/// [`resource_reference`] in the header's types.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_reference(mgr: *mut c_void, asset: u32) {
    resource_reference(mgr.cast(), asset)
}

/// [`resource_unreference`] in the header's types.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_unreference(mgr: *mut c_void, asset: u32) {
    resource_unreference(mgr.cast(), asset)
}

/// [`resource_touch`] in the header's types.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_touch(mgr: *mut c_void, ptr: *mut c_void) {
    resource_touch(mgr.cast(), ptr.cast())
}

/// [`resource_mark_dirty`] in the header's types.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_mark_dirty(
    mgr: *mut c_void,
    ptr: *mut c_void,
    dirty: bool,
) {
    resource_mark_dirty(mgr.cast(), ptr.cast(), dirty)
}
