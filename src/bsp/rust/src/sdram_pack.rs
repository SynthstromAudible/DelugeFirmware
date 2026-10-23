//! The SDRAM content's load copy, compressed.
//!
//! `.sdram_init` is linked at its SDRAM address only; the image carries it deflated in `.sdram_init_pack`
//! (`linker/sdram_sections.x`), written after the link by `scripts/tasks/sdram_pack.py`. The boot inflates it into
//! SDRAM ([`crate::boot_mem`]). The pack is `[u32 LE raw length][u32 LE zlib length][zlib stream][zero padding]`.

/// Why a pack did not inflate into its destination.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UnpackError {
    /// The header is short, or names a zlib stream that runs past the pack.
    Header,
    /// The header's raw length is not the destination's, or the stream inflated to another length.
    Length,
    /// The stream is not valid zlib, or its Adler-32 does not match what it inflated to.
    Inflate,
}

const HEADER_LEN: usize = 8;

/// Inflates `pack` into `out`, which must be exactly the raw length the pack's header names.
pub fn unpack(pack: &[u8], out: &mut [u8]) -> Result<(), UnpackError> {
    let header = pack.get(..HEADER_LEN).ok_or(UnpackError::Header)?;
    let raw_len = u32::from_le_bytes([header[0], header[1], header[2], header[3]]) as usize;
    let stream_len = u32::from_le_bytes([header[4], header[5], header[6], header[7]]) as usize;
    let stream = HEADER_LEN
        .checked_add(stream_len)
        .and_then(|end| pack.get(HEADER_LEN..end))
        .ok_or(UnpackError::Header)?;
    if raw_len != out.len() {
        return Err(UnpackError::Length);
    }
    match miniz_oxide::inflate::decompress_slice_iter_to_slice(
        out,
        core::iter::once(stream),
        true,
        false,
    ) {
        Ok(n) if n == raw_len => Ok(()),
        Ok(_) => Err(UnpackError::Length),
        Err(_) => Err(UnpackError::Inflate),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const RAW: &[u8] = include_bytes!("../tests/fixtures/sdram_pack/raw.bin");
    const PACK: &[u8] = include_bytes!("../tests/fixtures/sdram_pack/pack.bin");

    #[test]
    fn unpacks_the_python_packers_fixture_to_its_raw_bytes() {
        let mut out = [0u8; RAW.len()];
        assert_eq!(unpack(PACK, &mut out), Ok(()));
        assert_eq!(&out[..], RAW);
    }

    #[test]
    fn refuses_a_raw_length_that_is_not_the_destinations() {
        let mut out = [0u8; RAW.len() - 1];
        assert_eq!(unpack(PACK, &mut out), Err(UnpackError::Length));
    }

    #[test]
    fn refuses_a_corrupted_stream() {
        let mut pack = PACK.to_vec();
        pack[100] ^= 0x40;
        let mut out = [0u8; RAW.len()];
        assert_eq!(unpack(&pack, &mut out), Err(UnpackError::Inflate));
    }

    #[test]
    fn refuses_a_zlib_length_past_the_pack() {
        let mut pack = PACK.to_vec();
        pack[4..8].copy_from_slice(&(PACK.len() as u32).to_le_bytes());
        let mut out = [0u8; RAW.len()];
        assert_eq!(unpack(&pack, &mut out), Err(UnpackError::Header));
    }
}
