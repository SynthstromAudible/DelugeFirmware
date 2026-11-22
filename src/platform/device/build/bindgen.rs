//! Generates `libdeluge_sys.rs` and `libdeluge_layout.rs` from the libdeluge headers.

use std::fs;
use std::path::Path;

use super::{allowlist, crate_types};

/// Binds the libdeluge headers for `clang_target`, writing `libdeluge_sys.rs` into `out_dir`: the
/// POD types, the `DELUGE_*` constants, and a declaration of every header function.
///
/// The crate defines the service functions itself; the generated declarations are what `abi_pin!`
/// checks those definitions against, so a header change is a compile error rather than silent ABI
/// drift. The header types an in-tree crate defines bind to that crate's types, and their header
/// layouts go to `libdeluge_layout.rs` instead (see [`crate_types`]).
///
/// `deluge_host` defines `DELUGE_HOST`, as the host app's CMake build does, which binds the
/// host-only declarations.
///
/// No `-fshort-enums`: every libdeluge enum declares its underlying type (`enum DelugeStatus :
/// int8_t`), which no flag or ABI default overrides. That is load-bearing: the arm device defaults
/// to short enums, and an unsized enum would differ in width between the two sides.
pub fn run(
    repo_root: &Path,
    manifest_dir: &Path,
    out_dir: &Path,
    clang_target: &str,
    deluge_host: bool,
) {
    let include_dir = repo_root.join("include");
    let wrapper = manifest_dir.join("wrapper.h");
    let base = || {
        let builder = bindgen::Builder::default()
            .header(wrapper.to_str().unwrap())
            .clang_arg(format!("-I{}", include_dir.display()))
            .use_core()
            .clang_arg(format!("--target={clang_target}"))
            // `abi_layout!` checks the layouts that matter, on the target itself.
            .layout_tests(false);
        if deluge_host {
            builder.clang_arg("-DDELUGE_HOST")
        } else {
            builder
        }
    };
    let builder = base()
        .allowlist_type("Deluge.*")
        // Every function and constant too: `abi_pin!` and `abi_const!` check the Rust side against
        // these.
        .allowlist_function("deluge_.*")
        .allowlist_function(allowlist::SCHEDULER_API_FUNCTIONS)
        .allowlist_function(allowlist::UNPREFIXED_FUNCTIONS)
        .allowlist_var("DELUGE_.*");
    let bindings = crate_types::bind_crate_types(builder, !deluge_host)
        .generate()
        .expect("bindgen failed on libdeluge headers");
    fs::write(
        out_dir.join("libdeluge_sys.rs"),
        edition_2024_externs(&bindings.to_string()),
    )
    .expect("write libdeluge_sys.rs");
    let layouts = crate_types::crate_type_layouts(base())
        .generate()
        .expect("bindgen failed on the crate-defined libdeluge types");
    fs::write(out_dir.join("libdeluge_layout.rs"), layouts.to_string())
        .expect("write libdeluge_layout.rs");
    println!("cargo:rerun-if-changed={}", wrapper.display());
    println!("cargo:rerun-if-changed={}", include_dir.display());
    println!("cargo:rerun-if-changed=build/allowlist.rs");
    println!("cargo:rerun-if-changed=build/crate_types.rs");
}

/// Marks each generated `extern "C"` block `unsafe`, as edition 2024 requires.
/// bindgen 0.70 predates `unsafe extern` and emits the bare form. Its output is one line when
/// rustfmt is unavailable, so the match cannot rely on a block starting a line; any block already
/// marked is unmarked first, so none ends up marked twice.
fn edition_2024_externs(bindings: &str) -> String {
    bindings
        .replace("unsafe extern \"C\" {", "extern \"C\" {")
        .replace("extern \"C\" {", "unsafe extern \"C\" {")
}
