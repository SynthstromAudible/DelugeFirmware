//! Host correctness/regression harness for the vendored `embedded-fatfs`,
//! driven over one in-RAM disk image.
pub mod block_dev;
pub mod diff;
pub mod efatfs;
pub mod ops;
pub mod ram_disk;
