//! Links the host-built C++ app using the BSP's own link logic.
//!
//! Link arguments do not propagate to dependents, so the BSP's build script cannot do it for this
//! binary.

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
    // TODO(ui-integration): the UI libraries (deluge_gfx_oled, deluge_gfx_grid, deluge_ui_core,
    // deluge_ui_oled, deluge_ui_grid, deluge_ui_shell, deluge_notify, deluge_scenes) join
    // `host_app::link_closure`'s DEPS so this harness links the app's UI, re-hosted on golden_vt_render.
    host_app::link(&repo_root, &out_dir);
    println!("cargo:rerun-if-changed=../../build/host_app.rs");
    println!("cargo:rerun-if-changed=../../build/link.rs");
}
