//! The `sim_latency` model through the `deluge_block_write`/`deluge_block_read` C ABI.
//!
//! A write and a read-back of one sector, from a thread that is not the executor's, must each take
//! at least the modeled command overhead and round-trip the bytes unchanged. `sim_latency::pump`
//! runs on its own executor thread and the spin uses the threaded-host hook, as in the preemptive
//! host harness.
//!
//! Run with `cargo test --features sim_latency --test sim_latency_host`.
#![cfg(all(not(target_os = "none"), feature = "sim_latency"))]
#![feature(impl_trait_in_assoc_type)]

use deluge_bsp_rust::boot;
use deluge_bsp_rust::storage::sd;
use deluge_bsp_rust::storage::sim_block;
use deluge_bsp_rust::tier;
use deluge_bsp_rust::tier_meter;

use std::time::{Duration, Instant};

use embassy_executor::{Executor, Spawner};

/// The sector written and read back, in this test's own image.
const TEST_SECTOR: u32 = 4000;
const PATTERN_LEN: usize = 512;

/// Modeled command overhead: large enough to tell a modeled transfer from a plain file access.
/// The throughput below makes the per-byte term negligible.
const OVERHEAD_US: u32 = 150_000;
const THROUGHPUT_BYTES_PER_SEC: u32 = 1_000_000_000;

fn pattern() -> [u8; PATTERN_LEN] {
    let mut p = [0u8; PATTERN_LEN];
    for (i, b) in p.iter_mut().enumerate() {
        *b = (i as u8).wrapping_mul(97).wrapping_add(13);
    }
    p
}

#[test]
fn modeled_write_then_read_round_trips_and_takes_the_modeled_time() {
    // Its own image: the host shim's default path is shared across the process.
    let img = std::env::temp_dir().join(format!(
        "deluge-sim-latency-host-{}.img",
        std::process::id()
    ));
    // SAFETY: set before any other thread in this test binary exists.
    unsafe { std::env::set_var("DELUGE_SD_IMAGE", &img) };
    sim_block::block_on(deluge_bsp::sd::init()).expect("the host card initializes");

    sim_block::use_threaded_host_hook();
    sd::sim_latency::set_command_overhead_us(OVERHEAD_US);
    sd::sim_latency::set_throughput_bytes_per_sec(THROUGHPUT_BYTES_PER_SEC);

    std::thread::Builder::new()
        .name("deluge-sim-latency-host".into())
        .spawn(|| {
            let executor: &'static mut Executor = Box::leak(Box::new(Executor::new()));
            executor.run(|spawner: Spawner| {
                // Without the pump no modeled transfer ever completes.
                spawner.spawn(sd::sim_latency::pump().unwrap());
            });
        })
        .expect("spawning the host executor thread");

    // The threaded hook never reports a wedge, so bound the round trip with a timeout.
    let (tx, rx) = std::sync::mpsc::channel();
    std::thread::spawn(move || {
        round_trip();
        tx.send(()).unwrap();
    });
    rx.recv_timeout(Duration::from_secs(20))
        .expect("the round trip did not finish within 20s (or panicked; see its output above)");

    let _ = std::fs::remove_file(&img);
}

fn round_trip() {
    let pat = pattern();
    let write_start = Instant::now();
    let status = sd::deluge_block_write(0, pat.as_ptr(), TEST_SECTOR, 1);
    let write_elapsed = write_start.elapsed();
    assert_eq!(status, 0, "deluge_block_write failed: status={status}");
    assert!(
        write_elapsed >= Duration::from_micros(OVERHEAD_US as u64),
        "the write returned in {write_elapsed:?}, faster than the modeled {OVERHEAD_US}us overhead"
    );

    let mut buf = [0u8; PATTERN_LEN];
    let read_start = Instant::now();
    let status = sd::deluge_block_read(0, buf.as_mut_ptr(), TEST_SECTOR, 1);
    let read_elapsed = read_start.elapsed();
    assert_eq!(status, 0, "deluge_block_read failed: status={status}");
    assert!(
        read_elapsed >= Duration::from_micros(OVERHEAD_US as u64),
        "the read returned in {read_elapsed:?}, faster than the modeled {OVERHEAD_US}us overhead"
    );
    assert!(
        buf == pat,
        "the read returned bytes that differ from the ones written"
    );
}
