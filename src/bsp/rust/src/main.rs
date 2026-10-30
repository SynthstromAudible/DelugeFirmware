//! The `deluge-rust` firmware image. Everything lives in the library; this is only the entry point.
#![cfg_attr(target_os = "none", no_std)]
#![cfg_attr(target_os = "none", no_main)]

/// The HAL reset handler (`_reset_handler` in `rza1-rt`) ends in `bl main`; control lands here
/// with caches and the MMU off and the stacks set up.
#[cfg(target_os = "none")]
#[unsafe(no_mangle)]
pub extern "C" fn main() -> ! {
    deluge_bsp_rust::runtime::start()
}

#[cfg(not(target_os = "none"))]
fn main() {
    deluge_bsp_rust::runtime::main();
}
