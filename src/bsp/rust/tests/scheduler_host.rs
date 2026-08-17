//! Drive the REAL `scheduler.rs` + `fiber.rs` task/fiber concurrency on a host
//! `platform-std` Embassy executor.
//!
//! The normal (unsanitized) `cargo test` entry point. The exercise body is
//! shared with the ThreadSanitizer entry point (`examples/scheduler_host_tsan.rs`)
//! via `tests/support/scheduler_host_exercise.rs` — see that file's header for
//! why two entry points exist. See `HOST_HARNESS.md` for the TSan invocation
//! and verdict.
#![cfg(not(target_os = "none"))]
#![feature(impl_trait_in_assoc_type)]

#[path = "support/scheduler_host_exercise.rs"]
mod exercise;
#[path = "../src/fiber.rs"]
mod fiber;
#[path = "../src/scheduler.rs"]
mod scheduler;
// scheduler.rs reaches sim_block for the bounded progress wait; declare it the same way.
#[path = "../src/sim_block.rs"]
mod sim_block;

#[test]
fn scheduler_host_exercise() {
    exercise::run();
}
