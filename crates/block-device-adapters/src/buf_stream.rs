use aligned::Aligned;
use block_device_driver::{slice_to_blocks, slice_to_blocks_mut, BlockDevice};
use embedded_io_async::{ErrorKind, Read, Seek, SeekFrom, Write};

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
#[cfg_attr(feature = "defmt", derive(defmt::Format))]
#[non_exhaustive]
pub enum BufStreamError<T> {
    Io(T),
}

impl<T> From<T> for BufStreamError<T> {
    fn from(t: T) -> Self {
        BufStreamError::Io(t)
    }
}

impl<T: core::fmt::Debug> core::fmt::Display for BufStreamError<T> {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        match self {
            BufStreamError::Io(e) => write!(f, "IO error: {e:?}"),
        }
    }
}

impl<T: core::fmt::Debug> core::error::Error for BufStreamError<T> {}

impl<T: core::fmt::Debug> embedded_io_async::Error for BufStreamError<T> {
    fn kind(&self) -> ErrorKind {
        ErrorKind::Other
    }
}

/// A Stream wrapper for accessing a stream in block sized chunks.
///
/// [`BufStream<T, const SIZE: usize, const ALIGN: usize`](BufStream) can be initialized with the following parameters.
///
/// - `T`: The inner stream.
/// - `SIZE`: The size of the block, this dictates the size of the internal buffer.
/// - `ALIGN`: The alignment of the internal buffer.
///
/// If the `buf` provided to either [`Read::read`] or [`Write::write`] meets the following conditions the `buf`
/// will be used directly instead of the intermediate buffer to avoid unnecessary copies:
///
/// - `buf.len()` is a multiple of block size
/// - `buf` has the same alignment as the internal buffer
/// - The byte address of the inner device is aligned to a block size.
///
/// [`BufStream<T, const SIZE: usize, const ALIGN: usize`](BufStream) implements the [`embedded_io_async`] traits, and implicitly
/// handles the RMW (Read, Modify, Write) cycle for you.
pub struct BufStream<T: BlockDevice<SIZE>, const SIZE: usize> {
    inner: T,
    buffer: Aligned<T::Align, [u8; SIZE]>,
    current_block: u32,
    current_offset: u64,
    dirty: bool,
}

impl<T: BlockDevice<SIZE>, const SIZE: usize> BufStream<T, SIZE> {
    const ALIGN: usize = core::mem::align_of::<Aligned<T::Align, [u8; SIZE]>>();
    /// Create a new [`BufStream`] around a hardware block device.
    pub fn new(inner: T) -> Self {
        Self {
            inner,
            current_block: u32::MAX,
            current_offset: 0,
            buffer: Aligned([0; SIZE]),
            dirty: false,
        }
    }

    /// Returns inner object.
    pub fn into_inner(self) -> T {
        self.inner
    }

    #[inline]
    fn pointer_block_start_addr(&self) -> u64 {
        self.pointer_block_start() as u64 * SIZE as u64
    }

    #[inline]
    fn pointer_block_start(&self) -> u32 {
        (self.current_offset / SIZE as u64)
            .try_into()
            .expect("Block larger than 2TB")
    }

    async fn flush(&mut self) -> Result<(), T::Error> {
        // flush the internal buffer if we have modified the buffer
        if self.dirty {
            self.dirty = false;
            // Note, alignment of internal buffer is guarenteed at compile time so we don't have to check it here
            self.inner
                .write(self.current_block, slice_to_blocks(&self.buffer[..]))
                .await?;
        }
        Ok(())
    }

    async fn check_cache(&mut self) -> Result<(), T::Error> {
        let block_start = self.pointer_block_start();
        if block_start != self.current_block {
            // we may have modified data in old block, flush it to disk
            self.flush().await?;
            // We have seeked to a new block, read it
            let buf = &mut self.buffer[..];
            self.inner
                .read(block_start, slice_to_blocks_mut(buf))
                .await?;
            self.current_block = block_start;
        }
        Ok(())
    }
}

impl<T: BlockDevice<SIZE>, const SIZE: usize> embedded_io_async::ErrorType for BufStream<T, SIZE> {
    type Error = BufStreamError<T::Error>;
}

