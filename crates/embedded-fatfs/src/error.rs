use core::fmt::Debug;
pub(crate) use embedded_io_async::{Error as IoError, ErrorKind, ReadExactError};

/// Error enum with all errors that can be returned by functions from this crate
///
/// Generic parameter `T` is a type of external error returned by the user provided storage
#[cfg_attr(feature = "defmt", derive(defmt::Format))]
#[derive(Debug)]
#[non_exhaustive]
pub enum Error<T> {
    /// A user provided storage instance returned an error during an input/output operation.
    Io(T),
    /// A read operation cannot be completed because an end of a file has been reached prematurely.
    UnexpectedEof,
    /// A write operation cannot be completed because `Write::write` returned 0.
    WriteZero,
    /// A parameter was incorrect.
    InvalidInput,
    /// A requested file or directory has not been found.
    NotFound,
    /// A file or a directory with the same name already exists.
    AlreadyExists,
    /// An operation cannot be finished because a directory is not empty.
    DirectoryIsNotEmpty,
    /// File system internal structures are corrupted/invalid.
    CorruptedFileSystem,
    /// There is not enough free space on the storage to finish the requested operation.
    NotEnoughSpace,
    /// The provided file name is either too long or empty.
    InvalidFileNameLength,
    /// The provided file name contains an invalid character.
    UnsupportedFileNameCharacter,
}

impl<T: Debug + core::fmt::Display> IoError for Error<T> {
    fn kind(&self) -> ErrorKind {
        ErrorKind::Other
    }
}

impl<T: IoError + Debug + core::fmt::Display> From<T> for Error<T> {
    fn from(error: T) -> Self {
        Error::Io(error)
    }
}

impl<T> From<ReadExactError<Error<T>>> for Error<T> {
    fn from(error: ReadExactError<Error<T>>) -> Self {
        match error {
            ReadExactError::UnexpectedEof => Self::UnexpectedEof,
            ReadExactError::Other(error) => error,
        }
    }
}

impl<T: IoError + Debug + core::fmt::Display> From<ReadExactError<T>> for Error<T> {
    fn from(error: ReadExactError<T>) -> Self {
        match error {
            ReadExactError::UnexpectedEof => Self::UnexpectedEof,
            ReadExactError::Other(error) => error.into(),
        }
    }
}

impl<T: core::fmt::Display> core::fmt::Display for Error<T> {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        match self {
            Error::Io(io_error) => write!(f, "IO error: {}", io_error),
            Error::UnexpectedEof => write!(f, "Unexpected end of file"),
            Error::NotEnoughSpace => write!(f, "Not enough space"),
            Error::WriteZero => write!(f, "Write zero"),
            Error::InvalidInput => write!(f, "Invalid input"),
            Error::InvalidFileNameLength => write!(f, "Invalid file name length"),
            Error::UnsupportedFileNameCharacter => write!(f, "Unsupported file name character"),
            Error::DirectoryIsNotEmpty => write!(f, "Directory is not empty"),
            Error::NotFound => write!(f, "No such file or directory"),
            Error::AlreadyExists => write!(f, "File or directory already exists"),
            Error::CorruptedFileSystem => write!(f, "Corrupted file system"),
        }
    }
}

impl<T: Debug + core::fmt::Display> core::error::Error for Error<T> {}
