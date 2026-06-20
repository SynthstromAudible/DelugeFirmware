//! M0 stubs for app/BSP symbols that live OUTSIDE the libdeluge C-ABI headers:
//! BSP globals/functions the app references directly (USB-host, card, trigger
//! clock), the FatFS `disk_*` diskio glue, NE10 DSP entry points (see note), and
//! a couple of C-runtime/linker shims. Replaced with real impls per milestone.
#![allow(non_upper_case_globals, unused_variables)]

// --- BSP globals the app reads (cf. src/bsp/{host,rza1}) ---
#[unsafe(no_mangle)]
pub static mut anythingInitiallyAttachedAsUSBHost: u8 = 0;
#[unsafe(no_mangle)]
pub static mut currentlyAccessingCard: u8 = 0;
#[unsafe(no_mangle)]
pub static mut triggerClockRisingEdgesReceived: u32 = 0;
#[unsafe(no_mangle)]
pub static mut triggerClockRisingEdgesProcessed: u32 = 0;
/// uint32_t triggerClockRisingEdgeTimes[TRIGGER_CLOCK_INPUT_NUM_TIMES_STORED].
#[unsafe(no_mangle)]
pub static mut triggerClockRisingEdgeTimes: [u32; 16] = [0; 16];

// --- USB host/peripheral control (BSP) ---
#[unsafe(no_mangle)]
pub extern "C" fn openUSBHost() {}
#[unsafe(no_mangle)]
pub extern "C" fn closeUSBHost() {}
#[unsafe(no_mangle)]
pub extern "C" fn openUSBPeripheral() {}

// --- FatFS diskio glue (maps to deluge_block_* in a real BSP) ---
#[unsafe(no_mangle)]
pub extern "C" fn disk_initialize(pdrv: u8) -> u8 {
    0
}
#[unsafe(no_mangle)]
pub extern "C" fn disk_status(pdrv: u8) -> u8 {
    0
}
#[unsafe(no_mangle)]
pub extern "C" fn disk_ioctl(pdrv: u8, cmd: u8, buff: *mut core::ffi::c_void) -> i32 {
    0
}
#[unsafe(no_mangle)]
pub extern "C" fn disk_read_without_streaming_first(
    pdrv: u8,
    buff: *mut u8,
    sector: u32,
    count: u32,
) -> i32 {
    0
}
#[unsafe(no_mangle)]
pub extern "C" fn disk_write_without_streaming_first(
    pdrv: u8,
    buff: *const u8,
    sector: u32,
    count: u32,
) -> i32 {
    0
}
#[unsafe(no_mangle)]
pub extern "C" fn disk_timerproc() {}
#[unsafe(no_mangle)]
pub extern "C" fn get_fattime() -> u32 {
    0
}

// --- The app's FREEZE_WITH_ERROR macro calls this BSP fault reporter. ---
#[unsafe(no_mangle)]
pub extern "C" fn fault_handler_print_freeze_pointers(
    sys_lr: u32,
    sys_sp: u32,
    usr_lr: u32,
    usr_sp: u32,
) {
}

// --- C runtime shim: with -nostartfiles there's no crtn _fini. ---
#[unsafe(no_mangle)]
pub extern "C" fn _fini() {}

// --- NE10 DSP entry points. TODO: the Debug libNE10.a doesn't compile the FFT/
// FIR/IIR sources; investigate the NE10 CMake. Stubbed so M0 links; these must
// become real before the FFT analysis path is exercised. ---
macro_rules! ne10_stub {
    ($($name:ident),+ $(,)?) => { $(
        #[unsafe(no_mangle)]
        pub extern "C" fn $name() -> i32 { 0 }
    )+ };
}
ne10_stub!(
    ne10_fft_alloc_c2c_float32_c,
    ne10_fft_c2c_1d_float32_c,
    ne10_fft_c2c_1d_float32_neon,
    ne10_fft_c2c_1d_int16_c,
    ne10_fft_c2c_1d_int16_neon,
    ne10_fft_c2r_1d_float32_c,
    ne10_fft_c2r_1d_float32_neon,
    ne10_fft_c2r_1d_int16_c,
    ne10_fft_c2r_1d_int16_neon,
    ne10_fft_r2c_1d_float32_c,
    ne10_fft_r2c_1d_float32_neon,
    ne10_fft_r2c_1d_int16_c,
    ne10_fft_r2c_1d_int16_neon,
    ne10_fir_decimate_float_c,
    ne10_fir_float_c,
    ne10_fir_interpolate_float_c,
    ne10_fir_lattice_float_c,
    ne10_fir_sparse_float_c,
    ne10_iir_lattice_float_c,
);
