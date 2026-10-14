//! The Deluge file upgrader: reads a song or preset as the firmware reads it, converts every form the latest
//! releases no longer write, and writes the firmware's JSON.
#![forbid(unsafe_code)]

pub mod passes;
pub mod report;
pub mod tree;
pub mod upgrade;
pub mod version;

/// Why a file could not be upgraded.
#[derive(Debug, thiserror::Error)]
pub enum UpgradeError {
    /// The file is not a document the firmware would read.
    #[error("not a Deluge document: {0}")]
    Malformed(&'static str),
    /// The file could not be read or written.
    #[error(transparent)]
    Io(#[from] std::io::Error),
}
