//! [`ReadAhead`]: a block device that serves a run of single-block reads from one multi-block read.
//!
//! The filesystem reads its metadata a block at a time: a directory scan walks a directory's
//! clusters block by block, and a cluster-chain walk the FAT's. Each of those is a card command,
//! and the card's cost per command dwarfs its cost per block, so a sequential run of single-block
//! reads is served from one read of the blocks ahead. A single-block read that does not follow the
//! previous one reads just that block, so a scattered lookup pays nothing extra.
//!
//! Sits between the SD block device and `BufStream`, under the filesystem lock, so every write the
//! filesystem makes passes through it and the cached blocks stay current. Multi-block reads (a
//! chunk of sample data) pass straight through.
extern crate alloc;

use aligned::Aligned;
use alloc::vec::Vec;
use block_device_driver::BlockDevice;

/// Blocks read ahead once single-block reads are found to run sequentially.
pub const READ_AHEAD_BLOCKS: usize = 16;

/// A [`BlockDevice`] over `D` that reads ahead [`READ_AHEAD_BLOCKS`] when single-block reads run
/// sequentially.
pub struct ReadAhead<D: BlockDevice<512>> {
    inner: D,
    /// The blocks read ahead; empty if they could not be allocated, which turns read-ahead off.
    cache: Vec<Aligned<D::Align, [u8; 512]>>,
    /// The first block `cache` holds, and how many of its blocks are valid.
    start: u32,
    valid: u32,
    /// The block the last single-block read asked for.
    last: Option<u32>,
}

impl<D: BlockDevice<512>> ReadAhead<D> {
    /// Wraps `inner`, allocating the read-ahead blocks from the global allocator.
    pub fn new(inner: D) -> Self {
        let mut cache = Vec::new();
        if cache.try_reserve_exact(READ_AHEAD_BLOCKS).is_ok() {
            cache.resize_with(READ_AHEAD_BLOCKS, || Aligned([0u8; 512]));
        }
        Self {
            inner,
            cache,
            start: 0,
            valid: 0,
            last: None,
        }
    }

    /// The cached copy of `block`, if read ahead.
    fn cached(&self, block: u32) -> Option<&Aligned<D::Align, [u8; 512]>> {
        let offset = block.checked_sub(self.start)?;
        (offset < self.valid).then(|| &self.cache[offset as usize])
    }
}

impl<D: BlockDevice<512>> BlockDevice<512> for ReadAhead<D> {
    type Error = D::Error;
    type Align = D::Align;

    async fn read(
        &mut self,
        block_address: u32,
        data: &mut [Aligned<Self::Align, [u8; 512]>],
    ) -> Result<(), Self::Error> {
        if data.len() != 1 {
            return self.inner.read(block_address, data).await;
        }
        let sequential = self.last == Some(block_address.wrapping_sub(1));
        self.last = Some(block_address);
        if let Some(block) = self.cached(block_address) {
            data[0].copy_from_slice(&block[..]);
            return Ok(());
        }
        if !sequential || self.cache.is_empty() {
            return self.inner.read(block_address, data).await;
        }
        // Not past the device's end: a short device reads ahead only what it has.
        let blocks = (self.inner.size().await? / 512).saturating_sub(u64::from(block_address));
        let ahead = (READ_AHEAD_BLOCKS as u64).min(blocks) as usize;
        if ahead <= 1 {
            return self.inner.read(block_address, data).await;
        }
        self.valid = 0;
        self.inner
            .read(block_address, &mut self.cache[..ahead])
            .await?;
        self.start = block_address;
        self.valid = ahead as u32;
        data[0].copy_from_slice(&self.cache[0][..]);
        Ok(())
    }

    async fn write(
        &mut self,
        block_address: u32,
        data: &[Aligned<Self::Align, [u8; 512]>],
    ) -> Result<(), Self::Error> {
        let result = self.inner.write(block_address, data).await;
        // Keep any cached copy of the blocks written current; a failed write leaves the card's
        // content unknown, so the cache is dropped.
        if result.is_err() {
            self.valid = 0;
            return result;
        }
        for (i, block) in data.iter().enumerate() {
            let Some(at) = block_address.checked_add(i as u32) else {
                break;
            };
            if let Some(offset) = at.checked_sub(self.start).filter(|&o| o < self.valid) {
                self.cache[offset as usize].copy_from_slice(&block[..]);
            }
        }
        result
    }

