//! Device bring-up: the executors, their SGI handlers, and the boot tasks.

use core::mem::MaybeUninit;
use core::panic::PanicInfo;

use deluge_alloc as allocator;
use embassy_executor::{Executor, InterruptExecutor, Spawner};

use crate::channel::{audio_loader, file_bytes, file_directory, file_prefix, file_stream};
use crate::peripherals::{board, control, cv_gate, display, signals, usb};
use crate::storage::{sd, streaming_loader};
use crate::{boot, boot_mem, sys};

/// The Rust global allocator, which `embedded-fatfs`'s `alloc` feature needs.
///
/// A TLSF heap of its own, distinct from `deluge_resource`'s. [`app_task`] gives it its arena
/// before the mount; until then an allocation returns null.
#[global_allocator]
static FS_ALLOCATOR: fs_alloc::DelugeGlobalAlloc = fs_alloc::DelugeGlobalAlloc::new();

unsafe extern "C" {
    /// Draws a fault's call chain on the pads and flushes it to the PIC (`io/debug/fault_pattern.h`).
    /// `fault_type` is a `DelugeCpuFault` (`foundation/panic.h`).
    fn deluge_fault_pattern_draw(fault_pc: u32, fault_type: u32, sys_lr: u32, sys_sp: u32);
}

/// `DELUGE_CPU_FAULT_RUST_PANIC`: the commit ID is drawn orange.
const FAULT_RUST_PANIC: u32 = 0xD;

/// Set by the first panic, so a panic raised while reporting one doesn't draw over it.
static PANICKED: core::sync::atomic::AtomicBool = core::sync::atomic::AtomicBool::new(false);

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    // Where the panic machinery was entered from, and the stack to walk for the rest of the chain.
    let lr: u32;
    let sp: u32;
    // SAFETY: only reads two registers.
    unsafe {
        core::arch::asm!("mov {}, lr", out(reg) lr, options(nomem, nostack, preserves_flags));
        core::arch::asm!("mov {}, sp", out(reg) sp, options(nomem, nostack, preserves_flags));
    }
    // Never unwind across the `extern "C"` boundary. Print straight to RTT, not through `log`:
    // release builds compile `log`'s levels out, which made every device panic silent.
    #[cfg(feature = "rtt")]
    rtt_target::rprintln!("PANIC: {}", _info);
    // And on the pads, like a C++ freeze or a CPU fault, so a panic is legible with no probe
    // attached. Drawn by polling, so it works from any context, the render's included.
    if !PANICKED.swap(true, core::sync::atomic::Ordering::Relaxed) {
        // SAFETY: the reporter only reads the stack within the board's declared stack bounds.
        unsafe { deluge_fault_pattern_draw(0, FAULT_RUST_PANIC, lr, sp) };
    }
    // The spin stalls only the context that panicked, so a thread-mode panic leaves audio playing.
    loop {
        core::hint::spin_loop();
    }
}

static mut EXECUTOR: MaybeUninit<Executor> = MaybeUninit::uninit();

/// The preemptive executor for audio: it runs [`crate::audio::render::render_task`].
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

    // Clocks, caches, SDRAM and the static constructors are up, and IRQs are still masked.
    // SAFETY: C ABI with no arguments; called once, before `deluge_app_init`.
    let bench_ran = unsafe { sys::deluge_app_bench() };

    // GIC source registration, before the global interrupt enable below. The PIC UART starts at
    // 31250 bps; `control::pic_pump` runs the handshake up to 200000 bps.
    unsafe { deluge_bsp::uart::init_pic(31_250) };
    unsafe { deluge_bsp::uart::init_midi(31_250) };
    // The encoders' A-pin interrupts, and their 1 ms pin poll on MTU2 ch3.
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

    // The render task waits for boot to finish before its first tick.
    AUDIO_EXEC
        .start(AUDIO_SGI)
        .spawn(crate::audio::render::render_task().unwrap());
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
        // Decodes pad and button input.
        spawner.spawn(control::pic_pump().unwrap());
        // The sole PIC transmitter: drains the LED/pad output queue.
        spawner.spawn(control::pad_render().unwrap());
        // CV DAC writer (shares RSPI0 with the OLED).
        spawner.spawn(cv_gate::cv_writer().unwrap());
        // Calls the app's clock-in handler once per captured edge.
        spawner.spawn(cv_gate::trigger_clock_task().unwrap());
        // The USB device state machine and the two MIDI endpoint pumps (MIDI port 1).
        spawner.spawn(usb::device_task(usb_midi.device).unwrap());
        spawner.spawn(usb::midi_rx_task(usb_midi.ep_out).unwrap());
        spawner.spawn(usb::midi_tx_task(usb_midi.ep_in).unwrap());
        if bench_ran {
            // A bench image never starts the app: this task blanks the panel and the pads, then
            // parks. It owns the panel, so `oled_render` is not spawned.
            spawner.spawn(bench_park().unwrap());
        } else {
            // SSD1309 init and frame streaming over RSPI0.
            spawner.spawn(display::oled_render().unwrap());
            spawner.spawn(app_task().unwrap());
            // Runs the app's jobs once boot has finished.
            spawner.spawn(crate::app_loop::app_loop().unwrap());
        }
        // Re-identifies the card after a swap. Its own task because `sd::init()` needs
        // embassy-time timers, which a blocking storage call cannot run.
        spawner.spawn(sd::card_service().unwrap());
    });
}

