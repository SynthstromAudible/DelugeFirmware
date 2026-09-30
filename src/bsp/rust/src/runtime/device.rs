//! Device bring-up: the executors, their SGI handlers, and the boot tasks.

use core::mem::MaybeUninit;
use core::panic::PanicInfo;

use deluge_alloc as allocator;
use embassy_executor::{Executor, InterruptExecutor, Spawner};

use crate::{
    audio_loader, board, boot, boot_mem, control, cv_gate, display, file_bytes, file_directory,
    file_prefix, file_stream, scheduler, sd, signals, streaming_loader, sys, usb,
};

/// `#[global_allocator]` binding for `extern crate alloc` — required as
/// soon as any dependency needs it, which `fat_block_device.rs`'s
/// `embedded-fatfs` (`alloc` feature) is the first to on this target; nothing
/// else here used `alloc` before. Backed by `fs_alloc::DelugeGlobalAlloc` — the
/// same TLSF heap *implementation* `deluge_resource` uses for the residency
/// engine's C ABI, but a DISTINCT instance/arena (its own `DelugeHeap`, not
/// shared with `deluge_resource`'s). Aliased to the `fs_alloc` crate name to
/// avoid colliding with the sibling deluge-sdk `deluge-alloc` crate
/// (`allocator`, above) — both would otherwise normalize to
/// the identifier `deluge_alloc`.
///
/// NOT YET INITIALISED with a real backing arena (`fs_alloc::DelugeGlobalAlloc
/// ::init` is never called): this registration exists purely so `alloc` has a
/// `GlobalAlloc` impl to bind to at link time, satisfying the whole-stack
/// cross-compile. `deluge_alloc::deluge_alloc`'s null-handle guard makes an
/// allocation through an uninitialised instance return null (not UB), so this
/// is safe to add now without a live heap — but nothing that actually needs
/// `alloc` (e.g. a real on-device `embedded-fatfs` mount) can run correctly
/// until a future task calls `.init()` with a real arena and decides where it
/// comes from (dedicated pool vs. shared with an existing heap).
#[global_allocator]
static FS_ALLOCATOR: fs_alloc::DelugeGlobalAlloc = fs_alloc::DelugeGlobalAlloc::new();

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    // `extern "C"` boundary: never unwind (panic = "abort" is also set).
    //
    // Print STRAIGHT to the RTT channel rather than through `log::error!`. A panic is
    // exactly the moment the logging stack cannot be trusted to be working: this used
    // `log::error!`, the `log` crate's max level was compiled out in release (see
    // deluge-sdk's workspace `log` dependency), and every panic on the only profile that
    // fits the device was therefore silent. The spin below happens in whatever context
    // panicked, so in thread mode it stalls the whole thread-mode executor
    // while audio carries on in its own interrupt executor -- a dead front panel with
    // sound still playing, and not one byte of explanation. `rprintln!` shares the
    // channel the logger sits on but owes it nothing.
    #[cfg(all(feature = "rtt", target_os = "none"))]
    rtt_target::rprintln!("PANIC: {}", _info);
    loop {
        core::hint::spin_loop();
    }
}

static mut EXECUTOR: MaybeUninit<Executor> = MaybeUninit::uninit();

/// Preemptive Embassy executor for audio. Runs the audio
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
    // Mark the audio context for `deluge_in_audio_context()` — the predicate the resource
    // manager's asymmetric critical section keys off. Must wrap the whole poll: every manager
    // access the audio task makes has to be inside it, or the manager will mask on the one
    // path that must not.
    let prev = crate::services::audio_context_enter();
    // SAFETY: only called from the SGI handler, after AUDIO_EXEC.start().
    unsafe { AUDIO_EXEC.on_interrupt() };
    crate::services::audio_context_exit(prev);
}

/// Preemptive Embassy executor for the storage streaming fill task. A second interrupt
/// executor at a priority *between* audio and thread mode, so audio still preempts the fill
/// and the fill preempts thread mode.
///
/// That ordering is the whole point: thread-mode storage waits are non-yielding spins, and a
/// spin in thread mode must not be able to monopolise the context that
/// releases the filesystem mutex — which is this task, since it holds that mutex across its
/// SD await (`efatfs_fs::read_at` -> `with_fs`).
static STREAM_EXEC: InterruptExecutor = InterruptExecutor::new();

/// GIC Software-Generated Interrupt id driving [`STREAM_EXEC`]. [`AUDIO_SGI`] is 8; 9 is the
/// next free one (SMP-free board, so SGIs are otherwise unused).
const STREAM_SGI: u8 = 9;
/// Streaming SGI GIC priority. Numerically BELOW [`AUDIO_SGI_PRIORITY`] (20) so audio
/// preempts the fill, above thread mode so the fill preempts thread mode, and below PMR (31) so
/// it is forwarded at all. (Lower number = more urgent.)
const STREAM_SGI_PRIORITY: u8 = 24;

