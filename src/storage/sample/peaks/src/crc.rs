//! CRC-32 (IEEE, as zlib), four bits at a time: a 16-entry table keeps it out of the way of the image.

const TABLE: [u32; 16] = {
    let mut t = [0u32; 16];
    let mut i = 0;
    while i < 16 {
        let mut c = i as u32;
        let mut k = 0;
        while k < 4 {
            c = if c & 1 != 0 {
                0xEDB8_8320 ^ (c >> 1)
            } else {
                c >> 1
            };
            k += 1;
        }
        t[i] = c;
        i += 1;
    }
    t
};

/// The CRC-32 of `bytes`, continued from `crc` (0 to start).
#[must_use]
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub fn crc32_continue(crc: u32, bytes: &[u8]) -> u32 {
    let mut c = !crc;
    for &b in bytes {
        c ^= u32::from(b);
        c = TABLE[(c & 15) as usize] ^ (c >> 4);
        c = TABLE[(c & 15) as usize] ^ (c >> 4);
    }
    !c
}

/// The CRC-32 of `bytes`.
#[must_use]
pub fn crc32(bytes: &[u8]) -> u32 {
    crc32_continue(0, bytes)
}
