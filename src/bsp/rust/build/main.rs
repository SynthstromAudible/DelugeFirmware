//! Build script: bindgen of the libdeluge headers, and the link of the C++ app into the binary.
//!
//! The device build links the CMake-built C++ app (see [`device`]). A host build links nothing
//! unless `host_app` is on, when it binds the host ABI and links the host-built app (see
//! [`host_app`]). This script never runs CMake; the tree it links must already be built.

use std::env;
use std::path::PathBuf;

mod allowlist;
mod bindgen;
mod crate_types;
mod device;
mod host_app;
mod link;

fn main() {
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let repo_root = manifest_dir.join("../../..").canonicalize().unwrap();
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());

    // `has_app`: the C++ app is linked and `sys` is bindgen's output, on the device and under
    // `host_app`.
    println!("cargo::rustc-check-cfg=cfg(has_app)");
    if env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("none") {
        println!("cargo::rustc-cfg=has_app");
        bindgen::run(
            &repo_root,
            &manifest_dir,
            &out_dir,
            "armv7a-none-eabihf",
            false,
        );
        device::link(&repo_root, &manifest_dir, &out_dir);
    } else if env::var("CARGO_FEATURE_HOST_APP").is_ok() {
        // The bindgen target follows the build target: struct layouts differ between x86-64 and the
        // 32-bit arm-linux target the golden harness also runs on. Built from the target's cfgs
        // rather than `TARGET`, which names a custom target spec (the TSan harness's
        // `x86_64-unknown-linux-gnu-tsan`) by a name clang does not accept as a triple.
        println!("cargo::rustc-cfg=has_app");
        let cfg = |key: &str| env::var(format!("CARGO_CFG_TARGET_{key}")).unwrap_or_default();
        let target = format!(
            "{}-unknown-{}-{}{}",
            cfg("ARCH"),
            cfg("OS"),
            cfg("ENV"),
            cfg("ABI")
        );
        bindgen::run(&repo_root, &manifest_dir, &out_dir, &target, true);
        // Link arguments reach only this package's own binary, which needs `host-runtime`. Without
        // it this crate is a harness's library, and the harness links the app itself.
        if env::var("CARGO_FEATURE_HOST_RUNTIME").is_ok() {
            host_app::link(&repo_root, &out_dir);
        }
    }
}
