//! Generates `libdeluge_sys.rs` and `libdeluge_layout.rs` from the libdeluge headers.

use std::fs;
use std::path::Path;

use super::{allowlist, crate_types};

/// Generates the whole libdeluge C ABI (`include/libdeluge/*.h`) via bindgen
/// for `clang_target`, writing `libdeluge_sys.rs` into `out_dir`: the POD
/// types, the `DELUGE_*` constants, and an `extern "C"` declaration for every
/// header function. The crate DEFINES the service functions itself
/// (`#[no_mangle]`); the generated declarations are what those definitions are
/// checked against, so a header change is a compile error rather than a
/// silent ABI drift. Shared by the device path (`--target=armv7a-none-eabihf`)
/// and the `host_app` path (`--target=x86_64-unknown-linux-gnu`) — same
/// allowlist/flags otherwise, so the two `mod sys`es stay structurally
/// identical modulo target.
///
/// The header types an in-tree crate defines are bound to the crate's own
/// types, and their header layouts are written to `libdeluge_layout.rs`
/// instead: see `build_crate_types.rs`.
///
/// `deluge_host` defines `DELUGE_HOST`, as the host app's CMake build does, so
/// the host-only declarations (latency knobs, the host trigger-clock edge) are
/// bound for the `host_app` carrier and absent from the device one.
///
/// # Why no `-fshort-enums`
///
/// It is deliberately absent from both bindgen paths, and irrelevant to enum
/// sizing here: all 11 libdeluge FFI enums pin their underlying type
/// explicitly in the header (`enum DelugeStatus : int8_t`, and so on). An
/// explicit underlying type is authoritative in both C and C++ — no flag or
/// ABI default overrides it — so bindgen sizes them identically on every
/// target.
///
/// That explicitness is load-bearing, not incidental. Without it the arm
/// device (which defaults to short enums) and an un-flagged bindgen target
/// would silently disagree on enum width, mislaying every enum-bearing POD
/// across the FFI boundary. `--target` is passed purely for pointer width,
/// alignment and calling convention.
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
            // Layouts match the app being linked on every target (explicit
            // fixed-width enums throughout), and the asserts would run host-side
            // anyway.
            .layout_tests(false);
        if deluge_host {
            builder.clang_arg("-DDELUGE_HOST")
        } else {
            builder
        }
    };
    let builder = base()
        .allowlist_type("Deluge.*")
        // Every function and constant too: `abi_pin!` and `abi_const!` check the Rust side against these.
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
/// bindgen 0.70 predates `unsafe extern` and emits the bare form.
fn edition_2024_externs(bindings: &str) -> String {
    bindings.replace("\nextern \"C\" {", "\nunsafe extern \"C\" {")
}
