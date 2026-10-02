use core::cmp;
use core::fmt::Debug;
use embedded_io_async::{Read, Seek, SeekFrom, Write};

#[cfg_attr(feature = "defmt", derive(defmt::Format))]
#[derive(Debug)]
#[non_exhaustive]
pub enum StreamSliceError<T: Debug> {
    InvalidSeek(i64),
    WriteZero,
    Other(T),
}

impl<E: Debug> From<E> for StreamSliceError<E> {
    fn from(e: E) -> Self {
        Self::Other(e)
    }
}

impl<E: Debug> core::fmt::Display for StreamSliceError<E> {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        match self {
            StreamSliceError::InvalidSeek(pos) => write!(f, "Invalid seek position: {pos}"),
            StreamSliceError::WriteZero => write!(f, "Write zero bytes"),
            StreamSliceError::Other(e) => write!(f, "Other error: {e:?}"),
        }
    }
}

/// Stream wrapper for accessing limited segment of data from underlying file or device.
pub struct StreamSlice<T: Read + Write + Seek> {
    inner: T,
    start_offset: u64,
    current_offset: u64,
    size: u64,
}

impl<E: Debug> core::error::Error for StreamSliceError<E> {}

impl<E: Debug> embedded_io_async::Error for StreamSliceError<E> {
    fn kind(&self) -> embedded_io_async::ErrorKind {
        match self {
            StreamSliceError::InvalidSeek(_) => embedded_io_async::ErrorKind::InvalidInput,
            StreamSliceError::Other(_) | StreamSliceError::WriteZero => {
                embedded_io_async::ErrorKind::Other
            }
        }
    }
}

impl<T: Read + Write + Seek> embedded_io_async::ErrorType for StreamSlice<T> {
    type Error = StreamSliceError<T::Error>;
}

impl<T: Read + Write + Seek> StreamSlice<T> {
    /// Creates new `StreamSlice` from inner stream and offset range.
    ///
    /// `start_offset` is inclusive offset of the first accessible byte.
    /// `end_offset` is exclusive offset of the first non-accessible byte.
    /// `start_offset` must be lower or equal to `end_offset`.
    pub async fn new(
        mut inner: T,
        start_offset: u64,
        end_offset: u64,
    ) -> Result<Self, StreamSliceError<T::Error>> {
        debug_assert!(end_offset >= start_offset);
        inner.seek(SeekFrom::Start(start_offset)).await?;
        let size = end_offset - start_offset;
        Ok(StreamSlice {
            start_offset,
            size,
            inner,
            current_offset: 0,
        })
    }

    /// Returns inner object
    pub fn into_inner(self) -> T {
        self.inner
    }
}

impl<T: Read + Write + Seek> Read for StreamSlice<T> {
    async fn read(&mut self, buf: &mut [u8]) -> Result<usize, StreamSliceError<T::Error>> {
        if buf.is_empty() {
            // A zero-length write is a no-op; forwarding it would be misreported as `WriteZero` or cause other issues
            return Ok(0);
        }
        // Narrow only after the min, so a remaining size exactly divisible by 4GiB isn't truncated to
        // 0 by `as usize` on 32-bit targets
        let remaining = self.size - self.current_offset;
        let max_read_size = cmp::min(remaining, buf.len() as u64) as usize;
        let bytes_read = self.inner.read(&mut buf[..max_read_size]).await?;
        self.current_offset += bytes_read as u64;
        Ok(bytes_read)
    }
}

impl<T: Read + Write + Seek> Write for StreamSlice<T> {
    async fn write(&mut self, buf: &[u8]) -> Result<usize, StreamSliceError<T::Error>> {
        if buf.is_empty() {
            // A zero-length write is a no-op; forwarding it would be misreported as `WriteZero` or cause other issues
            return Ok(0);
        }
        // Narrow only after the min, so a remaining size exactly divisible by 4GiB isn't truncated to
        // 0 by `as usize` on 32-bit targets
        let remaining = self.size - self.current_offset;
        let max_write_size = cmp::min(remaining, buf.len() as u64) as usize;
        let bytes_written = self.inner.write(&buf[..max_write_size]).await?;
        if bytes_written == 0 {
            return Err(StreamSliceError::WriteZero);
        }
        self.current_offset += bytes_written as u64;
        Ok(bytes_written)
    }

    async fn flush(&mut self) -> Result<(), StreamSliceError<T::Error>> {
        self.inner.flush().await?;
        Ok(())
    }
}

