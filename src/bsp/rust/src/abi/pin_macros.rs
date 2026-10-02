//! The checking `abi_pin!`/`abi_const!`/`abi_layout!`, for the builds whose `sys` is bindgen
//! output (device and `host_app`); the plain host build gets the no-ops in `pin_noop.rs`. The
//! macros are textually scoped, so [`crate::abi`] comes first in the crate root.
#![allow(unused_macros)]

/// Fails the build unless each named export has exactly the signature bindgen generated from its
/// header. The export and the generated declaration go into one array, which compiles only if both
/// coerce to the same `fn` pointer type. A safe export matches an `unsafe` declaration; a different
/// parameter, return type or pointer mutability does not (E0308).
///
/// - `abi_pin!(a, b)` pins `a` and `b` in scope at the invocation.
/// - `abi_pin!(in path::to::module: a, b)` pins `a` and `b` defined in that module: another crate's
///   exports, or a crate's own `extern "C"` declaration of a header function.
/// - `abi_pin!(sys_name => local_name)` pins an export whose Rust name is not its C name, such as
///   one bindgen renames with a trailing underscore because the C name is a Rust keyword.
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

/// Fails the build unless each constant copied on the Rust side equals the header's. Both sides are
/// widened to `i64`, so a copy may use the width its module needs.
macro_rules! abi_const {
    ($($ours:expr => $theirs:ident),+ $(,)?) => { $(
        const _: () = assert!(($ours) as i64 == crate::sys::$theirs as i64, stringify!($theirs));
    )+ };
}

/// Fails the build unless a crate's definition of a header type is the header's (`sys_layout`, see
/// `build/crate_types.rs`), field for field or variant for variant.
///
/// - `abi_layout!(Ours => Theirs { a, b })` names every field. It checks size, alignment and each
///   offset, and builds the header's struct from ours in a closure that is never called: a header
///   field the list does not name fails E0063, and a field whose type differs from ours by so much
///   as signedness fails E0308. A second closure destructures ours by the same list, so a field
///   ours has and the header lacks fails even where it fits in the header's padding (macro hygiene
///   makes rustc word it "pattern requires `..` due to inaccessible fields"). A field whose type is
///   itself a crate-defined header type is written `a as Name`: ours must be `sys`'s binding of
///   `Name` and the header's must be `Name`, and `Name` has its own `abi_layout!`.
/// - `abi_layout!(Ours => sys Theirs { a, b })` is the same check against `sys`'s own binding of a
///   header struct, for a Rust copy of a header type that is not crate-defined (so `sys_layout` has
///   no entry for it).
/// - `abi_layout!(type Ours => sys Theirs)` checks that a Rust copy of a header typedef is `sys`'s
///   type.
/// - `abi_layout!(Ours => Theirs opaque)` is for storage the header declares only to size it: size
///   and alignment.
/// - `abi_layout!(enum Ours => Theirs { A = HEADER_A, B = HEADER_B })` checks size, each value, and
///   that the two enums have the same variants: a variant added to either side fails an exhaustive
///   match (E0004).
macro_rules! abi_layout {
    (@field $o:ident . $field:ident) => { $o.$field };
    (@field $o:ident . $field:ident as $nested:ident) => {{
        let ours: crate::sys::$nested = $o.$field;
        // SAFETY: never runs; `$nested`'s own `abi_layout!` makes the two the same layout.
        unsafe { core::mem::transmute::<crate::sys::$nested, crate::sys_layout::$nested>(ours) }
    }};
    (@struct $ours:ty => $theirs_path:path, $theirs:ident { $($field:ident $(as $nested:ident)?),+ }) => {
        const _: () = {
            use core::mem::{align_of, offset_of, size_of};
            type Ours = $ours;
            type Theirs = $theirs_path;
            assert!(size_of::<Ours>() == size_of::<Theirs>(), concat!(stringify!($theirs), ": size"));
            assert!(align_of::<Ours>() == align_of::<Theirs>(), concat!(stringify!($theirs), ": alignment"));
            $(
                assert!(
                    offset_of!(Ours, $field) == offset_of!(Theirs, $field),
                    concat!(stringify!($theirs), ".", stringify!($field)),
                );
            )+
            let _: fn(&Ours) -> Theirs = |o| Theirs { $($field: abi_layout!(@field o.$field $(as $nested)?)),+ };
            let _: fn(&Ours) = |o| {
                let Ours { $($field: _),+ } = o;
            };
        };
    };
    (type $ours:ty => sys $theirs:ident) => {
        const _: fn($ours) -> crate::sys::$theirs = |v| v;
    };
    ($ours:ty => sys $theirs:ident { $($field:ident),+ $(,)? }) => {
        abi_layout!(@struct $ours => crate::sys::$theirs, $theirs { $($field),+ });
    };
    ($ours:ty => $theirs:ident { $($field:ident $(as $nested:ident)?),+ $(,)? }) => {
        abi_layout!(@struct $ours => crate::sys_layout::$theirs, $theirs { $($field $(as $nested)?),+ });
    };
    ($ours:ty => $theirs:ident opaque) => {
        const _: () = {
            use core::mem::{align_of, size_of};
            type Theirs = crate::sys_layout::$theirs;
            assert!(size_of::<$ours>() == size_of::<Theirs>(), concat!(stringify!($theirs), ": size"));
            assert!(align_of::<$ours>() == align_of::<Theirs>(), concat!(stringify!($theirs), ": alignment"));
        };
    };
    (enum $ours:ty => $theirs:ident { $($variant:ident = $value:ident),+ $(,)? }) => {
        const _: () = {
            use core::mem::size_of;
            type Ours = $ours;
            type Theirs = crate::sys_layout::$theirs;
            assert!(size_of::<Ours>() == size_of::<Theirs>(), concat!(stringify!($theirs), ": size"));
            $(
                assert!(Ours::$variant as i64 == Theirs::$value as i64, stringify!($value));
            )+
            let _: fn(Ours) -> Theirs = |v| match v { $(Ours::$variant => Theirs::$value),+ };
            let _: fn(Theirs) -> Ours = |v| match v { $(Theirs::$value => Ours::$variant),+ };
        };
    };
}
