//! ThreadSanitizer entry point for the scheduler/fiber exercise.
//!
//! A plain `fn main()` binary, NOT a `#[test]`: `cargo test`'s `--test` harness
//! needs `libtest`, and building `libtest` for a custom `-Zbuild-std`
//! sanitized target hits a reproducible nightly Cargo bug (`E0152: duplicate
//! lang item ... sized`, `core` built twice for one unit — reproduced without
//! `--features sanitize` too, so it's a `-Zbuild-std`/host==target-triple
//! issue, not specific to this crate). A plain example binary needs no
//! `libtest` and sidesteps it entirely. Run via `cargo run --example
//! scheduler_host_tsan`; see `HOST_HARNESS.md` for the exact TSan invocation
//! (custom target JSON + `-Zbuild-std=core,alloc,std,panic_abort`) and verdict.
//!
//! Shares the actual exercise body with the normal `cargo test` entry point
//! (`tests/scheduler_host.rs`) via `tests/support/scheduler_host_exercise.rs`
//! — see that file's header for the full rationale (bin-only crate, no
//! `[lib]` target, `#[path]`-recompiles the real `scheduler.rs`/`fiber.rs`
//! unmodified).
#![cfg(not(target_os = "none"))]
#![feature(impl_trait_in_assoc_type)]

#[path = "../tests/support/scheduler_host_exercise.rs"]
mod exercise;
#[path = "../src/fiber.rs"]
mod fiber;
#[path = "../src/scheduler.rs"]
mod scheduler;
// scheduler.rs reaches sim_block for the bounded progress wait; declare it the same way.
#[path = "../src/sim_block.rs"]
mod sim_block;

fn main() {
    exercise::run();
    println!("scheduler_host_tsan: PASSED");
}
