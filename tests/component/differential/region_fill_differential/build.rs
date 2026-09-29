//! Compiles `cpp/harness_shim.cpp` (the C++ fill-differential reference slice) into this
//! crate's Rust test binary via `cc::Build`, so the differential can drive the real
//! `convert_cluster_data`/`stitch_boundaries` orchestration over FFI. Host-only, x86-64: this crate is
//! a test harness, never linked into the firmware, so (unlike `sample_convert`'s build.rs) there is no
//! device path and no armv7a-NEON compile-and-verify step.
//!
//! ## Only ONE translation unit compiled here — deliberately
//!
//! `harness_shim.cpp` `#include`s `convert.h`/`stitch.h` directly (same headers `sample_convert`'s own
//! `cpp/shim.cpp` includes), but this build script does NOT also compile
//! `convert.cpp`/`stitch.cpp`/`util/audio_format_helpers.cpp` the way `sample_convert`'s build.rs does.
//! `convert_cluster_data` is a TEMPLATE (header-only, in `convert.h`) — this TU's instantiation of it
//! compiles straight into `harness_shim.cpp.o` with no external symbol needed. `stitch_boundaries` (a
//! plain, non-template function declared in `stitch.h`, defined in `stitch.cpp`) and the internal
//! `convert_word` the template's `convert_word_in_place` calls (declared in `convert.h`, defined in
//! `convert.cpp`) are left UNDEFINED in this archive on purpose: this crate depends on
//! `deluge_sample_convert` (see `Cargo.toml`) as a normal Rust dependency, and `cc`-compiled static
//! libraries with no `links` key (neither crate declares one) have their `cargo:rustc-link-lib`/
//! `-search` directives forwarded to every downstream binary/test in the same crate graph — exactly how
//! `deluge_sample_convert` already reaches `deluge-bsp-rust`'s own final device/host_app link today. So
//! `deluge_sample_convert`'s ALREADY cc-compiled `convert.cpp.o`/`stitch.cpp.o` (built by ITS build.rs,
//! which — being a dependency — runs and completes before this one does) is already on this test
//! binary's link line by the time these two symbols need resolving, and the linker (rust-lld, which does
//! iterative multi-pass symbol resolution across all provided static archives, not a single left-to-
//! right sweep) picks them up from there.
//!
//! This is a real, load-bearing choice, not a shortcut: if this build.rs instead recompiled
//! `convert.cpp`/`stitch.cpp` itself into a SECOND archive, that archive's `convert.cpp.o`/
//! `stitch.cpp.o` would ALSO be pulled into the final link (to satisfy THIS crate's own
//! `harness_shim.cpp.o`'s references) alongside `deluge_sample_convert`'s copy (pulled in to satisfy
//! ITS `shim.cpp.o`'s references, needed by `fill_logic::finish_convert_stitch`, called from the
//! differential test) — two definitions of the same externally-linked, non-template C++ symbols
//! (`deluge::audio::stream::convert_word`, `deluge::audio::stream::stitch_boundaries`) landing in one
//! link is a genuine "duplicate symbol" error, not a hypothetical one. Not recompiling them here avoids
//! it categorically, and as a bonus needs no argon/SIMDe dependency-fetch of its own — `harness_shim.o`
//! only needs their INCLUDE PATHS (for the template instantiation and the `RawDataFormat`/`ConvertGeometry`/
//! `StitchPrevEdge`/`StitchNextEdge` type declarations), reused directly from `sample_convert`'s own
//! already-fetched `third_party/` cache (see below) rather than fetching a second pinned copy.
//!
//! ## Reusing `sample_convert`'s fetched argon + SIMDe, not fetching a second copy
//!
//! `deluge_sample_convert`'s build.rs fetches argon + SIMDe (pinned by SHA) into its own
//! `crates/deluge_sample_convert/third_party/` cache. Since `deluge_sample_convert` is THIS crate's normal
//! dependency, Cargo always finishes building it (running its build.rs to completion) before running
//! this one — so that cache is guaranteed to exist by the time this script needs it, from ANY directory
//! `cargo test`/`cargo build` is invoked from (Cargo resolves the whole dependency graph bottom-up
//! regardless of the invocation directory). Pointing straight at that directory (a plain filesystem
//! path, not a `DEP_*` build-script variable — `sample_convert`'s build.rs exports none) avoids a
//! redundant network fetch of the identical pinned SHAs.
use std::path::PathBuf;

fn main() {
    let manifest = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    // tests/component/differential/region_fill_differential -> repo root is four levels up.
    let repo = manifest
        .join("../../../..")
        .canonicalize()
        .expect("resolve repo root");

    let src = repo.join("src");
    let src_deluge = src.join("deluge");
    let include = repo.join("include");
    let sim_compat = repo.join("sim/compat");

    let shim_cpp = manifest.join("cpp/harness_shim.cpp");
    println!("cargo:rerun-if-changed={}", shim_cpp.display());
    println!("cargo:rerun-if-changed=build.rs");

    // Reused from deluge_sample_convert's own fetch (see the module doc above) — NOT fetched again here.
    let sample_convert_dir = manifest
        .join("../../../../crates/deluge_sample_convert")
        .canonicalize()
        .expect("resolve deluge_sample_convert dir");
    let argon_inc = sample_convert_dir.join("third_party/argon/include");
    let simde_root = sample_convert_dir.join("third_party/simde");
    assert!(
        argon_inc.join("argon.hpp").is_file(),
        "deluge_sample_convert's fetched argon cache not found at {} — expected deluge_sample_convert's \
         build.rs to have fetched it first (it's this crate's normal dependency, built before this \
         script runs). Try `cargo build` from crates/deluge_sample_convert/ first if invoking this \
         crate in isolation somehow skipped that.",
        argon_inc.display()
    );
    assert!(
        simde_root.join("simde/arm/neon.h").is_file(),
        "deluge_sample_convert's fetched SIMDe cache not found at {}",
        simde_root.display()
    );

    cc::Build::new()
        .cpp(true)
        .std("c++26")
        .file(&shim_cpp)
        // App include roots (mirror sample_convert's own build.rs HOST recipe).
        .include(&src) // definitions_cxx.hpp
        .include(&src_deluge) // storage/..., util/...
        .include(&include) // libdeluge/types.h
        // argon + SIMDe (non-native host): argon's bare <arm/neon.h> and the compat <arm_neon.h> shim.
        .include(&argon_inc) // argon.hpp, argon/helpers/size.hpp
        .include(&sim_compat) // <arm_neon.h> -> SIMDe shim
        .include(&simde_root) // compat shim's <simde/arm/neon.h>
        .include(simde_root.join("simde")) // argon's bare <arm/neon.h>
        // Host markers, mirroring sample_convert's own build.rs.
        .define("DELUGE_HOST", None)
        .define("SIMDE_NO_NATIVE", None) // portable-C NEON fallback = bit-accurate to device NEON
        .flag_if_supported("-Wno-unused-parameter")
        .compile("region_fill_diff_cpp");

    // `tests/native_finish_glue.rs` drives `deluge_sample_fill::chunk` (Rust) directly, with no C++
    // shim: that crate is already this crate's normal dependency and owns the streamed chunk's
    // storage (construct + all seven field accessors). See that test's own module doc for the seam.
}