impl<T: BlockDevice<SIZE>, const SIZE: usize> Read for BufStream<T, SIZE> {
    async fn read(&mut self, mut buf: &mut [u8]) -> Result<usize, Self::Error> {
        // A zero-length read must be a no-op: forwarding it would otherwise
        // satisfy the block-aligned fast path below and issue a 0-block device
        // transfer (which some block devices, e.g. STM32 SDMMC DMA, reject).
        if buf.is_empty() {
            return Ok(0);
        }
        let mut total = 0;
        let target = buf.len();
        loop {
            let bytes_read = if buf.len() % SIZE == 0
                && buf.as_ptr().cast::<u8>() as usize % Self::ALIGN == 0
                && self.current_offset % SIZE as u64 == 0
            {
                // If the provided buffer has a suitable length and alignment _and_ the read head is on a block boundary, use it directly
                let block = self.pointer_block_start();
                self.inner.read(block, slice_to_blocks_mut(buf)).await?;

                buf.len()
            } else {
                let block_start = self.pointer_block_start_addr();
                let block_end = block_start + SIZE as u64;
                trace!(
                    "offset {}, block_start {}, block_end {}",
                    self.current_offset,
                    block_start,
                    block_end
                );

                self.check_cache().await?;

                // copy as much as possible, up to the block boundary
                let buffer_offset = (self.current_offset - block_start) as usize;
                let bytes_to_read = buf.len();

                let end = core::cmp::min(buffer_offset + bytes_to_read, SIZE);
                trace!("buffer_offset {}, end {}", buffer_offset, end);
                let bytes_read = end - buffer_offset;
                buf[..bytes_read].copy_from_slice(&self.buffer[buffer_offset..end]);
                buf = &mut buf[bytes_read..]; // move the buffer along

                bytes_read
            };

            self.current_offset += bytes_read as u64;
            total += bytes_read;

            if total == target {
                return Ok(total);
            }
        }
    }
}

impl<T: BlockDevice<SIZE>, const SIZE: usize> Write for BufStream<T, SIZE> {
    async fn write(&mut self, mut buf: &[u8]) -> Result<usize, Self::Error> {
        // A zero-length write must be a no-op: forwarding it would otherwise
        // satisfy the block-aligned fast path below and issue a 0-block device
        // transfer (which some block devices, e.g. STM32 SDMMC DMA, reject).
        if buf.is_empty() {
            return Ok(0);
        }
        let mut total = 0;
        let target = buf.len();
        loop {
            let bytes_written = if buf.len() % SIZE == 0
                && buf.as_ptr().cast::<u8>() as usize % Self::ALIGN == 0
                && self.current_offset % SIZE as u64 == 0
            {
                // If the provided buffer has a suitable length and alignment _and_ the write head is on a block boundary, use it directly
                let block = self.pointer_block_start();
                self.inner.write(block, slice_to_blocks(buf)).await?;

                buf.len()
            } else {
                let block_start = self.pointer_block_start_addr();
                let block_end = block_start + SIZE as u64;
                trace!(
                    "offset {}, block_start {}, block_end {}",
                    self.current_offset,
                    block_start,
                    block_end
                );

                // reload the cache if we need to
                self.check_cache().await?;

                // copy as much as possible, up to the block boundary
                let buffer_offset = (self.current_offset - block_start) as usize;
                let bytes_to_write = buf.len();

                let end = core::cmp::min(buffer_offset + bytes_to_write, SIZE);
                trace!("buffer_offset {}, end {}", buffer_offset, end);
                let bytes_written = end - buffer_offset;
                self.buffer[buffer_offset..buffer_offset + bytes_written]
                    .copy_from_slice(&buf[..bytes_written]);
                buf = &buf[bytes_written..]; // move the buffer along

                // If we haven't written directly, we will use the cache, which will may need to flush later
                // so we mark it as dirty
                self.dirty = true;

                // write out the whole block with the modified data
                if block_start + end as u64 == block_end {
                    trace!("Flushing sector cache");
                    self.flush().await?;
                }

                bytes_written
            };

            self.current_offset += bytes_written as u64;
            total += bytes_written;

            if total == target {
                return Ok(total);
            }
        }
    }

    async fn flush(&mut self) -> Result<(), Self::Error> {
        self.flush().await?;
        Ok(())
    }
}

impl<T: BlockDevice<SIZE>, const SIZE: usize> Seek for BufStream<T, SIZE> {
    async fn seek(&mut self, pos: SeekFrom) -> Result<u64, Self::Error> {
        self.current_offset = match pos {
            SeekFrom::Start(x) => x,
            SeekFrom::End(x) => {
                let size = self.inner.size().await? as i64;
                (size + x).max(0) as u64
            }
            SeekFrom::Current(x) => {
                i64::try_from(self.current_offset)
                    .ok()
                    .and_then(|o| o.checked_add(x))
                    .map(|o| o.max(0) as u64)
                    .unwrap_or(0)
            }
        };
        Ok(self.current_offset)
    }
}

