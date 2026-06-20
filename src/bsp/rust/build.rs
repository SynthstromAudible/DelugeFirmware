use std::env;
use std::fs;
use std::path::PathBuf;

fn main() {
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());

    // Select the memory layout + linker script (rza1l-hal's build.rs puts
    // rza1l.x / rza1l_rtt.x on the link search path; we supply the matching
    // memory.x alongside). Mirrors deluge-sdk's firmwares/controller-firmware.
    let rtt = env::var("CARGO_FEATURE_RTT").is_ok();
    let (memory_src, linker_script) = if rtt {
        ("memory_rtt.x", "rza1l_rtt.x")
    } else {
        ("memory.x", "rza1l.x")
    };

    fs::copy(manifest_dir.join(memory_src), out_dir.join("memory.x")).unwrap();

    println!("cargo:rustc-link-search={}", out_dir.display());
    println!("cargo:rustc-link-arg=-T{linker_script}");
    println!("cargo:rerun-if-changed=memory.x");

    // M0b will add here: build the repo's `libdeluge_app` CMake target (cross
    // toolchain) and emit cargo:rustc-link-search + rustc-link-lib=static=deluge_app
    // plus the C++ runtime libs, so deluge_main()/deluge_app_* resolve at link.
}
