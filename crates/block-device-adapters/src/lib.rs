//! Block device adapters

#![cfg_attr(not(test), no_std)]

// MUST be the first module listed
mod fmt;

mod buf_stream;
mod stream_slice;

pub use buf_stream::{BufStream, BufStreamError};
pub use stream_slice::{StreamSlice, StreamSliceError};