#[cfg(test)]
mod tests {
    use aligned::A4;
    use embedded_io_async::ErrorType;

    use super::*;

    struct TestBlockDevice<T: Read + Write + Seek>(T);

    impl<T: Read + Write + Seek> ErrorType for TestBlockDevice<T> {
        type Error = T::Error;
    }

    impl<T: Read + Write + Seek> Read for TestBlockDevice<T> {
        async fn read(&mut self, buf: &mut [u8]) -> Result<usize, Self::Error> {
            Ok(self.0.read(buf).await?)
        }
    }

    impl<T: Read + Write + Seek> Write for TestBlockDevice<T> {
        async fn write(&mut self, buf: &[u8]) -> Result<usize, Self::Error> {
            Ok(self.0.write(buf).await?)
        }

        async fn flush(&mut self) -> Result<(), Self::Error> {
            self.0.flush().await
        }
    }

    impl<T: Read + Write + Seek> Seek for TestBlockDevice<T> {
        async fn seek(&mut self, pos: SeekFrom) -> Result<u64, Self::Error> {
            Ok(self.0.seek(pos).await?)
        }
    }

    impl<T: Read + Write + Seek> BlockDevice<512> for TestBlockDevice<T> {
        type Error = T::Error;
        type Align = aligned::A4;

        /// Read one or more blocks at the given block address.
        async fn read(
            &mut self,
            block_address: u32,
            data: &mut [Aligned<Self::Align, [u8; 512]>],
        ) -> Result<(), Self::Error> {
            self.0
                .seek(SeekFrom::Start((block_address * 512).into()))
                .await?;
            for b in data {
                self.0.read(&mut b[..]).await?;
            }
            Ok(())
        }

        /// Write one or more blocks at the given block address.
        async fn write(
            &mut self,
            block_address: u32,
            data: &[Aligned<Self::Align, [u8; 512]>],
        ) -> Result<(), Self::Error> {
            self.0
                .seek(SeekFrom::Start((block_address * 512).into()))
                .await?;
            for b in data {
                self.0.write(&b[..]).await?;
            }
            Ok(())
        }

        async fn size(&mut self) -> Result<u64, Self::Error> {
            Ok(u64::MAX)
        }
    }

    #[tokio::test]
    async fn block_512_read_test() {
        let _ = env_logger::builder().is_test(true).try_init();
        let buf = ("A".repeat(512) + "B".repeat(512).as_str()).into_bytes();
        let cur = std::io::Cursor::new(buf);
        let mut block: BufStream<_, 512> = BufStream::new(TestBlockDevice(
            embedded_io_adapters::tokio_1::FromTokio::new(cur),
        ));

        // Test sector aligned access
        let mut buf = vec![0; 128];
        block.seek(SeekFrom::Start(0)).await.unwrap();
        block.read_exact(&mut buf[..]).await.unwrap();
        assert_eq!(buf, "A".repeat(128).into_bytes());

        let mut buf = vec![0; 128];
        block.seek(SeekFrom::Start(512)).await.unwrap();
        block.read_exact(&mut buf[..]).await.unwrap();
        assert_eq!(buf, "B".repeat(128).into_bytes());

        // Read across sectors
        let mut buf = vec![0; 128];
        block.seek(SeekFrom::Start(512 - 64)).await.unwrap();
        block.read_exact(&mut buf[..]).await.unwrap();
        assert_eq!(buf, ("A".repeat(64) + "B".repeat(64).as_str()).into_bytes());
    }

    #[tokio::test]
    async fn block_512_read_successive() {
        let _ = env_logger::builder().is_test(true).try_init();
        let buf = ("A".repeat(64) + "B".repeat(64).as_str())
            .repeat(16)
            .into_bytes();
        let cur = std::io::Cursor::new(buf);
        let mut block: BufStream<_, 512> = BufStream::new(TestBlockDevice(
            embedded_io_adapters::tokio_1::FromTokio::new(cur),
        ));

        // Test sector aligned access
        let mut buf = vec![0; 64];
        block.seek(SeekFrom::Start(0)).await.unwrap();
        block.read_exact(&mut buf[..]).await.unwrap();
        assert_eq!(buf, "A".repeat(64).into_bytes());

        let mut buf = vec![0; 64];
        block.seek(SeekFrom::Start(64)).await.unwrap();
        block.read_exact(&mut buf[..]).await.unwrap();
        assert_eq!(buf, "B".repeat(64).into_bytes());

        let mut buf = vec![0; 64];
        block.seek(SeekFrom::Start(32)).await.unwrap();
        block.read_exact(&mut buf[..]).await.unwrap();
        assert_eq!(buf, ("A".repeat(32) + "B".repeat(32).as_str()).into_bytes());
    }

