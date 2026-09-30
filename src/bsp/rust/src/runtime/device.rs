//! Device bring-up: the executors, their SGI handlers, and the boot tasks.

use core::mem::MaybeUninit;
use core::panic::PanicInfo;

use deluge_alloc as allocator;
use embassy_executor::{Executor, InterruptExecutor, Spawner};

use crate::channel::{audio_loader, file_bytes, file_directory, file_prefix, file_stream};
use crate::peripherals::{board, control, cv_gate, display, signals, usb};
use crate::storage::{sd, streaming_loader};
use crate::{boot, boot_mem, scheduler, sys};

/// The Rust global allocator, which `embedded-fatfs`'s `alloc` feature needs.
///
/// A TLSF heap of its own, distinct from `deluge_resource`'s. [`app_task`] gives it its arena
/// before the mount; until then an allocation returns null.
#[global_allocator]
static FS_ALLOCATOR: fs_alloc::DelugeGlobalAlloc = fs_alloc::DelugeGlobalAlloc::new();

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    // Never unwind across the `extern "C"` boundary. Print straight to RTT, not through `log`:
    // release builds compile `log`'s levels out, which made every device panic silent. The spin
    // stalls only the context that panicked, so a thread-mode panic leaves audio playing.
    #[cfg(all(feature = "rtt", target_os = "none"))]
    rtt_target::rprintln!("PANIC: {}", _info);
    loop {
        core::hint::spin_loop();
    }
}

static mut EXECUTOR: MaybeUninit<Executor> = MaybeUninit::uninit();

/// The preemptive executor for audio: the scheduler routes the priority-0 task here.
///
/// It runs in a GIC SGI handler above thread mode, so a thread-mode storage spin cannot starve
/// audio, and below the hard-RT ISRs (OSTM, MTU2 gate, MIDI), which preempt it.
static AUDIO_EXEC: InterruptExecutor = InterruptExecutor::new();

/// The GIC software-generated interrupt (0..=15) driving [`AUDIO_EXEC`]. The board is
/// single-core, so SGIs are otherwise unused.
const AUDIO_SGI: u8 = 8;
/// The audio SGI's GIC priority (lower is more urgent): less urgent than the hard-RT IRQs
/// (OSTM 14, DMAC 13, UART/MIDI 10), so they preempt the render, and below PMR (31), so it is
/// forwarded.
const AUDIO_SGI_PRIORITY: u8 = 20;

/// Drives [`AUDIO_EXEC`] from [`AUDIO_SGI`]. The HAL dispatch acks and EOIs around it, with IRQs
/// re-enabled for nesting.
fn audio_sgi_handler() {
    // Mark the audio context for `deluge_in_audio_context()`, which the resource manager's
    // asymmetric critical section keys off. It must wrap the whole poll, or the manager masks on
    // the one path that must not.
    let prev = crate::services::audio_context_enter();
    // SAFETY: only called from the SGI handler, after AUDIO_EXEC.start().
    unsafe { AUDIO_EXEC.on_interrupt() };
    crate::services::audio_context_exit(prev);
}

/// The preemptive executor for the storage tasks, between audio and thread mode in priority.
///
/// Thread-mode storage waits are non-yielding spins, and the tasks here hold the filesystem mutex
/// across their SD awaits, so they must be able to preempt thread mode to release it.
static STREAM_EXEC: InterruptExecutor = InterruptExecutor::new();

/// The GIC software-generated interrupt driving [`STREAM_EXEC`].
const STREAM_SGI: u8 = 9;
/// The storage SGI's GIC priority: less urgent than [`AUDIO_SGI_PRIORITY`], so audio preempts
/// it, and below PMR (31), so it is forwarded.
const STREAM_SGI_PRIORITY: u8 = 24;

/// Drives [`STREAM_EXEC`] from [`STREAM_SGI`]. The HAL dispatch acks and EOIs around it.
///
/// Unlike [`audio_sgi_handler`] it does not mark the audio context: audio preempts this context,
/// so its resource-manager accesses must keep masking.
fn stream_sgi_handler() {
    // Mark the storage tier for `tier::current()` across the poll. The CPU mode cannot tell: the
    // shared IRQ entry has already switched to SYS mode, the mode thread code runs in.
    let _storage = crate::tier::storage_isr();
    // SAFETY: only called from the SGI handler, after STREAM_EXEC.start().
    unsafe { STREAM_EXEC.on_interrupt() };
}

