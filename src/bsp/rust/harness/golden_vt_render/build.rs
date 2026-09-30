//! Links the host-built C++ app, with the BSP's own link logic (link arguments do not reach
//! dependents, so the BSP's build script cannot do it for this binary).

use std::env;
use std::path::PathBuf;

#[path = "../../build/host_app.rs"]
mod host_app;
#[path = "../../build/link.rs"]
mod link;

fn main() {
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let repo_root = manifest_dir.join("../../../../..").canonicalize().unwrap();
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    host_app::link(&repo_root, &out_dir);
    println!("cargo:rerun-if-changed=../../build/host_app.rs");
    println!("cargo:rerun-if-changed=../../build/link.rs");
}
