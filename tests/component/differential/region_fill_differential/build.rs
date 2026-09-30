//! Compiles `cpp/harness_shim.cpp`, the C++ reference slice for the fill differential, into the
//! test binary. Host-only: this crate never links into firmware.
//!
//! # Only one translation unit
//!
//! `convert_cluster_data` is a header-only template, so its instantiation lands in
//! `harness_shim.cpp.o`. The non-template `convert_word` and `stitch_boundaries` are left undefined
//! here and resolve from `deluge_sample_convert`'s already-compiled `convert.cpp.o`/`stitch.cpp.o`:
//! its `cc` link directives reach every downstream binary (it declares no `links` key). Compiling
//! `convert.cpp`/`stitch.cpp` here too would put a second definition of those symbols in the same
//! link and fail with duplicate symbols.
//!
//! # argon and SIMDe
//!
//! The include paths for argon and SIMDe point at `deluge_sample_convert`'s fetched `third_party/`
//! cache. That crate is a normal dependency, so its build script has run (and fetched the pinned
//! copies) before this one; it exports no `DEP_*` variable, hence the plain path.
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

    // Reused from deluge_sample_convert's fetch; see the module doc.
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
        // argon + SIMDe (non-native host): argon's bare <arm/neon.h> and the compat <arm_neon.h>
        // shim.
        .include(&argon_inc) // argon.hpp, argon/helpers/size.hpp
        .include(&sim_compat) // <arm_neon.h> -> SIMDe shim
        .include(&simde_root) // compat shim's <simde/arm/neon.h>
        .include(simde_root.join("simde")) // argon's bare <arm/neon.h>
        // Host markers, mirroring sample_convert's own build.rs.
        .define("DELUGE_HOST", None)
        .define("SIMDE_NO_NATIVE", None) // portable-C NEON fallback = bit-accurate to device NEON
        .flag_if_supported("-Wno-unused-parameter")
        .compile("region_fill_diff_cpp");
}
