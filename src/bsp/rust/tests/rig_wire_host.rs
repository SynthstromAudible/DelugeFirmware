//! Host test for the rig harness's up-lines: each panel change as one `@` line on RTT channel 0,
//! numbered so the host can tell when one was lost.

use deluge_bsp_rust::peripherals::rig_harness::shadow::{Change, Shadow};
use deluge_bsp_rust::peripherals::rig_harness::wire;

fn line(change: Change, s: &Shadow) -> String {
    let mut out = String::new();
    wire::encode(&change, s, 17, 1500, &mut out).unwrap();
    out
}

#[test]
fn encodes_each_line_kind() {
    let mut s = Shadow::new();
    s.record_oled(&[0xab; 768], 128, 48);
    let mut rgb = [0u8; 48];
    rgb[0] = 0x0f;
    rgb[47] = 0xf0;
    s.record_columns(2, &rgb);
    s.record_led(7, true);
    s.record_ring(1, [1, 2, 3, 4]);
    s.record_pic(Some(5), Some(20));

    let oled = line(Change::Oled, &s);
    assert!(oled.starts_with("@O 17 1500 128 48 abab"));
    assert_eq!(oled.len(), "@O 17 1500 128 48 ".len() + 768 * 2 + 1);
    let cols = line(Change::Columns(2), &s);
    assert!(cols.starts_with("@C 17 1500 2 0f0000"));
    assert!(cols.ends_with("f0\n"));
    assert_eq!(cols.len(), "@C 17 1500 2 ".len() + 96 + 1);
    assert_eq!(line(Change::Led(7), &s), "@L 17 1500 7 1\n");
    assert_eq!(line(Change::Led(8), &s), "@L 17 1500 8 0\n");
    assert_eq!(line(Change::Ring(1), &s), "@G 17 1500 1 1 2 3 4\n");
    assert_eq!(line(Change::Flash(5, 3), &s), "@F 17 1500 5 3\n");
    assert_eq!(line(Change::Pic, &s), "@M 17 1500 5 20\n");
    assert_eq!(line(Change::Scroll, &s), "@X 17 1500\n");

    let mut out = String::new();
    wire::heartbeat(18, 1600, &mut out).unwrap();
    wire::dropped(19, 1600, 4, &mut out).unwrap();
    wire::ready(20, 1600, &mut out).unwrap();
    assert_eq!(out, "@T 18 1600\n@D 19 1600 4\n@R 20 1600\n");
}

#[test]
fn full_state_lists_every_pad_column_and_led() {
    let s = Shadow::new();
    let mut lines = Vec::new();
    let mut seq = 100;
    wire::full_state(&s, &mut seq, 2000, |l| lines.push(l.to_owned())).unwrap();
    let count = |p: &str| lines.iter().filter(|l| l.starts_with(p)).count();
    assert_eq!(count("@O "), 1);
    assert_eq!(count("@C "), 9);
    assert_eq!(count("@L "), 64);
    assert_eq!(count("@G "), 2);
    assert_eq!(count("@M "), 1);
    assert_eq!(lines.len(), 1 + 9 + 64 + 2 + 1);
    // Every line takes the next number.
    assert!(lines[0].starts_with("@O 100 2000 "));
    assert!(
        lines
            .last()
            .unwrap()
            .starts_with(&format!("@M {} 2000 ", 100 + lines.len() - 1))
    );
    assert_eq!(seq, 100 + lines.len() as u32);
}