/// Spawns the storage tasks onto [`STREAM_EXEC`] from inside it.
///
/// `InterruptExecutor::start` hands out a `SendSpawner`, but the fill task's future holds a raw
/// `DelugeResource*` and is not `Send`; this `Send` task gets the executor's own `Spawner`.
///
/// Every task here holds the filesystem mutex across an SD await, so none may share an executor
/// with a thread-mode caller's non-yielding `sim_block::block_on` spin: that would deadlock.
#[embassy_executor::task]
async fn stream_bootstrap() {
    // SAFETY: this task only ever runs on STREAM_EXEC (it is spawned onto it and nowhere
    // else), so the Spawner yielded here belongs to STREAM_EXEC, and the non-Send task
    // spawned with it never leaves the executor it was created on.
    let spawner = unsafe { Spawner::for_current_executor() }.await;
    spawner.spawn(streaming_loader::streaming_fill_task().unwrap());
    spawner.spawn(audio_loader::audio_load_task().unwrap());
    spawner.spawn(file_prefix::file_prefix_task().unwrap());
    spawner.spawn(file_directory::file_directory_task().unwrap());
    spawner.spawn(file_bytes::file_bytes_task().unwrap());
    spawner.spawn(file_stream::file_stream_task().unwrap());
}

/// Brings up the platform and runs the executors; never returns.
///
/// Called from the binary's `main`, which the HAL reset handler enters with the caches and MMU
/// off and the stacks set up.
pub fn start() -> ! {
    // RTT logger first, so every boot step is visible over the probe. The ring
    // buffer + control block live in uncached SRAM (.rtt_buffer / rza1l_rtt.x).
    #[cfg(all(feature = "rtt", target_os = "none"))]
    {
        let channels = rtt_target::rtt_init! {
            up: { 0: { size: 16384, name: "Terminal", section: ".rtt_buffer" } }
            section_cb: ".rtt_buffer"
        };
        rtt_target::set_print_channel(channels.up.0);
        rtt_target::init_logger_with_level(log::LevelFilter::Debug);
    }
    log::info!("deluge-rust: boot — Rust/Embassy BSP for RZ/A1L");

    // No Rust SRAM heap: any static pool would come out of the C++ app's fast on-chip heap, which
    // starts at the image end. `deluge_alloc::SRAM` stays uninitialised, so an allocation from it
    // fails; give it an arena before anything needs one.

    // CPG/PLL, MMU, L1+L2 caches, SDRAM controller, GIC, and the embassy-time (OSTM) driver.
    unsafe { deluge_bsp::system::init_clocks() };
    log::info!("deluge-rust: clocks/MMU/cache/SDRAM/GIC up");

    // Zero the app's SDRAM .bss and copy .sdram_init from its SRAM load address (a debugger
    // cannot write SDRAM at download).
    unsafe { boot_mem::init_sdram_memory() };

    // The Rust SDRAM allocator gets the reserved top slice; the app's heap runs
    // `__heap_start..RUST_SDRAM_BASE`.
    unsafe {
        allocator::SDRAM.init(
            boot_mem::RUST_SDRAM_BASE as *mut u8,
            boot_mem::RUST_SDRAM_RESERVE,
        )
    };
    log::info!("deluge-rust: SDRAM memory image ready");

    // Run C++ global constructors before any application code touches globals.
    unsafe { boot_mem::run_init_array() };
    log::info!("deluge-rust: C++ global constructors done");

    // GIC source registration, before the global interrupt enable below. The PIC UART starts at
    // 31250 bps; `control::pic_pump` runs the handshake up to 200000 bps.
    unsafe { deluge_bsp::uart::init_pic(31_250) };
    unsafe { deluge_bsp::uart::init_midi(31_250) };
    unsafe { deluge_bsp::encoder::irq_init() };
    // The sample-accurate MIDI/gate one-shot timer (MTU2 ch2), armed at render time through
    // `deluge_midi_gate_timer_arm`. Its module clock is already ungated by `init_clocks`.
    unsafe { rza1l_hal::mtu2::enable_write() };
    unsafe { signals::gate_timer_setup() };
    // Trigger clock in: P1_14 → IRQ6 on the falling edge (an on-board transistor inverts the
    // input). The app's handler runs from the cooperative `trigger_clock_task`, between app ticks.
    unsafe { deluge_bsp::trigger_clock::irq_init() };
    // USB-MIDI device (USB0): builds the device and registers its GIC handler; the driver enables
    // the line when `device.run()` starts.
    let usb_midi = unsafe { usb::build() };

    // Each SGI line is enabled only after its executor has started, below, so a handler never
    // runs before the executor it drives exists.
    unsafe {
        rza1l_hal::gic::register(AUDIO_SGI as u16, audio_sgi_handler);
        rza1l_hal::gic::set_priority(AUDIO_SGI as u16, AUDIO_SGI_PRIORITY);
        rza1l_hal::gic::register(STREAM_SGI as u16, stream_sgi_handler);
        rza1l_hal::gic::set_priority(STREAM_SGI as u16, STREAM_SGI_PRIORITY);
    }

    // Unmask IRQs so the time driver and peripheral ISRs fire.
    unsafe { cortex_ar::interrupt::enable() };

    // The SGI pender otherwise derives the GIC Distributor from CBAR, which is only valid on MPCore
    // parts; here it would write GICD_SGIR to a bogus address and audio would never run. Must
    // precede any use of the interrupt executors.
    embassy_executor::set_gicd_base(0xE820_1000);

    // Must precede `deluge_app_init`, whose `registerTasks()` adds the priority-0 task.
    scheduler::set_audio_spawner(AUDIO_EXEC.start(AUDIO_SGI));
    unsafe { rza1l_hal::gic::enable(AUDIO_SGI as u16) };

    STREAM_EXEC
        .start(STREAM_SGI)
        .spawn(stream_bootstrap().unwrap());
    unsafe { rza1l_hal::gic::enable(STREAM_SGI as u16) };

    let executor: &'static mut Executor = unsafe {
        let p = core::ptr::addr_of_mut!(EXECUTOR);
        (*p).write(Executor::new());
        (*p).assume_init_mut()
    };
    executor.run(move |spawner: Spawner| {
        // For the task runners `registerTasks()` spawns through the scheduler's C ABI.
        scheduler::set_spawner(spawner);
        // Decodes pad and button input.
        spawner.spawn(control::pic_pump().unwrap());
        // Wakes the app's self-blocking encoder task on encoder movement.
        spawner.spawn(control::encoder_wake_pump().unwrap());
        // The sole PIC transmitter: drains the LED/pad output queue.
        spawner.spawn(control::pad_render().unwrap());
        // SSD1309 init and frame streaming over RSPI0.
        spawner.spawn(display::oled_render().unwrap());
        // CV DAC writer (shares RSPI0 with the OLED).
        spawner.spawn(cv_gate::cv_writer().unwrap());
        // Calls the app's clock-in handler once per captured edge.
        spawner.spawn(cv_gate::trigger_clock_task().unwrap());
        // The USB device state machine and the two MIDI endpoint pumps (MIDI port 1).
        spawner.spawn(usb::device_task(usb_midi.device).unwrap());
        spawner.spawn(usb::midi_rx_task(usb_midi.ep_out).unwrap());
        spawner.spawn(usb::midi_tx_task(usb_midi.ep_in).unwrap());
        spawner.spawn(app_task().unwrap());
        // Re-identifies the card after a swap. Its own task because `sd::init()` needs
        // embassy-time timers, which a blocking storage call cannot run.
        spawner.spawn(sd::card_service().unwrap());
    });
}