    #[tokio::test]
    async fn block_512_write_single_sector() {
        let _ = env_logger::builder().is_test(true).try_init();
        let buf = vec![0; 2048];
        let cur = std::io::Cursor::new(buf);
        let mut block: BufStream<_, 512> = BufStream::new(TestBlockDevice(
            embedded_io_adapters::tokio_1::FromTokio::new(cur),
        ));

        // Test sector aligned access
        let data_a = "A".repeat(512).into_bytes();
        block.seek(SeekFrom::Start(0)).await.unwrap();
        block.write_all(&data_a).await.unwrap();
        assert_eq!(
            &block.into_inner().0.into_inner().into_inner()[..512],
            data_a
        )
    }

    #[tokio::test]
    async fn block_512_write_across_sectors() {
        let _ = env_logger::builder().is_test(true).try_init();
        let buf = vec![0; 2048];
        let cur = std::io::Cursor::new(buf);
        let mut block: BufStream<_, 512> = BufStream::new(TestBlockDevice(
            embedded_io_adapters::tokio_1::FromTokio::new(cur),
        ));

        // Test sector aligned access
        let data_a = "A".repeat(512).into_bytes();
        block.seek(SeekFrom::Start(256)).await.unwrap();
        block.write_all(&data_a).await.unwrap();
        block.flush().await.unwrap();
        let buf = block.into_inner().0.into_inner().into_inner();
        assert_eq!(&buf[..256], [0; 256]);
        assert_eq!(&buf[256..768], data_a);
        assert_eq!(&buf[768..1024], [0; 256]);
    }

    #[tokio::test]
    async fn aligned_write_block_optimization() {
        let _ = env_logger::builder().is_test(true).try_init();
        let buf = vec![0; 2048];
        let cur = std::io::Cursor::new(buf);
        let mut block: BufStream<_, 512> = BufStream::new(TestBlockDevice(
            embedded_io_adapters::tokio_1::FromTokio::new(cur),
        ));

        let mut aligned_buffer: Aligned<A4, [u8; 512]> = Aligned([0; 512]);
        let data_a = "A".repeat(512).into_bytes();
        aligned_buffer[..].copy_from_slice(&data_a[..]);
        block.seek(SeekFrom::Start(0)).await.unwrap();
        block.write_all(&aligned_buffer[..]).await.unwrap();

        // if we wrote directly, the block buffer will be empty
        assert_eq!(&block.buffer[..], [0u8; 512]);
        // ensure that the current offset is still updated
        assert_eq!(block.current_offset, 512);
        // the write suceeded
        assert_eq!(
            &block.into_inner().0.into_inner().into_inner()[..512],
            &data_a
        )
    }

    #[tokio::test]
    async fn aligned_write_block_optimization_misaligned_block() {
        let _ = env_logger::builder().is_test(true).try_init();
        let buf = vec![0; 2048];
        let cur = std::io::Cursor::new(buf);
        let mut block: BufStream<_, 512> = BufStream::new(TestBlockDevice(
            embedded_io_adapters::tokio_1::FromTokio::new(cur),
        ));

        let mut aligned_buffer: Aligned<A4, [u8; 2048]> = Aligned([0; 2048]);
        let data_a = "A".repeat(512).into_bytes();
        aligned_buffer[..512].copy_from_slice(&data_a[..]);
        // seek away from aligned block address
        block.seek(SeekFrom::Start(3)).await.unwrap();
        // attempt write all
        block.write_all(&aligned_buffer[..512]).await.unwrap();
        block.flush().await.unwrap();

        // because the addr was not block aligned, we will have used the cache
        assert_ne!(&block.buffer[..], [0u8; 512]);
        // the write suceeded
        assert_eq!(
            &block.into_inner().0.into_inner().into_inner()[3..515],
            &data_a
        )
    }

