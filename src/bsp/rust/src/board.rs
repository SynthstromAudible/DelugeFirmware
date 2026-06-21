//! board.h — capability descriptor + ordered one-time board bring-up, over the
//! deluge-sdk HAL/BSP. Mirrors the hardware steps in the C reference BSP
//! (src/bsp/rza1/board.c): GPIO direction/mux for LEDs/codec/detects, the CV DAC
//! SPI, the audio SSI, etc. All pin/port numbers stay BSP-internal here.
//!
//! M2b TODO: the OLED path (probe + the shared-SPI / DMA / PIC bring-up and the
//! display driver) is not wired yet — `deluge_board_probe_oled` returns false so
//! the app takes the 7-segment path and the superloop runs without needing the
//! OLED+PIC stack. Flip to real OLED detection + bring-up with display.h/control.
#![allow(non_snake_case)]

use rza1l_hal::gpio;

use crate::sys::{DelugeBoard, DelugeDisplayKind_DELUGE_DISPLAY_OLED};

// Pin map (mirrors src/bsp/rza1/board.c / definitions_cxx.hpp):
//   BATTERY_LED {1,1}  SYNCED_LED {6,7}  CODEC {6,12}  SPEAKER_ENABLE {4,1}
//   HEADPHONE_DETECT {6,5}  LINE_IN_DETECT {6,6}  MIC_DETECT {7,9}
//   LINE_OUT_DETECT_L {6,3}  LINE_OUT_DETECT_R {6,4}
//   ANALOG_CLOCK_IN {1,14}  VOLT_SENSE {1,13}
//   SPI_CLK {6,0}  SPI_MOSI {6,2}  SPI_SSL {6,1}

static BOARD_NAME: &[u8] = b"Synthstrom Deluge (144-pad, OLED)\0";

/// `DelugeBoard` holds a `*const c_char` (not `Sync`); the descriptor is
/// immutable, so sharing it across threads is sound.
struct BoardDescriptor(DelugeBoard);
unsafe impl Sync for BoardDescriptor {}

static DELUGE_BOARD: BoardDescriptor = BoardDescriptor(DelugeBoard {
    name: BOARD_NAME.as_ptr() as *const core::ffi::c_char,
    pad_grid_width: 16,
    pad_grid_height: 8,
    button_rows: 4,  // NUM_BUTTON_ROWS
    button_cols: 9,  // NUM_BUTTON_COLS
    encoder_count: 6,
    cv_channels: 2,  // NUM_PHYSICAL_CV_CHANNELS
    gate_channels: 4, // NUM_GATE_CHANNELS
    display_kind: DelugeDisplayKind_DELUGE_DISPLAY_OLED,
    display_width: 128, // OLED_MAIN_WIDTH_PIXELS
    display_height: 48, // OLED_MAIN_HEIGHT_PIXELS
    audio_sample_rate_hz: 44_100,
    audio_in_channels: 2,
    audio_out_channels: 2,
});

#[unsafe(no_mangle)]
pub extern "C" fn deluge_board() -> *const DelugeBoard {
    &DELUGE_BOARD.0
}

/// Report that an OLED is fitted. The hardware these BSP bring-up units run on
/// has the OLED (the PIC firmware-version byte also has its high bit set, 0x81),
/// so the app takes the OLED path; display.rs's oled_render task drives the panel.
/// M-later: read the actual OLED-present flag (PIC fw byte bit 7) rather than
/// hard-coding, for 7-segment-variant support.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_board_probe_oled() -> bool {
    true
}

/// Early bring-up: GPIO direction + initial state for status LEDs, codec/speaker
/// enables, jack detects, analog sense + trigger-clock input, and the CV DAC SPI.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_board_init_early(have_oled: bool) {
    unsafe {
        // Status LEDs / audio-path enables (direction + off state).
        gpio::write(1, 1, true); // BATTERY_LED off (open-drain: 1 = off)
        gpio::set_as_output(1, 1);
        gpio::write(6, 7, false); // SYNCED_LED off
        gpio::set_as_output(6, 7);
        gpio::write(6, 12, false); // Codec control off
        gpio::set_as_output(6, 12);
        gpio::write(4, 1, false); // Speaker / amp control off
        gpio::set_as_output(4, 1);

        // Detect inputs.
        gpio::set_as_input(6, 5); // Headphone detect
        gpio::set_as_input(6, 6); // Line-in detect
        gpio::set_as_input(7, 9); // Mic detect
        gpio::set_as_input(6, 3); // Line-out detect L
        gpio::set_as_input(6, 4); // Line-out detect R

        // Analog sense + trigger-clock input.
        gpio::set_pin_mux(1, 13, 1); // VOLT_SENSE
        gpio::set_pin_mux(1, 14, 2); // ANALOG_CLOCK_IN

        // CV DAC over RSPI0 (the BSP driver sets up the SPI peripheral + pins).
        deluge_bsp::cv_gate::init();

        // OLED shares RSPI0 (manual SSL); without it, mux SSL as the 7-seg path.
        // M2b: when have_oled, bring up the shared-SPI SSL/interrupts/DMA here.
        if !have_oled {
            gpio::set_pin_mux(6, 1, 3); // SPI_SSL
        }
    }
}

/// Bring up the audio serial (SSI0) port.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_board_init_audio() {
    // SAFETY: called once after init_early; clocks/STB up.
    unsafe { deluge_bsp::audio::init() };
}

/// Storage-phase bring-up (SPIBSC serial flash). M4 TODO.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_board_init_storage() {}

/// Unlock the L2 cache data region locked during boot. M-later TODO (the locked
/// region just reduces cache slightly; safe to no-op for now).
#[unsafe(no_mangle)]
pub extern "C" fn deluge_board_unlock_data_cache() {}
