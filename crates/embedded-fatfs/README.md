embedded-fatfs
===========

[![CI Status](https://github.com/mabezdev/embedded-fatfs/actions/workflows/ci.yml/badge.svg)](https://github.com/mabezdev/embedded-fatfs/actions/workflows/ci.yml)
[![MIT licensed](https://img.shields.io/badge/license-MIT-blue.svg)](./LICENSE.txt)
[![crates.io](https://img.shields.io/crates/v/embedded-fatfs)](https://crates.io/crates/embedded-fatfs)
[![Documentation](https://docs.rs/embedded-fatfs/badge.svg)](https://docs.rs/embedded-fatfs)
![Minimum rustc version](https://img.shields.io/badge/rustc-1.75+-green.svg)

A FAT filesystem library implemented in Rust. Built on the shoulders of the amazing [rust-fatfs](https://github.com/rafalh/rust-fatfs) crate by [@rafalh](https://github.com/rafalh).

## Features
* async
* read/write to files using `embedded-io-async` Read/Write traits
* read directory contents
* create/remove file or directory
* rename/move file or directory
* read/write file timestamps (updated automatically if `chrono` feature is enabled)
* format volume
* FAT12, FAT16, FAT32 compatibility
* LFN (Long File Names) extension is supported
* `no_std` environment support

## Porting from rust-fatfs to embedded-fatfs

There a are a few key differences between the crates:

- embedded-fatfs is async, therefore your storage device must implement the [embedded-io-async](https://github.com/rust-embedded/embedded-hal/tree/master/embedded-io-async) traits.
- You must call `flush` on `File`s before they are dropped. See the CHANGELOG for details.

`no_std` usage
------------

Add this to your `Cargo.toml`:

    [dependencies]
    embedded-fatfs = { version = "0.1", default-features = false }

Additional features:

* `lfn` - LFN (long file name) support
* `alloc` - use `alloc` crate for dynamic allocation. Needed for API which uses `String` type. You may have to provide
a memory allocator implementation.
* `unicode` - use Unicode-compatible case conversion in file names - you may want to have it disabled for lower memory
footprint

License
-------
The MIT license. See `LICENSE`.
