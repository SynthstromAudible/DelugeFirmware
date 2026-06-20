//! Rust/Embassy firmware image for the Synthstrom Deluge (RZ/A1L).
//!
//! The third BSP for the Deluge, alongside `src/bsp/rza1` (on-device C) and
//! `src/bsp/host` (simulator). Unlike those, this one OWNS reset, `main`/`_start`
//! and the Embassy executor: the HAL startup (`rza1l_hal` `_start` →
//! `_reset_handler` → `bl main`) lands in [`main`] here, which brings up the
//! platform on the deluge-sdk HAL+BSP and runs the portable C++ application's
//! `deluge_main()` superloop inside a single Embassy task (Stage 1).
//!
//! The C++ application is compiled by CMake into `libdeluge_app.a` and linked in
//! by `build.rs`; this crate implements the `<libdeluge/...>` C-ABI services it
//! calls. See docs/dev/libdeluge_bsp_design.md.
#![no_std]
#![no_main]
#![feature(impl_trait_in_assoc_type)]

use core::mem::MaybeUninit;
use core::panic::PanicInfo;

use embassy_executor::{Executor, Spawner};
use rza1l_hal::allocator;

/// libdeluge POD types generated from include/libdeluge/*.h (types only; the
/// service functions are defined in [`ffi`]).
#[allow(non_camel_case_types, non_upper_case_globals, dead_code)]
mod sys {
    include!(concat!(env!("OUT_DIR"), "/libdeluge_sys.rs"));
}

/// The libdeluge C-ABI service implementations the C++ app calls (M0: stubs).
mod ffi;
/// Non-header app/BSP symbols (USB-host globals, FatFS glue, NE10, runtime shims).
mod ffi_extra;
/// Real impls of the simplest services (system.h, clock.h).
mod services;

unsafe extern "C" {
    /// Start of the free SRAM heap region (set by the linker script).
    static __sram_heap_start: u8;
    /// End of the free SRAM heap region (start of stack/RTT reservation).
    static __sram_heap_end: u8;

    /// The portable C++ application's boot + `mainLoop()` superloop (deluge.cpp),
    /// resolved from libdeluge_app.a. Never returns.
    fn deluge_main() -> i32;
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    // `extern "C"` boundary: never unwind (panic = "abort" is also set).
    #[cfg(feature = "rtt")]
    log::error!("PANIC: {}", _info);
    loop {
        core::hint::spin_loop();
    }
}

static mut EXECUTOR: MaybeUninit<Executor> = MaybeUninit::uninit();

/// Firmware entry. The HAL reset handler (`_reset_handler` in rza1l-hal) ends in
/// `bl main`; control lands here with caches/MMU off and stacks set up.
#[unsafe(no_mangle)]
pub extern "C" fn main() -> ! {
    // RTT logger first, so every boot step is visible over the probe. The ring
    // buffer + control block live in uncached SRAM (.rtt_buffer / rza1l_rtt.x).
    #[cfg(feature = "rtt")]
    {
        let channels = rtt_target::rtt_init! {
            up: { 0: { size: 16384, name: "Terminal", section: ".rtt_buffer" } }
            section_cb: ".rtt_buffer"
        };
        rtt_target::set_print_channel(channels.up.0);
        rtt_target::init_logger_with_level(log::LevelFilter::Debug);
    }
    log::info!("deluge-rust: boot — Rust/Embassy BSP for RZ/A1L");

    // Initialise the SRAM heap before any allocation from internal RAM.
    unsafe {
        let start = core::ptr::addr_of!(__sram_heap_start) as *mut u8;
        let size = core::ptr::addr_of!(__sram_heap_end) as usize - start as usize;
        allocator::SRAM.init(start, size);
    }

    // CPG/PLL, MMU, L1+L2 caches, SDRAM controller, GIC, and the embassy-time
    // (OSTM) driver — the whole platform bring-up, owned by the BSP.
    unsafe { deluge_bsp::system::init_clocks() };
    log::info!("deluge-rust: clocks/MMU/cache/SDRAM/GIC up");

    // Big external SDRAM heap (bulk audio buffers, samples).
    // M1b TODO: base must move past __sdram_bss_end (linker-placed app SDRAM
    // sections live at 0x0C000000); using the full range collides with them.
    unsafe { allocator::SDRAM.init(0x0C00_0000 as *mut u8, 64 * 1024 * 1024) };

    // M1+ setup window (IRQs still masked): GIC source registration goes here.

    // Unmask IRQs so the time driver and peripheral ISRs fire.
    unsafe { cortex_ar::interrupt::enable() };

    let executor: &'static mut Executor = unsafe {
        let p = core::ptr::addr_of_mut!(EXECUTOR);
        (*p).write(Executor::new());
        (*p).assume_init_mut()
    };
    executor.run(|spawner: Spawner| {
        spawner.spawn(app_task().unwrap());
    });
}

/// Stage 1 "superloop in a task": prove the platform is alive (M1a — RTT banner
/// + sync-LED blink, exercising Embassy, the OSTM time driver and GPIO), then run
/// the portable C++ application's boot + `mainLoop()`.
#[embassy_executor::task]
async fn app_task() {
    use embassy_time::Timer;
    use rza1l_hal::gpio;

    // M1a sign-of-life: blink the SYNC LED (P6.7). Visible on the panel and
    // proves the executor, time driver and GPIO all work end-to-end.
    const SYNC_LED_PORT: u8 = 6;
    const SYNC_LED_BIT: u8 = 7;
    // SAFETY: we own this pin; GPIO clocks are up after init_clocks.
    unsafe { gpio::set_as_output(SYNC_LED_PORT, SYNC_LED_BIT) };
    for i in 0..6 {
        unsafe { gpio::write(SYNC_LED_PORT, SYNC_LED_BIT, true) };
        Timer::after_millis(120).await;
        unsafe { gpio::write(SYNC_LED_PORT, SYNC_LED_BIT, false) };
        Timer::after_millis(120).await;
        log::info!("deluge-rust: alive ({})", i);
    }

    // M1b TODO: before deluge_main() the C++ memory model must be set up — zero
    // the SDRAM .bss, copy .sdram_data from its LMA, and run global constructors
    // (__libc_init_array). Until then deluge_main() will fault; the blink above
    // is the M1a verification.
    log::info!("deluge-rust: entering deluge_main()");
    unsafe { deluge_main() };
    // deluge_main() ends in mainLoop()'s while(1); never reached.
    loop {
        core::hint::spin_loop();
    }
}
