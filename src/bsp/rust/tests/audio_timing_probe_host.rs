//! Tests the audio-timing probe's per-interval statistics and its `AUDIO-TIMING` line.

use deluge_bsp_rust::audio::timing_probe::{INTERVAL_FRAMES, TimingStats, log2_bucket};

#[test]
fn buckets_are_log2_with_zero_in_the_first() {
    assert_eq!(log2_bucket(0, 14), 0);
    assert_eq!(log2_bucket(1, 14), 1);
    assert_eq!(log2_bucket(2, 14), 2);
    assert_eq!(log2_bucket(3, 14), 2);
    assert_eq!(log2_bucket(1024, 14), 11);
    assert_eq!(log2_bucket(u32::MAX, 14), 13);
}

#[test]
fn a_render_adds_to_the_sums_extremes_and_histograms() {
    let mut s = TimingStats::EMPTY;
    s.record_render(128, 900, 1500, 66);
    s.record_render(64, 800, 700, 30);
    assert_eq!(s.renders, 2);
    assert_eq!(s.frames, 192);
    assert_eq!(s.us, 2200);
    assert_eq!(s.frames_sq, 128 * 128 + 64 * 64);
    assert_eq!(s.frames_us, 128 * 1500 + 64 * 700);
    assert_eq!(s.max_us, 1500);
    assert_eq!(s.max_us_frames, 128);
    assert_eq!(s.min_queued, 800);
    assert_eq!(s.min_slack, 770);
    assert_eq!(s.overruns, 0);
    assert_eq!(s.us_hist[11], 1);
    assert_eq!(s.us_hist[10], 1);
    assert_eq!(s.frames_hist[8], 1);
    assert_eq!(s.frames_hist[7], 1);
}

#[test]
fn a_render_that_outlasts_the_queue_is_an_overrun() {
    let mut s = TimingStats::EMPTY;
    s.record_render(128, 100, 3000, 132);
    assert_eq!(s.overruns, 1);
    assert_eq!(s.min_slack, -32);
}

#[test]
fn the_wake_lateness_keeps_its_maximum() {
    let mut s = TimingStats::EMPTY;
    s.record_wake_late(12);
    s.record_wake_late(40);
    s.record_wake_late(3);
    assert_eq!(s.max_wake_late_us, 40);
}

#[test]
fn an_interval_ends_after_two_seconds_of_frames() {
    let mut s = TimingStats::EMPTY;
    assert!(!s.interval_done());
    s.frames = INTERVAL_FRAMES - 1;
    assert!(!s.interval_done());
    s.frames = INTERVAL_FRAMES;
    assert!(s.interval_done());
}

#[test]
fn the_line_names_every_field_with_comma_separated_histograms() {
    let mut s = TimingStats::EMPTY;
    s.record_render(128, 900, 1500, 66);
    s.record_wake_late(25);
    let line = s.to_string();
    assert!(
        line.starts_with("AUDIO-TIMING renders=1 frames=128 us=1500 "),
        "{line}"
    );
    for key in [
        "f2=16384",
        "fus=192000",
        "max_us=1500",
        "max_us_frames=128",
        "min_queued=900",
        "min_slack=834",
        "overruns=0",
        "max_wake_late_us=25",
        "us_hist=0,0,0,0,0,0,0,0,0,0,0,1,0,0",
        "frames_hist=0,0,0,0,0,0,0,0,1",
    ] {
        assert!(line.contains(key), "missing {key}: {line}");
    }
    assert!(!line.contains(", "), "{line}");
}
