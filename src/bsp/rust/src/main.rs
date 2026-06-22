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

use embassy_executor::{Executor, InterruptExecutor, Spawner};
// General-purpose Rust heaps (for BSP/Embassy/our boot). The C++ app keeps its
// own GeneralMemoryAllocator over the region we hand it; the Rust app allocator
// (TLSF/slab, docs/dev/allocator_redesign.md) is a later, separately-gated step.
use deluge_alloc as allocator;

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
/// Real impls of the simplest services (system.h, clock.h, memory.h).
mod services;
/// board.h — capability descriptor + GPIO/audio/CV bring-up.
mod board;
/// control_surface.h — pads/buttons/encoders + LEDs (M2b WIP).
mod control;
/// display.h — main OLED output over deluge_bsp::oled.
mod display;
/// audio_io.h — duplex block audio over the SSI0 DMA rings.
mod audio;
/// block_device.h + FatFS diskio — SD card over deluge_bsp::sd.
mod sd;
/// flash.h — persistent settings flash over deluge_bsp::flash / spibsc.
mod flash;
/// cv_gate.h — CV/gate outputs + external trigger clock.
mod cv_gate;
/// midi_io.h — DIN MIDI over deluge_bsp::uart (+ USB-MIDI peripheral, see usb).
mod midi;
/// USB device bring-up — USB-MIDI 1.0 peripheral (Deluge → computer).
mod usb;
/// signals.h — board GPIO signals, battery, MIDI/gate timer.
mod signals;
/// C++ memory-model bring-up (SDRAM bss/data, global ctors).
mod boot_mem;
/// scheduler.h / OSLikeStuff scheduler_api.h — the cooperative task scheduler,
/// implemented on the Embassy executor (one task per registered Deluge task).
mod scheduler;
/// The worker fiber: a stackful coroutine for the long synchronous C++ operations
/// that pause via `yield()` (Phase 6). This module is the context-switch primitive.
mod fiber;

unsafe extern "C" {
    /// One-time C++ application bring-up (deluge.cpp / app.h). On this BSP it also
    /// runs `registerTasks()`, which spawns one Embassy task per registered Deluge
    /// task through the `scheduler_api.h` C ABI in [`scheduler`]. After this returns
    /// the spawned runners drive the app — there is no `deluge_app_tick` loop.
    fn deluge_app_init(board: *const sys::DelugeBoard);
}

/// Rust-side SRAM heap pool. The C++ app's GeneralMemoryAllocator owns the rest
/// of SRAM (`[__heap_start, program_stack_start)`), so the Rust heap is a small,
/// dedicated static pool that can't overlap it. Nothing on the Rust side
/// allocates from SRAM yet; sized small.
static mut RUST_SRAM_POOL: [u8; 64 * 1024] = [0; 64 * 1024];

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

/// Preemptive Embassy executor for audio (Phase 3 audio lift). Runs the audio
/// render task in a GIC SGI handler at a priority above thread mode but below the
/// µs hard-RT ISRs, so it preempts the cooperative thread executor (a storage
/// `yield()` spin can no longer starve audio) yet is itself preempted by
/// OSTM/MTU2-gate/MIDI. The scheduler routes the priority-0 task here.
static AUDIO_EXEC: InterruptExecutor = InterruptExecutor::new();

/// GIC Software-Generated Interrupt id driving [`AUDIO_EXEC`] (0..=15; SMP-free
/// board, so SGIs are otherwise unused).
const AUDIO_SGI: u8 = 8;
/// Audio SGI GIC priority. Numerically ABOVE the hard-RT IRQs (OSTM=14, UART/MIDI
/// =10, DMAC=13) so they preempt the render, and below PMR (31) so it is
/// forwarded. (Lower number = more urgent.)
const AUDIO_SGI_PRIORITY: u8 = 20;