impl<T: Read + Write + Seek> Seek for StreamSlice<T> {
    async fn seek(&mut self, pos: SeekFrom) -> Result<u64, StreamSliceError<T::Error>> {
        let new_offset = match pos {
            SeekFrom::Current(x) => i64::try_from(self.current_offset)
                .ok()
                .and_then(|o| o.checked_add(x))
                .ok_or(StreamSliceError::InvalidSeek(i64::MAX))?,
            SeekFrom::Start(x) => {
                i64::try_from(x).map_err(|_| StreamSliceError::InvalidSeek(i64::MAX))?
            }
            SeekFrom::End(x) => self.size as i64 + x,
        };
        if new_offset < 0 || new_offset as u64 > self.size {
            Err(StreamSliceError::InvalidSeek(new_offset))
        } else {
            self.inner
                .seek(SeekFrom::Start(self.start_offset + new_offset as u64))
                .await?;
            self.current_offset = new_offset as u64;
            Ok(self.current_offset)
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[tokio::test]
    async fn stream_test() {
        let _ = env_logger::builder().is_test(true).try_init();
        let buf = "BeforeTest dataAfter".to_string().into_bytes();
        let cur = std::io::Cursor::new(buf);
        let mut stream =
            StreamSlice::new(embedded_io_adapters::tokio_1::FromTokio::new(cur), 6, 6 + 9)
                .await
                .unwrap();

        let data = read_to_string(&mut stream).await.unwrap();
        assert_eq!(data, "Test data");

        stream.seek(SeekFrom::Start(5)).await.unwrap();
        let data = read_to_string(&mut stream).await.unwrap();
        assert_eq!(data, "data");

        stream.seek(SeekFrom::Start(5)).await.unwrap();
        stream.write_all("Rust".as_bytes()).await.unwrap();
        assert!(stream.write_all("X".as_bytes()).await.is_err());
        stream.seek(SeekFrom::Start(0)).await.unwrap();
        let data = read_to_string(&mut stream).await.unwrap();
        assert_eq!(data, "Test Rust");
    }

    #[tokio::test]
    async fn empty_transfers_are_noops() {
        let cur = std::io::Cursor::new("BeforeTest dataAfter".to_string().into_bytes());
        let mut stream =
            StreamSlice::new(embedded_io_adapters::tokio_1::FromTokio::new(cur), 6, 6 + 9)
                .await
                .unwrap();

        // Empty write must be Ok(0), not `WriteZero`, and must not advance.
        assert_eq!(stream.write(&[]).await.unwrap(), 0);
        assert_eq!(stream.read(&mut []).await.unwrap(), 0);
        assert_eq!(stream.seek(SeekFrom::Current(0)).await.unwrap(), 0);

        // Data is still intact / readable afterwards.
        let data = read_to_string(&mut stream).await.unwrap();
        assert_eq!(data, "Test data");
    }

    /// A zero-storage `Read + Write + Seek` device with a virtual size, so a
    /// multi-GiB `StreamSlice` can be tested without allocating anything.
    struct Sink {
        pos: u64,
        len: u64,
    }

    impl embedded_io_async::ErrorType for Sink {
        type Error = core::convert::Infallible;
    }

    impl Read for Sink {
        async fn read(&mut self, buf: &mut [u8]) -> Result<usize, Self::Error> {
            let n = cmp::min(self.len - self.pos, buf.len() as u64) as usize;
            self.pos += n as u64;
            Ok(n)
        }
    }

    impl Write for Sink {
        async fn write(&mut self, buf: &[u8]) -> Result<usize, Self::Error> {
            let n = cmp::min(self.len - self.pos, buf.len() as u64) as usize;
            self.pos += n as u64;
            Ok(n)
        }

        async fn flush(&mut self) -> Result<(), Self::Error> {
            Ok(())
        }
    }

    impl Seek for Sink {
        async fn seek(&mut self, pos: SeekFrom) -> Result<u64, Self::Error> {
            self.pos = match pos {
                SeekFrom::Start(x) => x,
                SeekFrom::Current(x) => (self.pos as i64 + x) as u64,
                SeekFrom::End(x) => (self.len as i64 + x) as u64,
            };
            Ok(self.pos)
        }
    }

    #[tokio::test]
    async fn transfers_past_4gib_boundary_not_truncated() {
        const G: u64 = 1024 * 1024 * 1024;
        let mut slice = StreamSlice::new(Sink { pos: 0, len: 8 * G }, 0, 8 * G)
            .await
            .unwrap();

        let data = [0xAAu8; 512];
        assert_eq!(slice.write(&data).await.unwrap(), data.len());

        slice.seek(SeekFrom::Start(4 * G)).await.unwrap(); // remaining == 2^32
        let data = [0xAAu8; 512];
        assert_eq!(slice.write(&data).await.unwrap(), data.len());

        slice.seek(SeekFrom::Start(4 * G)).await.unwrap();
        let mut rbuf = [0u8; 512];
        assert_eq!(slice.read(&mut rbuf).await.unwrap(), rbuf.len());
    }

    async fn read_to_string<IO: embedded_io_async::Read>(io: &mut IO) -> Result<String, IO::Error> {
        let mut buf = Vec::new();
        loop {
            let mut tmp = [0; 256];
            match io.read(&mut tmp).await {
                Ok(0) => break,
                Ok(n) => buf.extend(&tmp[..n]),
                Err(e) => return Err(e),
            }
        }

        Ok(String::from_utf8(buf).unwrap())
    }
}
