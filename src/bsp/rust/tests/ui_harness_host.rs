//! Host tests for the UI harness's shadows and input door: what the app sends is what a snapshot reads,
//! and what a script pushes is what the app polls.
use deluge_bsp_rust::peripherals::{control, display, ui_harness};
use deluge_bsp_rust::sys::{
    DelugeColour, DelugeInputEvent, DelugeInputEventKind_DELUGE_EVENT_PAD as KIND_PAD,
};
use std::sync::{Mutex, MutexGuard};

fn serial() -> MutexGuard<'static, ()> {
    static SERIAL: Mutex<()> = Mutex::new(());
    let g = SERIAL.lock().unwrap_or_else(|e| e.into_inner());
    ui_harness::reset_for_test();
    g
}

#[test]
fn a_column_pair_lands_on_both_columns() {
    let _g = serial();
    let mut colours = [DelugeColour { r: 0, g: 0, b: 0 }; 16];
    colours[0] = DelugeColour { r: 1, g: 2, b: 3 }; // column 2, row 0
    colours[15] = DelugeColour { r: 4, g: 5, b: 6 }; // column 3, row 7
    control::deluge_control_set_pad_columns(1, colours.as_ptr(), 16);
    let pads =
        unsafe { core::slice::from_raw_parts(ui_harness::host_script_pad_shadow(), 32 * 8 * 3) };
    assert_eq!(&pads[(2 * 8) * 3..(2 * 8) * 3 + 3], &[1, 2, 3]);
    assert_eq!(&pads[(3 * 8 + 7) * 3..(3 * 8 + 7) * 3 + 3], &[4, 5, 6]);
}

#[test]
fn leds_and_rings_are_recorded() {
    let _g = serial();
    control::deluge_control_set_led(5, true);
    let levels = [9u8, 8, 7];
    control::deluge_control_set_indicator(1, levels.as_ptr(), 3);
    let leds = unsafe { core::slice::from_raw_parts(ui_harness::host_script_led_shadow(), 64) };
    assert_eq!(leds[5], 1);
    assert!(
        ui_harness::host_script_ring_shadow(0).is_null(),
        "ring 0 was never written"
    );
    let ring = unsafe { core::slice::from_raw_parts(ui_harness::host_script_ring_shadow(1), 4) };
    assert_eq!(ring, &[9, 8, 7, 0]);
}

#[test]
fn the_oled_shadow_is_null_until_the_first_blit() {
    let _g = serial();
    let (mut w, mut h) = (0u16, 0u16);
    assert!(ui_harness::host_script_oled_shadow(0, &mut w, &mut h).is_null());
    let frame = [0xA5u8; 768];
    display::deluge_display_blit_oled(frame.as_ptr(), 128, 48);
    let p = ui_harness::host_script_oled_shadow(0, &mut w, &mut h);
    assert_eq!((w, h), (128, 48));
    assert_eq!(unsafe { *p.add(767) }, 0xA5);
    assert!(
        ui_harness::host_script_oled_shadow(1, &mut w, &mut h).is_null(),
        "one panel"
    );
}

#[test]
fn a_pushed_pad_is_what_the_app_polls() {
    let _g = serial();
    ui_harness::host_script_push_event(KIND_PAD, 3, 4, 200);
    let mut ev = DelugeInputEvent {
        kind: 0,
        x: 0,
        y: 0,
        value: 0,
    };
    assert!(control::deluge_control_poll_event(&mut ev));
    assert_eq!((ev.kind, ev.x, ev.y, ev.value), (KIND_PAD, 3, 4, 200));
}

#[test]
fn encoder_edges_reach_the_app_by_its_own_ordinal() {
    let _g = serial();
    ui_harness::host_script_add_encoder_edges(3, 4); // the app's select encoder
    assert_eq!(control::deluge_encoder_take_quadrature_edges(3), 4);
    assert_eq!(
        control::deluge_encoder_take_quadrature_edges(3),
        0,
        "taken once"
    );
}

#[test]
fn encoder_saturates_not_wraps() {
    let _g = serial();
    ui_harness::host_script_add_encoder_edges(0, 100);
    ui_harness::host_script_add_encoder_edges(0, 100);
    assert_eq!(control::deluge_encoder_take_quadrature_edges(0), 127);
}

#[test]
fn the_pad_grid_is_the_deluge_s_with_its_sidebar() {
    let _g = serial();
    let (mut cols, mut rows) = (0u8, 0u8);
    ui_harness::host_script_pad_bounds(&mut cols, &mut rows);
    assert_eq!((cols, rows), (18, 8));
    assert_eq!(ui_harness::host_script_display_count(), 1);
}