/// GIC handler for [`AUDIO_SGI`]: drive the audio interrupt executor. Registered
/// in the HAL dispatch (`gic::register`), which already acks (GICC_IAR) before and
/// EOIs (GICC_EOIR) after, with IRQs re-enabled for nesting.
fn audio_sgi_handler() {
    // SAFETY: only called from the SGI handler, after AUDIO_EXEC.start().
    unsafe { AUDIO_EXEC.on_interrupt() };
}

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

    // Initialise the Rust-side SRAM heap over its dedicated static pool.
    unsafe {
        let p = core::ptr::addr_of_mut!(RUST_SRAM_POOL);
        allocator::SRAM.init(p as *mut u8, core::mem::size_of_val(&*p));
    }

    // CPG/PLL, MMU, L1+L2 caches, SDRAM controller, GIC, and the embassy-time
    // (OSTM) driver — the whole platform bring-up, owned by the BSP.
    unsafe { deluge_bsp::system::init_clocks() };
    log::info!("deluge-rust: clocks/MMU/cache/SDRAM/GIC up");

    // SDRAM is up: zero the app's SDRAM .bss and copy .sdram_init from its SRAM
    // load address (the debugger can't write SDRAM at download).
    unsafe { boot_mem::init_sdram_memory() };

    // Rust SDRAM allocator gets the reserved top slice; the app owns the rest
    // (its heap runs __heap_start .. deluge_memory_external_end()).
    unsafe {
        allocator::SDRAM.init(boot_mem::RUST_SDRAM_BASE as *mut u8, boot_mem::RUST_SDRAM_RESERVE)
    };
    log::info!("deluge-rust: SDRAM memory image ready");

    // Run C++ global constructors before any application code touches globals.
    unsafe { boot_mem::run_init_array() };
    log::info!("deluge-rust: C++ global constructors done");

    // Setup window — GIC source registration, with IRQs still masked:
    //  - PIC co-processor UART (SCIF1) with DMA RX/TX, started at 31250 bps; the
    //    pump task (control::pic_pump) runs the handshake up to 200000 bps.
    //  - The six front-panel quadrature encoders (GPIO + GIC edge IRQs).
    // Both must be configured before the global interrupt enable below.
    unsafe { deluge_bsp::uart::init_pic(31_250) };
    unsafe { deluge_bsp::uart::init_midi(31_250) };
    unsafe { deluge_bsp::encoder::irq_init() };
    // Sample-accurate MIDI/gate one-shot timer (MTU2 ch2). Unlock MTU2 register
    // writes once, then register the compare-match ISR; armed at render time by
    // the app via deluge_midi_gate_timer_arm(). MTU2's module clock is already
    // ungated by init_clocks (STBCR3).
    unsafe { rza1l_hal::mtu2::enable_write() };
    unsafe { signals::gate_timer_setup() };
    // Trigger-clock (analog clock-in): route P1_14 → IRQ6 (falling edge — an
    // on-board transistor inverts the external clock, so an external rising edge
    // is a falling edge here). The app's handler runs from the cooperative
    // trigger_clock_task between app ticks; with no preemption it needs none of
    // the critical-section protection the original ISR path used. See cv_gate.rs.
    unsafe { deluge_bsp::trigger_clock::irq_init() };
    // USB-MIDI 1.0 peripheral (Deluge → computer): bring up USB0 in device mode
    // and build the device now (registers the USB0 GIC handler); its tasks are
    // spawned on the executor below. The GIC line is enabled by the driver when
    // device.run() starts.
    let usb_midi = unsafe { usb::build() };

    // Audio interrupt-executor: register the SGI handler and set its priority
    // (below the hard-RT IRQs, above thread mode). The GIC line is *enabled* only
    // after AUDIO_EXEC.start() below, so audio_sgi_handler can never run before the
    // executor it drives is initialised.
    unsafe {
        rza1l_hal::gic::register(AUDIO_SGI as u16, audio_sgi_handler);
        rza1l_hal::gic::set_priority(AUDIO_SGI as u16, AUDIO_SGI_PRIORITY);
    }

    // Verify the worker-fiber context switch in isolation before it drives real
    // operations (Phase 6 bring-up). Pure + synchronous; logs PASS/FAIL over RTT.
    fiber::selftest();

    // Unmask IRQs so the time driver and peripheral ISRs fire.
    unsafe { cortex_ar::interrupt::enable() };

    // Start the audio interrupt-executor and stash its SendSpawner so the scheduler
    // can spawn the priority-0 (audio) task onto it. Must precede deluge_app_init
    // (in app_task), which calls registerTasks() → addRepeatingTask(priority 0).
    // Enable the SGI in the GIC only now that the executor is initialised.
    scheduler::set_audio_spawner(AUDIO_EXEC.start(AUDIO_SGI));
    unsafe { rza1l_hal::gic::enable(AUDIO_SGI as u16) };

    let executor: &'static mut Executor = unsafe {
        let p = core::ptr::addr_of_mut!(EXECUTOR);
        (*p).write(Executor::new());
        (*p).assume_init_mut()
    };
    executor.run(move |spawner: Spawner| {
        // Stash the spawner so the scheduler's add*Task C-ABI entry points (called
        // synchronously by the C++ app during registerTasks) can spawn task runners.
        scheduler::set_spawner(spawner);
        // The PIC pump decodes pad/button input concurrently with the app's tick
        // loop (which yields each tick, letting these tasks make progress).
        spawner.spawn(control::pic_pump().unwrap());
        // Bridges the encoder edge ISRs to the scheduler: unblocks the app's
        // self-blocking encoder task on movement (else encoders stay dead).
        spawner.spawn(control::encoder_wake_pump().unwrap());
        // The sole PIC transmitter: drains the LED/pad output queue.
        spawner.spawn(control::pad_render().unwrap());
        // The OLED render task: SSD1309 init + frame streaming over RSPI0.
        spawner.spawn(display::oled_render().unwrap());
        // CV DAC writer (shares RSPI0 with the OLED).
        spawner.spawn(cv_gate::cv_writer().unwrap());
        // External trigger-clock: calls the app's clock-in handler once per
        // captured rising edge (cooperatively, between app ticks).
        spawner.spawn(cv_gate::trigger_clock_task().unwrap());
        // USB-MIDI 1.0 peripheral: the device state machine + the two MIDI 1.0
        // endpoint pumps (host↔class byte queues, bridged by midi.rs port 1).
        spawner.spawn(usb::device_task(usb_midi.device).unwrap());
        spawner.spawn(usb::midi_rx_task(usb_midi.ep_out).unwrap());
        spawner.spawn(usb::midi_tx_task(usb_midi.ep_in).unwrap());
        spawner.spawn(app_task().unwrap());
    });
}

