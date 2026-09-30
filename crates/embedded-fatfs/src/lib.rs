//! A FAT filesystem library implemented in Rust.
//!
//! # Usage
//!
//! This crate is [on crates.io](https://crates.io/crates/embedded-fatfs) and can be
//! used by adding `fatfs` to the dependencies in your project's `Cargo.toml`.
//!
//! ```toml
//! [dependencies]
//! embeded-fatfs = "0.1"
//! ```
//!
//! # Examples
//!
//! ```rust,no_run
//! use tokio::fs;
//! use embedded_io_async::Write;
//!
//! #[tokio::main]
//! async fn main() -> anyhow::Result<()> {
//!     # fs::copy("resources/fat16.img", "tmp/fat.img").await?;
//!     // Initialize a filesystem object
//!     let img_file = fs::OpenOptions::new().read(true).write(true)
//!         .open("tmp/fat.img").await?;
//!     let buf_stream = tokio::io::BufStream::new(img_file);
//!     let fs = embedded_fatfs::FileSystem::new(buf_stream, embedded_fatfs::FsOptions::new()).await?;
//!     let root_dir = fs.root_dir();
//!
//!     // Write a file
//!     root_dir.create_dir("foo").await?;
//!     let mut file = root_dir.create_file("foo/hello.txt").await?;
//!     file.truncate().await?;
//!     file.write_all(b"Hello World!").await?;
//!     file.flush().await?;
//!
//!     // Read a directory
//!     let dir = root_dir.open_dir("foo").await?;
//!     let mut iter = dir.iter();
//!     while let Some(r) = iter.next().await {
//!         let entry = r?;
//!         println!("{}", entry.file_name());
//!     }
//!     # fs::remove_file("tmp/fat.img").await?;
//!     # Ok(())
//! }
//! ```

#![crate_type = "lib"]
#![crate_name = "embedded_fatfs"]
#![cfg_attr(not(feature = "std"), no_std)]
// Disable warnings to not clutter code with cfg too much
#![cfg_attr(not(all(feature = "alloc", feature = "lfn")), allow(dead_code, unused_imports))]
#![warn(clippy::pedantic)]
// #![warn(missing_docs)]
#![allow(
    clippy::module_name_repetitions,
    clippy::cast_possible_truncation,
    clippy::bool_to_int_with_if, // less readable
    clippy::uninlined_format_args, // not supported before Rust 1.58.0
)]

#[cfg(feature = "alloc")]
extern crate alloc;

// MUST be the first module listed
mod fmt;

mod boot_sector;
mod dir;
mod dir_entry;
mod error;
mod file;
mod fs;
mod io;
mod table;
mod time;

pub use crate::dir::*;
pub use crate::dir_entry::*;
pub use crate::error::*;
pub use crate::file::*;
pub use crate::fs::*;
pub use crate::time::*;
