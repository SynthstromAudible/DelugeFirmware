//! The USB audio probe's line.

use deluge_platform_device::audio::usb_service_probe::UsbDmaStats;

#[test]
fn a_line_reports_both_streams_and_the_interrupt() {
    let s = UsbDmaStats {
        in_packets: 16000,
        in_frames: 88200,
        in_stops: 2,
        in_ahead: Some((38, 63)),
        out_frames: 88150,
        interrupt_us: 1234,
    };
    assert_eq!(
        s.to_string(),
        "USB-AUDIO-TIMING in_packets=16000 in_frames=88200 in_stops=2 in_ahead_min=38 in_ahead_max=63 out_frames=88150 \
         interrupt_us=1234"
    );
}

/// A stream that queued nothing has no range to report.
#[test]
fn a_queue_never_used_is_left_out() {
    let s = UsbDmaStats {
        in_ahead: None,
        ..UsbDmaStats::EMPTY
    };
    assert_eq!(
        s.to_string(),
        "USB-AUDIO-TIMING in_packets=0 in_frames=0 in_stops=0 out_frames=0 interrupt_us=0"
    );
}