/// The application bring-up task. Runs the C++ app's one-time init, which on this
/// BSP also calls `registerTasks()` → spawns one Embassy task per registered
/// Deluge task via the [`scheduler`] C ABI. Those runners (plus the BSP's async
/// I/O tasks) then drive everything cooperatively, so this task has nothing left
/// to do and parks forever. The decomposed scheduler replaces the old yielding
/// `deluge_app_tick` superloop.
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

    // Bring SD up BEFORE deluge_app_init so the app's boot settings read finds the
    // card ready (no "SD CARD ERROR" popup). But first wait for the PIC's baud-rate
    // handshake (31250→200000, run by pic_pump) to finish: sd::init() racing that
    // handshake corrupts it → garbled pads. With the PIC already ready, sd::init has
    // nothing to race. Must be async — sd::init's embassy-time Timers can't run under
    // block_on (integrated timer queue).
    deluge_bsp::pic::wait_ready().await;
    crate::sd::boot_init().await;

    log::info!("deluge-rust: deluge_app_init() (registers + spawns task runners)");
    // deluge_app_init → registerTasks() spawns the per-task runners onto this
    // executor via scheduler::set_spawner's stashed spawner. They begin running as
    // soon as we yield below.
    unsafe { deluge_app_init(board::deluge_board()) };
    log::info!("deluge-rust: scheduler running; pumping async worker");

    // The scheduler's task runners own the periodic app work. This task now pumps
    // the worker fiber ([`fiber`]): it drives the long user-initiated operations
    // (song load, stem export, grid clip create) that `yield()` instead of
    // busy-waiting. Each tick starts/resumes a ready operation; between ticks the
    // executor runs every other task + I/O, so the awaited work makes progress
    // (this is what lets song-load-while-playing complete instead of hanging on a
    // frozen executor). ~1 ms cadence — imperceptible for these ops.
    let mut ticker = embassy_time::Ticker::every(embassy_time::Duration::from_millis(1));
    loop {
        fiber::worker_poll();
        ticker.next().await;
    }
}
