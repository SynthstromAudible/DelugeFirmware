//! An abstraction of block devices.

#![cfg_attr(not(test), no_std)]
#![warn(missing_docs)]
#![allow(async_fn_in_trait)]

use aligned::Aligned;

/// A trait for a block devices
///
/// [`BlockDevice<const SIZE: usize>`](BlockDevice) can be initialized with the following parameters.
///
/// - `const SIZE`: The size of the block in the block device.
/// - `type Align`: The [`aligned::Alignment`] of the block buffers for this implementation.
/// - `type Error`: The error type for the implementation.
///
/// The generic parameter `SIZE` on [BlockDevice] is the number of _bytes_ in a block
/// for this block device.
///
/// All addresses are zero indexed, and the unit is blocks. For example to read bytes
/// from 1024 to 1536 on a 512 byte block device, the supplied block address would be 2.
///
/// <div class="warning"><b>NOTE to implementors</b>: Alignment of the buffer <b>must</b> be multiple of SIZE to avoid
/// padding bytes when casting between blocks and slices.</div>
///
/// This trait can be implemented multiple times to support various different block sizes.
pub trait BlockDevice<const SIZE: usize> {
    /// The error type for the BlockDevice implementation.
    type Error: core::fmt::Debug;

    /// The alignment requirements of the block buffers.
    type Align: aligned::Alignment;

    /// Read one or more blocks at the given block address.
    async fn read(
        &mut self,
        block_address: u32,
        data: &mut [Aligned<Self::Align, [u8; SIZE]>],
    ) -> Result<(), Self::Error>;

    /// Write one or more blocks at the given block address.
    async fn write(
        &mut self,
        block_address: u32,
        data: &[Aligned<Self::Align, [u8; SIZE]>],
    ) -> Result<(), Self::Error>;

    /// Report the size of the block device in bytes.
    async fn size(&mut self) -> Result<u64, Self::Error>;
}

impl<T: BlockDevice<SIZE>, const SIZE: usize> BlockDevice<SIZE> for &mut T {
    type Error = T::Error;
    type Align = T::Align;

    async fn read(
        &mut self,
        block_address: u32,
        data: &mut [Aligned<Self::Align, [u8; SIZE]>],
    ) -> Result<(), Self::Error> {
        (*self).read(block_address, data).await
    }

    async fn write(
        &mut self,
        block_address: u32,
        data: &[Aligned<Self::Align, [u8; SIZE]>],
    ) -> Result<(), Self::Error> {
        (*self).write(block_address, data).await
    }

    async fn size(&mut self) -> Result<u64, Self::Error> {
        (*self).size().await
    }
}

/// Cast a byte slice to an aligned slice of blocks.
///
/// This function panics if
///
/// * ALIGNment is not a multiple of SIZE
/// * The input slice is not a multiple of SIZE
/// * The input slice does not have the correct alignment.
pub fn slice_to_blocks<ALIGN, const SIZE: usize>(slice: &[u8]) -> &[Aligned<ALIGN, [u8; SIZE]>]
where
    ALIGN: aligned::Alignment,
{
    let align: usize = core::mem::align_of::<Aligned<ALIGN, ()>>();
    assert!(slice.len() % SIZE == 0);
    assert!(slice.len() % align == 0);
    assert!(slice.as_ptr().cast::<u8>() as usize % align == 0);
    // Note unsafe: we check the buf has the correct SIZE and ALIGNment before casting
    unsafe {
        core::slice::from_raw_parts(
            slice.as_ptr() as *const Aligned<ALIGN, [u8; SIZE]>,
            slice.len() / SIZE,
        )
    }
}

/// Cast a mutable byte slice to an aligned mutable slice of blocks.
///
/// This function panics if
///
/// * ALIGNment is not a multiple of SIZE
/// * The input slice is not a multiple of SIZE
/// * The input slice does not have the correct alignment.
pub fn slice_to_blocks_mut<ALIGN, const SIZE: usize>(
    slice: &mut [u8],
) -> &mut [Aligned<ALIGN, [u8; SIZE]>]
where
    ALIGN: aligned::Alignment,
{
    let align: usize = core::mem::align_of::<Aligned<ALIGN, [u8; SIZE]>>();
    assert!(slice.len() % SIZE == 0);
    assert!(slice.len() % align == 0);
    assert!(slice.as_ptr().cast::<u8>() as usize % align == 0);
    // Note unsafe: we check the buf has the correct SIZE and ALIGNment before casting
    unsafe {
        core::slice::from_raw_parts_mut(
            slice.as_mut_ptr() as *mut Aligned<ALIGN, [u8; SIZE]>,
            slice.len() / SIZE,
        )
    }
}

/// Cast a slice of aligned blocks to a byte slice
///
/// This function panics if
///
/// * ALIGNment is not a multiple of SIZE
pub fn blocks_to_slice<ALIGN, const SIZE: usize>(buf: &[Aligned<ALIGN, [u8; SIZE]>]) -> &[u8]
where
    ALIGN: aligned::Alignment,
{
    // We only need to assert that ALIGN is a multiple of SIZE, the other invariants are checked via the type system.
    // This relationship must be true to avoid padding bytes which will introduce UB when casting.
    let align: usize = core::mem::align_of::<Aligned<ALIGN, ()>>();
    assert!(SIZE % align == 0);
    // Note unsafe: we check the buf has the correct SIZE and ALIGNment before casting
    unsafe { core::slice::from_raw_parts(buf.as_ptr() as *const u8, buf.len() * SIZE) }
}

/// Cast a mutable slice of aligned blocks to a mutable byte slice
///
/// This function panics if
///
/// * ALIGNment is not a multiple of SIZE
pub fn blocks_to_slice_mut<ALIGN, const SIZE: usize>(
    buf: &mut [Aligned<ALIGN, [u8; SIZE]>],
) -> &mut [u8]
where
    ALIGN: aligned::Alignment,
{
    // We only need to assert that ALIGN is a multiple of SIZE, the other invariants are checked via the type system.
    // This relationship must be true to avoid padding bytes which will introduce UB when casting.
    let align: usize = core::mem::align_of::<Aligned<ALIGN, ()>>();
    assert!(SIZE % align == 0);
    // Note unsafe: we check the buf has the correct SIZE and ALIGNment before casting
    unsafe { core::slice::from_raw_parts_mut(buf.as_mut_ptr() as *mut u8, buf.len() * SIZE) }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_conversion_round_trip() {
        let blocks = &mut [
            Aligned::<aligned::A4, _>([0; 512]),
            Aligned::<aligned::A4, _>([0; 512]),
        ];
        let slice = blocks_to_slice_mut(blocks);
        assert!(slice.len() == 1024);
        let blocks: &mut [Aligned<aligned::A4, [u8; 512]>] = slice_to_blocks_mut(slice);
        assert!(blocks.len() == 2);
    }
}
