//! Host test for the rig harness's panel shadow: what the app last sent to the OLED and the PIC,
//! reported once per change so the device can stream only what moved.

use deluge_bsp_rust::peripherals::rig_harness::shadow::{Change, Shadow};

fn drain(s: &mut Shadow) -> Vec<Change> {
    s.take_changes().collect()
}

#[test]
fn shadow_reports_each_change_once() {
    let mut s = Shadow::new();
    s.record_oled(&[0x5a; 768], 128, 48);
    s.record_columns(3, &[7; 48]);
    s.record_led(7, true);
    s.record_ring(1, [1, 2, 3, 4]);
    s.record_flash(5, 0b11);
    assert_eq!(
        drain(&mut s),
        vec![
            Change::Oled,
            Change::Columns(3),
            Change::Led(7),
            Change::Ring(1),
            Change::Flash(5, 0b11),
        ]
    );
    assert_eq!(drain(&mut s), vec![]);

    // An unchanged value is no change; a flash is an event, so each one counts.
    s.record_led(7, true);
    s.record_columns(3, &[7; 48]);
    s.record_flash(5, 0b11);
    s.record_flash(5, 0b11);
    assert_eq!(
        drain(&mut s),
        vec![Change::Flash(5, 0b11), Change::Flash(5, 0b11)]
    );
}

#[test]
fn shadow_keeps_the_values_it_reports() {
    let mut s = Shadow::new();
    s.record_columns(8, &[9; 48]);
    s.record_ring(0, [4, 3, 2, 1]);
    s.record_pic(Some(5), None);
    s.record_pic(None, Some(20));
    assert_eq!(s.columns(8), &[9; 48]);
    assert_eq!(s.ring(0), [4, 3, 2, 1]);
    assert_eq!(s.pic(), (5, 20));
    assert_eq!(
        drain(&mut s),
        vec![Change::Columns(8), Change::Ring(0), Change::Pic]
    );
}

#[test]
fn scrolls_and_overflowing_flashes_are_reported() {
    let mut s = Shadow::new();
    s.record_scroll();
    for i in 0..40 {
        s.record_flash(i, 0);
    }
    let got = drain(&mut s);
    assert_eq!(got[0], Change::Scroll);
    // The flash queue holds FLASH_QUEUE entries; the rest are counted, not lost silently.
    assert_eq!(got.len(), 1 + Shadow::FLASH_QUEUE);
    assert_eq!(s.take_dropped(), 40 - Shadow::FLASH_QUEUE as u32);
    assert_eq!(s.take_dropped(), 0);
}
