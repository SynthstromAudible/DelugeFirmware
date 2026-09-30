//! The checking `abi_pin!`/`abi_const!`/`abi_layout!`, for every crate whose `sys` is the bindgen output: this crate
//! on device and under `host_app`, and `harness/golden_vt_render`. Include it with `#[macro_use]` before the first
//! pinned module, because `macro_rules!` is textually scoped. A crate with no bindgen output includes the no-ops in
//! `tests/support/abi_pin_noop.rs` instead.
#![allow(unused_macros)]

/// Fails the build unless each named export has exactly the signature bindgen generated from its header. The
/// export and the generated declaration go into one array, which compiles only if both coerce to the same `fn`
/// pointer type. A safe export matches an `unsafe` declaration; a different parameter, return type or pointer
/// mutability does not (E0308).
///
/// - `abi_pin!(a, b)` pins `a` and `b` in scope at the invocation.
/// - `abi_pin!(in path::to::module: a, b)` pins `a` and `b` defined in that module: another crate's exports, or a
///   crate's own `extern "C"` declaration of a header function.
/// - `abi_pin!(sys_name => local_name)` pins an export whose Rust name is not its C name: `r#yield`, which bindgen
///   declares as `yield_` because `yield` is a Rust keyword.
macro_rules! abi_pin {
    ($sys:ident => $local:ident) => {
        const _: () = { let _ = [crate::sys::$sys, $local]; };
    };
    (in $($m:ident)::+ : $($f:ident),+ $(,)?) => {
        const _: () = {
            use $($m)::+ as pinned;
            $( let _ = [crate::sys::$f, pinned::$f]; )+
        };
    };
    ($($f:ident),+ $(,)?) => { $(
        const _: () = { let _ = [crate::sys::$f, $f]; };
    )+ };
}

/// Fails the build unless each constant copied on the Rust side equals the header's. Both sides are widened to
/// `i64`, so a copy may use the width its module needs.
macro_rules! abi_const {
    ($($ours:expr => $theirs:ident),+ $(,)?) => { $(
        const _: () = assert!(($ours) as i64 == crate::sys::$theirs as i64, stringify!($theirs));
    )+ };
}

/// Fails the build unless a crate's definition of a header type has the header's layout (`sys_layout`, see
/// `build_crate_types.rs`).
///
/// - `abi_layout!(Ours => Theirs: a, b)` checks size, alignment, and the offset of each named field.
/// - `abi_layout!(enum Ours => Theirs: A = CONST_A, B = CONST_B)` checks size, and each variant's value against the
///   header's constant.
macro_rules! abi_layout {
    ($ours:ty => $theirs:ident $(: $($field:ident),+)? $(,)?) => {
        const _: () = {
            use core::mem::{align_of, size_of};
            type Theirs = crate::sys_layout::$theirs;
            assert!(size_of::<$ours>() == size_of::<Theirs>(), concat!(stringify!($theirs), ": size"));
            assert!(align_of::<$ours>() == align_of::<Theirs>(), concat!(stringify!($theirs), ": alignment"));
            $($(
                assert!(
                    core::mem::offset_of!($ours, $field) == core::mem::offset_of!(Theirs, $field),
                    concat!(stringify!($theirs), ".", stringify!($field)),
                );
            )+)?
        };
    };
    (enum $ours:ty => $theirs:ident : $($variant:ident = $value:ident),+ $(,)?) => {
        const _: () = {
            use core::mem::size_of;
            assert!(
                size_of::<$ours>() == size_of::<crate::sys_layout::$theirs>(),
                concat!(stringify!($theirs), ": size"),
            );
            $(
                assert!(
                    <$ours>::$variant as i64 == crate::sys_layout::$value as i64,
                    stringify!($value),
                );
            )+
        };
    };
}
