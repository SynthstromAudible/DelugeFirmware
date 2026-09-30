//! The board's peripherals behind the libdeluge headers.

/// board.h — capability descriptor + GPIO/audio/CV bring-up. Compiled on host
/// too under `host_app` (the descriptor/probe are pure data/logic; the GPIO/CV
/// bring-up bodies get host no-op siblings — see board.rs).
#[cfg(any(target_os = "none", feature = "host_app"))]
pub mod board;
/// control_surface.h — pads/buttons/encoders + LEDs (M2b WIP).
pub mod control;
/// cv_gate.h — CV/gate outputs + external trigger clock.
#[cfg(target_os = "none")]
pub(crate) mod cv_gate;
/// display.h — main OLED output over deluge_bsp::oled.
pub mod display;
/// `libdeluge/fault.h`: the board half of the portable pad-grid crash reporter — the
/// blocking PIC transport and the code/stack ranges, both usable from a fault vector.
#[cfg(target_os = "none")]
pub(crate) mod fault;
/// flash.h — persistent settings flash over deluge_bsp::flash / spibsc.
pub(crate) mod flash;
/// midi_io.h — DIN MIDI over deluge_bsp::uart (+ USB-MIDI peripheral, see usb).
#[cfg(target_os = "none")]
pub(crate) mod midi;
/// signals.h — board GPIO signals, battery, MIDI/gate timer.
#[cfg(target_os = "none")]
pub(crate) mod signals;
/// USB device bring-up — USB-MIDI 1.0 peripheral (Deluge → computer).
#[cfg(target_os = "none")]
pub(crate) mod usb;