    #[tokio::test]
    async fn aligned_read_block_optimization() {
        let _ = env_logger::builder().is_test(true).try_init();
        let buf = "A".repeat(2048).into_bytes();
        let cur = std::io::Cursor::new(buf);
        let mut block: BufStream<_, 512> = BufStream::new(TestBlockDevice(
            embedded_io_adapters::tokio_1::FromTokio::new(cur),
        ));

        let mut aligned_buffer: Aligned<A4, [u8; 512]> = Aligned([0; 512]);
        block.seek(SeekFrom::Start(0)).await.unwrap();
        block.read_exact(&mut aligned_buffer[..]).await.unwrap();

        // if we read directly, the block buffer will be empty
        assert_eq!(&block.buffer[..], [0u8; 512]);
        // ensure that the current offset is still updated
        assert_eq!(block.current_offset, 512);
        // the write suceeded
        assert_eq!(
            &block.into_inner().0.into_inner().into_inner()[..512],
            &aligned_buffer[..]
        )
    }

    #[tokio::test]
    async fn aligned_read_block_optimization_misaligned() {
        let _ = env_logger::builder().is_test(true).try_init();
        let buf = "A".repeat(2048).into_bytes();
        let cur = std::io::Cursor::new(buf);
        let mut block: BufStream<_, 512> = BufStream::new(TestBlockDevice(
            embedded_io_adapters::tokio_1::FromTokio::new(cur),
        ));

        let mut aligned_buffer: Aligned<A4, [u8; 512]> = Aligned([0; 512]);
        // seek away from aligned block
        block.seek(SeekFrom::Start(3)).await.unwrap();
        // pass an aligned buffer with correct sizing
        block.read_exact(&mut aligned_buffer[..]).await.unwrap();

        // now, we must seek back and read the entire block
        // meaning our block cache will be written to:
        assert_ne!(&block.buffer[..], [0u8; 512]);

        // the read suceeded
        assert_eq!(
            &block.into_inner().0.into_inner().into_inner()[3..512],
            &aligned_buffer[3..]
        )
    }

    #[tokio::test]
    async fn write_seek_read_write() {
        let _ = env_logger::builder().is_test(true).try_init();
        let buf = "A".repeat(2048).into_bytes();
        let cur = std::io::Cursor::new(buf);
        let mut block: BufStream<_, 512> = BufStream::new(TestBlockDevice(
            embedded_io_adapters::tokio_1::FromTokio::new(cur),
        ));

        block.seek(SeekFrom::Start(524)).await.unwrap();
        block
            .write_all(&"B".repeat(512).into_bytes())
            .await
            .unwrap();
        block.flush().await.unwrap();

        block.seek(SeekFrom::Start(0)).await.unwrap();
        let mut tmp = [0u8; 256];
        block.read(&mut tmp[..]).await.unwrap();

        assert_eq!(&tmp[..], "A".repeat(256).into_bytes().as_slice());

        block.seek(SeekFrom::Start(524 + 512)).await.unwrap();
        block
            .write_all(&"C".repeat(512).into_bytes())
            .await
            .unwrap();
        block.flush().await.unwrap();

        let buf = block.into_inner().0.into_inner().into_inner();

        assert_eq!(
            buf,
            ("A".repeat(524) + &"B".repeat(512) + &"C".repeat(512) + &"A".repeat(500)).into_bytes()
        )
    }

    /// A block device that panics if ever handed a 0-block transfer, used to
    /// prove the `BufStream` empty-buffer guard never forwards one.
    struct AssertNonEmptyBlockDevice;

    impl BlockDevice<512> for AssertNonEmptyBlockDevice {
        type Error = core::convert::Infallible;
        type Align = A4;

        async fn read(
            &mut self,
            _block_address: u32,
            data: &mut [Aligned<A4, [u8; 512]>],
        ) -> Result<(), Self::Error> {
            assert!(!data.is_empty(), "0-block read forwarded to device");
            Ok(())
        }

        async fn write(
            &mut self,
            _block_address: u32,
            data: &[Aligned<A4, [u8; 512]>],
        ) -> Result<(), Self::Error> {
            assert!(!data.is_empty(), "0-block write forwarded to device");
            Ok(())
        }

        async fn size(&mut self) -> Result<u64, Self::Error> {
            Ok(u64::MAX)
        }
    }

    #[tokio::test]
    async fn empty_transfers_are_noops() {
        let mut block: BufStream<_, 512> = BufStream::new(AssertNonEmptyBlockDevice);
        block.seek(SeekFrom::Start(0)).await.unwrap();

        // Empty read/write must return Ok(0) without touching the device.
        assert_eq!(block.write(&[]).await.unwrap(), 0);
        assert_eq!(block.read(&mut []).await.unwrap(), 0);

        // A real aligned transfer still reaches the device (sanity check that
        // the assert above is actually armed).
        let aligned: Aligned<A4, [u8; 512]> = Aligned([0xAB; 512]);
        block.write_all(&aligned[..]).await.unwrap();
    }
}