/// Boots the C++ app, then parks.
///
/// Once `deluge_app_init` returns, the render task and the app loop drive the app.
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
        // For LFN scan scratch and handle-table strings. In SDRAM (`.sdram_bss`, zeroed at boot before
        // any task runs): the file system's metadata work runs on the loader tier, never in a render, and
        // never as a DMA target, so it does not need on-chip SRAM, which the image fills.
        const EFATFS_ARENA_SIZE: usize = 96 * 1024;
        #[unsafe(link_section = ".sdram_bss")]
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

    log::info!("deluge-rust: deluge_app_init() (registers the app's jobs)");
    unsafe { sys::deluge_app_init(board::deluge_board()) };
    boot::mark_finished();
    log::info!("deluge-rust: boot finished");

    core::future::pending::<()>().await
}

/// Turns off everything a bench image would otherwise leave lit, then parks without starting the
/// app.
///
/// The PIC (pads, button LEDs, gold-knob rings) and the OLED controller keep their state across a
/// CPU reset, so after a bench boot they would hold whatever the previous boot left, for as long
/// as the device stays powered. This task blanks them all. It is the panel's sole owner in a bench
/// image ([`display::oled_render`] is not spawned), so its OLED commands cannot interleave with a
/// frame.
#[embassy_executor::task]
async fn bench_park() {
    use deluge_bsp::{oled, pic, rgb};

    /// Column pairs the PIC drives: the 16 main columns, then the sidebar as pair 8.
    const COLUMN_PAIRS: u8 = (rgb::COLS / 2) as u8;
    /// Button LEDs: `NUM_LED_COLS` × `NUM_LED_ROWS` (`board_config.h`), ids 0–35.
    const BUTTON_LEDS: u8 = 9 * 4;
    /// Gold knobs with an indicator ring: `NUM_LEVEL_INDICATORS` (`board_config.h`).
    const GOLD_KNOBS: u8 = 2;

    // A command sent mid-handshake would go out at the wrong baud rate.
    pic::wait_ready().await;

    let black = [[0u8; 3]; 16];
    for pair in 0..COLUMN_PAIRS {
        pic::set_column_pair_rgb(pair, &black).await;
    }
    pic::done_sending_rows().await;
    for id in 0..BUTTON_LEDS {
        pic::led_off(id).await;
    }
    for knob in 0..GOLD_KNOBS {
        pic::set_gold_knob_indicators(knob, [0; 4]).await;
    }

    // `init` powers and configures the panel and clears its RAM. The blank frame leaves the
    // visible window dark should anything turn the display back on, then the panel goes off.
    oled::init().await;
    let mut fb = oled::FrameBuffer::new();
    fb.fill(0x00);
    oled::send_frame(&fb).await;
    oled_display_off().await;

    // Raw, like the bench's own lines: `log` prefixes a level and module path, and the host tools
    // match this line from its start.
    #[cfg(feature = "rtt")]
    rtt_target::rprintln!("bench: parked, pads/LEDs/OLED off");
    core::future::pending::<()>().await
}

/// Sends the SSD1309 DISPLAY OFF command, which puts the panel to sleep with every pixel dark.
///
/// Follows `oled::send_frame`'s bus discipline: RSPI0 is held from before the PIC asserts the
/// OLED's chip-select until it confirms the deselect, so no CV write can clock into the panel.
/// DC goes low for the command and back high, the data mode `oled::send_frame` expects.
async fn oled_display_off() {
    use deluge_bsp::{bus, pic};

    /// SSD1309 DISPLAY OFF (sleep mode).
    const SSD1309_DISPLAY_OFF: u8 = 0xAE;

    let mut bus = bus::lock_rspi0().await;
    bus.reconfigure_8bit();
    // The PIC handles its commands in order, so the select echo also confirms DC is low.
    pic::oled_dc_low().await;
    pic::oled_select().await;
    pic::wait_oled_selected().await;
    bus.send8(SSD1309_DISPLAY_OFF);
    bus.wait_end();
    pic::oled_dc_high().await;
    pic::oled_deselect().await;
    pic::wait_oled_deselected().await;
}
