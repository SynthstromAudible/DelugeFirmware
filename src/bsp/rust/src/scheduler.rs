//! The BSP clock the app reads (`getSystemTime`), from `libdeluge/scheduler.h`.
#![allow(non_snake_case)]

use embassy_time::Instant;

fn us_to_secs(us: u64) -> f64 {
    us as f64 / 1_000_000.0
}

#[unsafe(no_mangle)]
pub extern "C" fn getSystemTime() -> f64 {
    us_to_secs(Instant::now().as_micros())
}

abi_pin!(getSystemTime);
