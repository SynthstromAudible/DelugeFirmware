//! The USB audio service probe's gap statistics.

use deluge_platform_device::audio::usb_service_probe::{StreamGaps, UsbServiceStats};

#[test]
fn the_first_service_has_no_gap() {
    let mut s = StreamGaps::EMPTY;
    s.record(10_000, 5);
    assert_eq!(s.packets, 1);
    assert_eq!(s.frames, 5);
    assert_eq!(s.late, 0);
    assert_eq!(s.max_gap_us, 0);
}

#[test]
fn a_gap_the_buffers_cover_is_not_late() {
    let mut s = StreamGaps::EMPTY;
    s.record(0, 5);
    s.record(250, 6);
    assert_eq!(s.frames, 11);
    assert_eq!(s.late, 0);
    assert_eq!(s.missed, 0);
    assert_eq!(s.max_gap_us, 250);
}

#[test]
fn a_longer_gap_counts_missed_microframes() {
    let mut s = StreamGaps::EMPTY;
    s.record(0, 5);
    s.record(500, 5);
    assert_eq!((s.late, s.missed), (1, 2));
    s.record(2_000, 5);
    assert_eq!((s.late, s.missed), (2, 12));
    assert_eq!(s.max_gap_us, 1_500);
}

#[test]
fn a_restart_is_not_a_gap() {
    let mut s = StreamGaps::EMPTY;
    s.record(0, 5);
    s.restart();
    s.record(5_000_000, 5);
    assert_eq!(s.packets, 2);
    assert_eq!(s.late, 0);
    assert_eq!(s.max_gap_us, 0);
}

#[test]
fn the_clock_wraps() {
    let mut s = StreamGaps::EMPTY;
    s.record(u32::MAX - 99, 5);
    s.record(25, 5);
    assert_eq!(s.max_gap_us, 125);
    assert_eq!(s.late, 0);
}

#[test]
fn take_keeps_the_last_service() {
    let mut s = StreamGaps::EMPTY;
    s.record(0, 5);
    let taken = s.take();
    assert_eq!((taken.packets, taken.frames), (1, 5));
    s.record(500, 6);
    assert_eq!((s.packets, s.frames), (1, 6));
    assert_eq!((s.late, s.missed), (1, 2));
}

#[test]
fn the_line_names_both_streams() {
    let mut stats = UsbServiceStats {
        to_host: StreamGaps::EMPTY,
        from_host: StreamGaps::EMPTY,
        interrupt_us: 1_234,
    };
    stats.to_host.record(0, 5);
    stats.to_host.record(500, 6);
    stats.from_host.record(0, 7);
    let line = stats.to_string();
    assert_eq!(
        line,
        "USB-AUDIO-TIMING in_packets=2 in_frames=11 in_late=1 in_missed=2 in_max_gap_us=500 \
         out_packets=1 out_frames=7 out_late=0 out_missed=0 out_max_gap_us=0 interrupt_us=1234"
    );
}
