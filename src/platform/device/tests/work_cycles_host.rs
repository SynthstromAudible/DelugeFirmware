//! The work-cycle reading: the cycle counter less the time metered interrupts took.

use deluge_platform_device::work_cycles::read;

#[test]
fn it_is_the_counter_less_the_interrupt_time() {
    assert_eq!(read(|| 300, || 1_000), 700);
}

#[test]
fn an_interrupt_between_the_reads_is_read_again() {
    // The first pass sees the meter move from 0 to 400 around its counter read; the second is quiet.
    let mut meter = [0, 400, 400, 400].into_iter();
    let mut counter = [1_000, 1_500].into_iter();
    assert_eq!(
        read(|| meter.next().unwrap(), || counter.next().unwrap()),
        1_100
    );
    assert_eq!(meter.next(), None);
    assert_eq!(counter.next(), None);
}

#[test]
fn both_counts_wrap() {
    let start = read(|| u32::MAX - 49, || u32::MAX - 9);
    let end = read(|| 150, || 990);
    // 1,000 cycles passed and 200 of them were the interrupt's.
    assert_eq!(end.wrapping_sub(start), 800);
}
