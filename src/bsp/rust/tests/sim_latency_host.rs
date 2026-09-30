//! `sim_latency` round trip: a modeled host SD write, then a modeled read of the same sector, both through the
//! real `deluge_block_write`/`deluge_block_read` C ABI from a thread that is not the executor's. Each transfer must
//! take at least the modeled command overhead (the model is applied) and the read must return exactly the bytes
//! written (the model changes timing only). `sim_latency::pump` runs on its own std executor thread, and the spin
//! uses the threaded-host progress hook, the shape the preemptive host harness gives them.
//!
//! Requires the `sim_latency` feature:
//!   cargo test --features sim_latency --test sim_latency_host
#![cfg(all(not(target_os = "none"), feature = "sim_latency"))]
#![feature(impl_trait_in_assoc_type)]

#[macro_use]
#[path = "support/abi_pin_noop.rs"]
mod abi_pin_noop;

// No `deluge_bsp_rust::` path exists (bin-only crate) — re-include the modules by path, the same way the other
// `tests/*_host.rs` files do.
#[path = "../src/boot.rs"]
mod boot;
#[path = "../src/sd.rs"]
mod sd;
#[path = "../src/sim_block.rs"]
mod sim_block;
#[path = "../src/tier.rs"]
mod tier;
#[path = "../src/tier_meter.rs"]
mod tier_meter;

use std::time::{Duration, Instant};

use embassy_executor::{Executor, Spawner};

/// Sector this test writes and reads, in its own image (see below).
const TEST_SECTOR: u32 = 4000;
const PATTERN_LEN: usize = 512;

/// Throughput high enough that the `bytes / throughput` term is negligible, so each transfer's modeled delay is
/// close to the fixed command overhead: large enough to tell apart from an unmodeled file access.
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
                // Without it nothing consumes a modeled transfer's request and the transfer never completes.
                spawner.spawn(sd::sim_latency::pump().unwrap());
            });
        })
        .expect("spawning the host executor thread");

    // The threaded hook never reports a wedge, so a model that never completes would hang: bound it here instead.
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