/// GIC handler for [`STREAM_SGI`]: drive the streaming interrupt executor. Registered in the
/// HAL dispatch (`gic::register`), which already acks (GICC_IAR) before and EOIs (GICC_EOIR)
/// after, with IRQs re-enabled for nesting.
///
/// Deliberately does NOT mark the audio context (unlike [`audio_sgi_handler`]): this context
/// is preempted by audio, so its resource-manager accesses must keep masking. See
/// `deluge_resource::sync`'s module doc.
fn stream_sgi_handler() {
    // Mark the storage tier for `tier::current()` across the whole poll. The CPU mode cannot say this: the shared
    // IRQ entry has already switched to SYS mode, the same mode thread code runs in.
    let _storage = crate::tier::storage_isr();
    // SAFETY: only called from the SGI handler, after STREAM_EXEC.start().
    unsafe { STREAM_EXEC.on_interrupt() };
}

/// Bootstrap for [`STREAM_EXEC`]: obtain that executor's own `Spawner` from a task already
/// running on it, then spawn the (non-`Send`) storage tasks locally.
///
/// The indirection exists because `InterruptExecutor::start` hands out a `SendSpawner`, while
/// the fill task's future holds a raw `DelugeResource*` and so is not `Send`. This task is
/// `Send` (it owns nothing across its single await), and once it is running ON `STREAM_EXEC`
/// it can spawn onto that executor directly.
///
/// The audio-file load task lands here rather than on the thread-mode executor for the reason
/// its own module doc gives: like the fill task, it holds the single `efatfs` FS mutex across
/// an SD await, and a thread-mode caller's non-yielding `sim_block::block_on` spin would deadlock
/// against that if both shared an executor. The file-prefix task is here for the same reason
/// and on the same terms.
#[embassy_executor::task]
async fn stream_bootstrap() {
    // SAFETY: this task only ever runs on STREAM_EXEC (it is spawned onto it and nowhere
    // else), so the Spawner yielded here belongs to STREAM_EXEC, and the non-Send task
    // spawned with it never leaves the executor it was created on.
    let spawner = unsafe { Spawner::for_current_executor() }.await;
    spawner.spawn(streaming_loader::streaming_fill_task().unwrap());
    spawner.spawn(audio_loader::audio_load_task().unwrap());
    // Here rather than on the thread-mode executor for the same reason as the load task above: it
    // holds the FS mutex across an SD await, and a thread-mode caller's non-yielding
    // `sim_block::block_on` spin would deadlock against that if the two shared an executor.
    spawner.spawn(file_prefix::file_prefix_task().unwrap());
    // Here for the same reason again: `task_dir_open` goes through `with_fs`, so it holds the FS
    // mutex across an SD await.
    spawner.spawn(file_directory::file_directory_task().unwrap());
    // And again: every one of `task_file_open`/`_read_exact`/`_write`/`_truncate` and `task_rename`
    // goes through `with_fs`, so this task holds the FS mutex across an SD await too.
    spawner.spawn(file_bytes::file_bytes_task().unwrap());
    // The recorder's cluster write. Same reason as its neighbours: `stream_write_at` goes through
    // `with_fs`, so this task holds the FS mutex across an SD await.
    spawner.spawn(file_stream::file_stream_task().unwrap());
}