/// Boots the C++ app, then parks.
///
/// `deluge_app_init` calls `registerTasks()`, which spawns one Embassy task per Deluge task
/// through the [`scheduler`] C ABI; those runners drive the app from then on.
#[embassy_executor::task]
async fn app_task() {
    use embassy_time::Timer;
    use rza1l_hal::gpio;

    // Sign of life: blink the SYNC LED (P6.7), which exercises the executor, time driver and GPIO.
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

    // SD comes up before `deluge_app_init`, so the boot settings read finds the card. First wait
    // for the PIC's baud-rate handshake: `sd::init()` racing it garbles the pads. Async, because
    // `sd::init`'s timers cannot run under `block_on`.
    deluge_bsp::pic::wait_ready().await;
    crate::storage::sd::boot_init().await;

    // Give `FS_ALLOCATOR` its arena and mount the filesystem before `deluge_app_init`, for the
    // first sample load. A failed mount does not stop boot, but streamed samples then won't load.
    {
        // For LFN scan scratch and handle-table strings. Plain `.bss`, in on-chip SRAM.
        const EFATFS_ARENA_SIZE: usize = 96 * 1024;
        static mut EFATFS_ARENA: [u8; EFATFS_ARENA_SIZE] = [0; EFATFS_ARENA_SIZE];

        // SAFETY: only this block touches the function-local `EFATFS_ARENA`, and `app_task` runs
        // once. `addr_of_mut!` avoids a reference to the `static mut`.
        let (base, size) = unsafe {
            let p = core::ptr::addr_of_mut!(EFATFS_ARENA);
            (p.cast::<u8>(), core::mem::size_of_val(&*p))
        };
        // SAFETY: `base`/`size` describe that exclusively owned `'static` arena, and
        // `deluge_heap_create` writes only within it.
        let handle = unsafe { fs_alloc::deluge_heap_create(base, size) };
        assert!(
            !handle.is_null(),
            "efatfs: {EFATFS_ARENA_SIZE}-byte arena too small for a DelugeHeap control block"
        );
        FS_ALLOCATOR.init(handle);
        log::info!(
            "efatfs: FS_ALLOCATOR initialised over a {} KiB arena",
            EFATFS_ARENA_SIZE / 1024
        );

        match crate::storage::efatfs::device::mount().await {
            Ok(()) => log::info!("efatfs: mounted"),
            Err(status) => {
                log::warn!("efatfs: mount failed ({status}) — streamed samples will not load")
            }
        }
    }

    log::info!("deluge-rust: deluge_app_init() (registers + spawns task runners)");
    // The task runners `registerTasks()` spawns start once this task yields.
    unsafe { sys::deluge_app_init(board::deluge_board()) };
    boot::mark_finished();
    log::info!("deluge-rust: scheduler running");

    core::future::pending::<()>().await
}
