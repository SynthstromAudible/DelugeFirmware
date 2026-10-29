//! `libdeluge/board.h` — the capability descriptor and the ordered one-time board bring-up (GPIO
//! for LEDs, codec and jack detects, the audio SSI). Pin and port numbers stay BSP-internal. The CV
//! DAC and its SPI bus come up earlier, in `runtime::device::start`, before the executor runs.
//!
//! OLED bring-up is not here: [`display`](super::display) and `deluge_bsp::oled` own the panel.
//!
//! On host (under `host_app`) the descriptor and OLED probe compile unchanged; the hardware
//! bring-up functions are logging no-ops.
#![allow(non_snake_case)]

use crate::sys::{DelugeBoard, DelugeDisplayKind_DELUGE_DISPLAY_OLED};

// Pin map (as definitions_cxx.hpp names them):
//   BATTERY_LED {1,1}  SYNCED_LED {6,7}  CODEC {6,12}  SPEAKER_ENABLE {4,1}
//   HEADPHONE_DETECT {6,5}  LINE_IN_DETECT {6,6}  MIC_DETECT {7,9}
//   LINE_OUT_DETECT_L {6,3}  LINE_OUT_DETECT_R {6,4}
//   ANALOG_CLOCK_IN {1,14}  VOLT_SENSE {1,13}
//   SPI_CLK {6,0}  SPI_MOSI {6,2}  SPI_SSL {6,1}

static BOARD_NAME: &[u8] = b"Synthstrom Deluge (144-pad, OLED)\0";

/// The board descriptor, made `Sync`: `DelugeBoard` holds a `*const c_char`, but the descriptor is
/// immutable, so sharing it is sound.
struct BoardDescriptor(DelugeBoard);
unsafe impl Sync for BoardDescriptor {}

static DELUGE_BOARD: BoardDescriptor = BoardDescriptor(DelugeBoard {
    name: BOARD_NAME.as_ptr() as *const core::ffi::c_char,
    pad_grid_width: 16,
    pad_grid_height: 8,
    button_rows: 4, // NUM_BUTTON_ROWS
    button_cols: 9, // NUM_BUTTON_COLS
    encoder_count: 6,
    cv_channels: 2,   // NUM_PHYSICAL_CV_CHANNELS
    gate_channels: 4, // NUM_GATE_CHANNELS
    display_kind: DelugeDisplayKind_DELUGE_DISPLAY_OLED,
    display_width: 128, // OLED_MAIN_WIDTH_PIXELS
    display_height: 48, // OLED_MAIN_HEIGHT_PIXELS
    audio_sample_rate_hz: 44_100,
    audio_in_channels: 2,
    audio_out_channels: 2,
});

/// Returns the board's static capability descriptor.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_board() -> *const DelugeBoard {
    &DELUGE_BOARD.0
}

/// Reports that an OLED is fitted.
///
/// TODO: read the real OLED-present flag (PIC firmware-version byte, bit 7) to support the
/// 7-segment variant.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_board_probe_oled() -> bool {
    true
}

/// Early bring-up. Nothing left to do: `runtime::device::start` hands the LEDs, the jack detects
/// and speaker enable, the voltage-sense input and the trigger input to their drivers (each owns
/// its pins), and `cv_gate` owns the SPI SSL pin. The 7-segment variant's hardware SSL (P6_1,
/// alternative 3) is not set up: this firmware drives an OLED, and the app always passes `true`.
#[cfg(target_os = "none")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_board_init_early(_have_oled: bool) {}

/// Host stand-in: there is no hardware to bring up. Logs once so the boot trace shows the call.
#[cfg(feature = "host_app")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_board_init_early(_have_oled: bool) {
    use core::sync::atomic::{AtomicBool, Ordering};
    static LOGGED: AtomicBool = AtomicBool::new(false);
    if !LOGGED.swap(true, Ordering::Relaxed) {
        log::info!("stub: deluge_board_init_early (host, no GPIO/CV-DAC hardware)");
    }
}

/// SSI0 and its DMA channels, from `runtime::device::start` until the app's
/// `deluge_board_init_audio` brings the codec up (its place in the C++ init sequence).
#[cfg(target_os = "none")]
static AUDIO_PARTS: embassy_sync::blocking_mutex::Mutex<
    embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex,
    core::cell::RefCell<
        Option<(
            rza1_hal::Peri<'static, deluge_bsp::board::AudioSsi>,
            deluge_bsp::board::AudioPins,
            rza1_hal::Peri<'static, deluge_bsp::board::SsiTxDma>,
            rza1_hal::Peri<'static, deluge_bsp::board::SsiRxDma>,
        )>,
    >,
> = embassy_sync::blocking_mutex::Mutex::new(core::cell::RefCell::new(None));

/// Keep the audio parts for [`deluge_board_init_audio`].
#[cfg(target_os = "none")]
pub(crate) fn stash_audio(
    ssi: rza1_hal::Peri<'static, deluge_bsp::board::AudioSsi>,
    pins: deluge_bsp::board::AudioPins,
    tx: rza1_hal::Peri<'static, deluge_bsp::board::SsiTxDma>,
    rx: rza1_hal::Peri<'static, deluge_bsp::board::SsiRxDma>,
) {
    AUDIO_PARTS.lock(|p| *p.borrow_mut() = Some((ssi, pins, tx, rx)));
}

/// Brings up the audio serial port (SSI0).
#[cfg(target_os = "none")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_board_init_audio() {
    let (ssi, pins, tx, rx) = AUDIO_PARTS
        .lock(|p| p.borrow_mut().take())
        .expect("deluge_board_init_audio runs once, after runtime::device::start");
    // Powers the codec (P6_12) too.
    deluge_bsp::audio::init(ssi, pins, tx, rx);
}

/// Host stand-in: there is no SSI0 to bring up. Logs once so the boot trace shows the call.
#[cfg(feature = "host_app")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_board_init_audio() {
    use core::sync::atomic::{AtomicBool, Ordering};
    static LOGGED: AtomicBool = AtomicBool::new(false);
    if !LOGGED.swap(true, Ordering::Relaxed) {
        log::info!("stub: deluge_board_init_audio (host, no SSI0 hardware)");
    }
}

/// Storage bring-up (SPIBSC serial flash). TODO: currently a no-op.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_board_init_storage() {}

/// Unlocks the L2 cache data region locked during boot.
///
/// TODO: currently a no-op, which is safe — the locked region only reduces the usable cache.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_board_unlock_data_cache() {}

abi_pin!(
    deluge_board,
    deluge_board_init_audio,
    deluge_board_init_early,
    deluge_board_init_storage,
    deluge_board_probe_oled,
    deluge_board_unlock_data_cache,
);