    async fn size(&mut self) -> Result<u64, Self::Error> {
        self.inner.size().await
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use aligned::A4;
    use embassy_futures::block_on;
    use std::vec;

    /// A RAM disk that records each read as (first block, blocks).
    struct Recorder {
        blocks: Vec<[u8; 512]>,
        reads: Vec<(u32, usize)>,
    }

    impl Recorder {
        fn new(n: usize) -> Self {
            Self {
                blocks: (0..n).map(|i| [i as u8; 512]).collect(),
                reads: Vec::new(),
            }
        }
    }

    impl BlockDevice<512> for Recorder {
        type Error = ();
        type Align = A4;

        async fn read(&mut self, at: u32, data: &mut [Aligned<A4, [u8; 512]>]) -> Result<(), ()> {
            self.reads.push((at, data.len()));
            for (i, d) in data.iter_mut().enumerate() {
                d.copy_from_slice(self.blocks.get(at as usize + i).ok_or(())?);
            }
            Ok(())
        }

        async fn write(&mut self, at: u32, data: &[Aligned<A4, [u8; 512]>]) -> Result<(), ()> {
            for (i, d) in data.iter().enumerate() {
                self.blocks[at as usize + i].copy_from_slice(&d[..]);
            }
            Ok(())
        }

        async fn size(&mut self) -> Result<u64, ()> {
            Ok(self.blocks.len() as u64 * 512)
        }
    }

    fn read_one(dev: &mut ReadAhead<Recorder>, at: u32) -> u8 {
        let mut b = [Aligned([0u8; 512])];
        block_on(dev.read(at, &mut b)).unwrap();
        b[0][0]
    }

    #[test]
    fn a_sequential_run_of_single_reads_is_one_read_ahead() {
        let mut dev = ReadAhead::new(Recorder::new(100));
        for at in 10..30u32 {
            assert_eq!(read_one(&mut dev, at), at as u8);
        }
        // 10 alone, then 11 starts the run: 11..27 in one read, then 27..43.
        assert_eq!(dev.inner.reads, vec![(10, 1), (11, 16), (27, 16)]);
    }

    #[test]
    fn scattered_single_reads_read_only_their_block() {
        let mut dev = ReadAhead::new(Recorder::new(100));
        for at in [5u32, 40, 7, 90] {
            assert_eq!(read_one(&mut dev, at), at as u8);
        }
        assert_eq!(dev.inner.reads, vec![(5, 1), (40, 1), (7, 1), (90, 1)]);
    }

    #[test]
    fn read_ahead_stops_at_the_device_end() {
        let mut dev = ReadAhead::new(Recorder::new(20));
        for at in 14..20u32 {
            assert_eq!(read_one(&mut dev, at), at as u8);
        }
        assert_eq!(dev.inner.reads, vec![(14, 1), (15, 5)]);
    }

    #[test]
    fn a_write_updates_a_block_read_ahead() {
        let mut dev = ReadAhead::new(Recorder::new(100));
        read_one(&mut dev, 10);
        read_one(&mut dev, 11);
        block_on(dev.write(13, &[Aligned([0xEE; 512])])).unwrap();
        read_one(&mut dev, 12);
        assert_eq!(read_one(&mut dev, 13), 0xEE);
        assert_eq!(dev.inner.reads, vec![(10, 1), (11, 16)]);
    }

    #[test]
    fn multi_block_reads_pass_through() {
        let mut dev = ReadAhead::new(Recorder::new(100));
        let mut b = vec![Aligned([0u8; 512]); 4];
        block_on(dev.read(50, &mut b)).unwrap();
        assert_eq!(b[3][0], 53);
        assert_eq!(dev.inner.reads, vec![(50, 4)]);
    }
}
