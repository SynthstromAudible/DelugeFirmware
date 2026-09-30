//! Host tests for the audio tier's render-time average.

use deluge_bsp_rust::audio::render;

/// The average is `(avg + sample) / 2` per recorded tick, starting from zero.
#[test]
fn average_halves_toward_each_sample() {
    render::reset_average_for_test();
    assert_eq!(render::average_tick_us(), 0);
    render::record_tick_us(1000);
    assert_eq!(render::average_tick_us(), 500);
    render::record_tick_us(1000);
    assert_eq!(render::average_tick_us(), 750);
    render::record_tick_us(0);
    assert_eq!(render::average_tick_us(), 375);
}