/// Firmware entry. The HAL reset handler (`_reset_handler` in rza1l-hal) ends in
/// `bl main`; control lands here with caches/MMU off and stacks set up.
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

    // No Rust-side SRAM heap. Nothing here allocates from on-chip SRAM -- the SDK crate that would
    // make `deluge_alloc::SRAM` a global allocator is not linked -- and the 64 KiB static pool it used
    // to be initialised over came straight out of the C++ app's fast on-chip heap, which starts at
    // the image end rounded up to 64 KiB. `deluge_alloc::SRAM` stays uninitialised, so an allocation
    // from it fails rather than landing anywhere; give it a real arena before anything needs it.

    // CPG/PLL, MMU, L1+L2 caches, SDRAM controller, GIC, and the embassy-time
    // (OSTM) driver — the whole platform bring-up, owned by the BSP.
    unsafe { deluge_bsp::system::init_clocks() };
    log::info!("deluge-rust: clocks/MMU/cache/SDRAM/GIC up");

    // SDRAM is up: zero the app's SDRAM .bss and copy .sdram_init from its SRAM
    // load address (the debugger can't write SDRAM at download).
    unsafe { boot_mem::init_sdram_memory() };

    // Rust SDRAM allocator gets the reserved top slice; the app owns the rest
    // (its heap runs __heap_start .. boot_mem::RUST_SDRAM_BASE).
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
        rza1l_hal::gic::register(STREAM_SGI as u16, stream_sgi_handler);
        rza1l_hal::gic::set_priority(STREAM_SGI as u16, STREAM_SGI_PRIORITY);
    }

    // Unmask IRQs so the time driver and peripheral ISRs fire.
    unsafe { cortex_ar::interrupt::enable() };

    // Tell the interrupt-executor SGI pender where the GIC Distributor actually
    // is. The pender otherwise derives it from CBAR/PERIPHBASE, which is only
    // valid on Cortex-A MPCore parts; on this single-core RZ/A1L CBAR reads
    // 0xF000_0000 while the Renesas-integrated GIC Distributor is fixed at
    // 0xE820_1000. Without this the pender writes GICD_SGIR to a bogus address
    // and the audio SGI never fires (silent audio). Must precede AUDIO_EXEC use.
    embassy_executor::set_gicd_base(0xE820_1000);

    // Start the audio interrupt-executor and stash its SendSpawner so the scheduler
    // can spawn the priority-0 (audio) task onto it. Must precede deluge_app_init
    // (in app_task), which calls registerTasks() → addRepeatingTask(priority 0).
    // Enable the SGI in the GIC only now that the executor is initialised.
    scheduler::set_audio_spawner(AUDIO_EXEC.start(AUDIO_SGI));
    unsafe { rza1l_hal::gic::enable(AUDIO_SGI as u16) };

    // Streaming fill executor, started here for the same reason audio is: the GIC line must
    // not be enabled before the executor its handler drives exists. Spawned via
    // `stream_bootstrap` — see that task's doc for why a SendSpawner cannot take the fill task
    // directly.
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
        // Re-identifies the card after a swap. Must be its own task: `sd::init()`
        // needs embassy-time Timers, which a blocking storage call cannot provide
        // (see `sd::card_service`).
        spawner.spawn(sd::card_service().unwrap());
        // The async cluster-fill task is not spawned here: it runs on STREAM_EXEC,
        // its own preemptive interrupt executor (see `STREAM_EXEC` and `stream_bootstrap`).
        // A non-yielding storage spin in thread mode must not be able to starve it.
    });
}

/// The application bring-up task. Runs the C++ app's one-time init, which on this
/// BSP also calls `registerTasks()` → spawns one Embassy task per registered
/// Deluge task via the [`scheduler`] C ABI. Those runners (plus the BSP's async
/// I/O tasks) then drive everything cooperatively, so this task has nothing left
/// to do and parks forever.
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

    // Give the FS allocator a real backing arena, then mount the single-owner
    // embedded-fatfs `FileSystem` — BEFORE `deluge_app_init` so the FS is ready
    // for the first C++ sample-load. A failed mount must NOT brick boot — but note
    // the streaming read is efatfs-only with no fallback filesystem, so a
    // failed mount means streamed samples won't load (open_read_stream fails).
    // The SD block driver is already up (boot_init above) and nothing has
    // touched the card yet.
    {
        // Backing arena for `FS_ALLOCATOR` (see its doc): embedded-fatfs's
        // `alloc` feature needs a live heap before its first allocation (LFN
        // directory-scan scratch, handle-table strings). 96 KiB — plain `.bss`,
        // well within the RZ/A1L's on-chip SRAM.
        const EFATFS_ARENA_SIZE: usize = 96 * 1024;
        static mut EFATFS_ARENA: [u8; EFATFS_ARENA_SIZE] = [0; EFATFS_ARENA_SIZE];

        // SAFETY: `EFATFS_ARENA` is a function-local static this block alone ever
        // touches; this runs at most once (app_task runs once). `addr_of_mut!` +
        // `size_of_val(&*p)` (not `&mut EFATFS_ARENA` directly) avoids a
        // `static_mut_refs` reference to the static itself.
        let (base, size) = unsafe {
            let p = core::ptr::addr_of_mut!(EFATFS_ARENA);
            (p.cast::<u8>(), core::mem::size_of_val(&*p))
        };
        // SAFETY: `base`/`size` describe that same live, exclusively-owned
        // 'static arena; `deluge_heap_create` only ever writes within
        // `[base, base+size)`.
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

        match crate::efatfs_fs::mount().await {
            Ok(()) => log::info!("efatfs: mounted"),
            Err(status) => {
                log::warn!("efatfs: mount failed ({status}) — streamed samples will not load")
            }
        }
    }

    log::info!("deluge-rust: deluge_app_init() (registers + spawns task runners)");
    // deluge_app_init → registerTasks() spawns the per-task runners onto this
    // executor via scheduler::set_spawner's stashed spawner. They begin running as
    // soon as we yield below.
    unsafe { sys::deluge_app_init(board::deluge_board()) };
    boot::mark_finished();
    log::info!("deluge-rust: scheduler running");

    // Nothing left to pump: the scheduler's task runners were spawned by `deluge_app_init`.
    core::future::pending::<()>().await
}
