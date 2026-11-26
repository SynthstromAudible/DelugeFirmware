//! A sample's sidecar file: a 64-byte header naming the audio file it belongs to, a table of sections, then the
//! sections. `PEAK` holds the peak index's level 1 (levels 2 and up are rebuilt by folding, as a scan builds them);
//! every other section is the caller's, carried byte for byte. A reader skips sections it does not know.

use crate::crc::{crc32, crc32_continue};
use crate::index::PeakIndex;

/// The file's first four bytes.
pub const MAGIC: [u8; 4] = *b"DLPK";
/// The format's version.
pub const VERSION: u16 = 1;
/// The header's length in bytes.
pub const HEADER_LEN: usize = 64;
/// The most sections a sidecar holds.
pub const MAX_SECTIONS: usize = 8;
const ENTRY_LEN: usize = 16;

/// A section's tag: four ASCII bytes as a little-endian `u32`.
#[must_use]
pub const fn tag(name: [u8; 4]) -> u32 {
    u32::from_le_bytes(name)
}

/// The peak index's level 1.
pub const TAG_PEAK: u32 = tag(*b"PEAK");

/// The audio file a sidecar belongs to, and the shape of its samples.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Key {
    /// The file's first cluster on its card.
    pub first_cluster: u32,
    /// The file's size in bytes.
    pub file_size: u32,
    /// The file's FAT modify date and time, `date << 16 | time`.
    pub fat_datetime: u32,
    /// Frames per second.
    pub sample_rate: u32,
    /// Frames in the file.
    pub frames: u64,
    /// Channels per frame.
    pub channels: u8,
    /// Bytes per sample.
    pub byte_depth: u8,
    /// Always 0.
    pub reserved: u16,
}

/// A section to write besides `PEAK`, opaque to this crate.
#[derive(Clone, Copy, Debug)]
pub struct Section<'a> {
    /// Its tag; not [`TAG_PEAK`].
    pub tag: u32,
    /// Its bytes.
    pub bytes: &'a [u8],
}

/// Why a sidecar was refused.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(i32)]
pub enum Reject {
    /// Shorter than its header and table.
    Short = 1,
    /// Not a sidecar.
    Magic = 2,
    /// Another format version or header length.
    Version = 3,
    /// The header's or the table's checksum does not match.
    HeaderCrc = 4,
    /// It belongs to another file, or the index is not final at the file's frame count.
    Key = 5,
    /// The table names more than [`MAX_SECTIONS`] sections, or one outside the file or overlapping another.
    Table = 6,
    /// A section's checksum does not match.
    SectionCrc = 7,
    /// The `PEAK` section is missing or its entry count does not fit the index.
    Length = 8,
}

/// A sidecar whose header, table and sections all checked out.
pub struct Sidecar<'a> {
    bytes: &'a [u8],
    count: usize,
}

fn put(buf: &mut [u8], at: usize, bytes: &[u8]) {
    if let Some(d) = buf.get_mut(at..at + bytes.len()) {
        d.copy_from_slice(bytes);
    }
}

fn u16_at(buf: &[u8], at: usize) -> u16 {
    buf.get(at..at + 2)
        .and_then(|b| b.try_into().ok())
        .map_or(0, u16::from_le_bytes)
}

fn u32_at(buf: &[u8], at: usize) -> u32 {
    buf.get(at..at + 4)
        .and_then(|b| b.try_into().ok())
        .map_or(0, u32::from_le_bytes)
}

fn u64_at(buf: &[u8], at: usize) -> u64 {
    buf.get(at..at + 8)
        .and_then(|b| b.try_into().ok())
        .map_or(0, u64::from_le_bytes)
}

/// The `PEAK` section's length for `index`.
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
fn peak_len(index: &PeakIndex) -> Option<usize> {
    usize::try_from(index.level1_entries())
        .ok()?
        .checked_mul(4)?
        .checked_add(8)
}

/// The sidecar's length for `index` and `extras`, or `None` if it would not fit.
#[must_use]
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub fn len(index: &PeakIndex, extras: &[Section<'_>]) -> Option<usize> {
    if extras.len() + 1 > MAX_SECTIONS {
        return None;
    }
    let mut total = HEADER_LEN
        .checked_add((extras.len() + 1) * ENTRY_LEN)?
        .checked_add(peak_len(index)?)?;
    for s in extras {
        total = total.checked_add(s.bytes.len())?;
    }
    u32::try_from(total).ok()?;
    Some(total)
}

/// Writes the sidecar for `index`, `key` and `extras` into `out`; its length, or `None` if `out` is too short.
/// Thread mode.
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub fn write(
    index: &PeakIndex,
    key: &Key,
    extras: &[Section<'_>],
    out: &mut [u8],
) -> Option<usize> {
    let total = len(index, extras)?;
    let count = extras.len() + 1;
    let out = out.get_mut(..total)?;
    out.fill(0);
    let table_end = HEADER_LEN + count * ENTRY_LEN;
    // PEAK first.
    let n = u32::try_from(index.level1_entries()).ok()?;
    let peak_at = table_end;
    let peak_end = peak_at + peak_len(index)?;
    let mut complete = 0u32;
    {
        let peak = out.get_mut(peak_at..peak_end)?;
        let (head, words) = peak.split_at_mut(8);
        for (entry, word) in (0u64..).zip(words.as_chunks_mut::<4>().0) {
            let w = index.level1_word(entry);
            complete += u32::from(w != 0);
            word.copy_from_slice(&w.to_le_bytes());
        }
        put(head, 0, &n.to_le_bytes());
        put(head, 4, &complete.to_le_bytes());
    }
    let mut sections = [(0u32, 0usize, 0usize); MAX_SECTIONS];
    *sections.get_mut(0)? = (TAG_PEAK, peak_at, peak_end);
    let mut at = peak_end;
    for (slot, s) in sections.iter_mut().skip(1).zip(extras) {
        let end = at + s.bytes.len();
        out.get_mut(at..end)?.copy_from_slice(s.bytes);
        *slot = (s.tag, at, end);
        at = end;
    }
    for (i, &(t, from, to)) in sections.iter().take(count).enumerate() {
        let e = HEADER_LEN + i * ENTRY_LEN;
        let crc = crc32(out.get(from..to)?);
        put(out, e, &t.to_le_bytes());
        put(out, e + 4, &u32::try_from(from).ok()?.to_le_bytes());
        put(out, e + 8, &u32::try_from(to - from).ok()?.to_le_bytes());
        put(out, e + 12, &crc.to_le_bytes());
    }
    put(out, 0, &MAGIC);
    put(out, 4, &VERSION.to_le_bytes());
    put(out, 6, &(HEADER_LEN as u16).to_le_bytes());
    put(out, 8, &key.first_cluster.to_le_bytes());
    put(out, 12, &key.file_size.to_le_bytes());
    put(out, 16, &key.fat_datetime.to_le_bytes());
    put(out, 20, &key.frames.to_le_bytes());
    put(out, 28, &key.sample_rate.to_le_bytes());
    put(out, 32, &[key.channels, key.byte_depth]);
    put(out, 34, &(count as u16).to_le_bytes());
    let crc = crc32_continue(crc32(out.get(..60)?), out.get(HEADER_LEN..table_end)?);
    put(out, 60, &crc.to_le_bytes());
    Some(total)
}

/// Checks `bytes` as a sidecar for `key`: header, table and every section's checksum. Thread mode.
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub fn open<'a>(bytes: &'a [u8], key: &Key) -> Result<Sidecar<'a>, Reject> {
    let head = bytes.get(..HEADER_LEN).ok_or(Reject::Short)?;
    if head.get(..4) != Some(&MAGIC[..]) {
        return Err(Reject::Magic);
    }
    if u16_at(head, 4) != VERSION || usize::from(u16_at(head, 6)) != HEADER_LEN {
        return Err(Reject::Version);
    }
    let count = usize::from(u16_at(head, 34));
    if count == 0 || count > MAX_SECTIONS {
        return Err(Reject::Table);
    }
    let table_end = HEADER_LEN + count * ENTRY_LEN;
    let table = bytes.get(HEADER_LEN..table_end).ok_or(Reject::Short)?;
    let crc = crc32_continue(crc32(head.get(..60).ok_or(Reject::Short)?), table);
    if u32_at(head, 60) != crc {
        return Err(Reject::HeaderCrc);
    }
    let stored = Key {
        first_cluster: u32_at(head, 8),
        file_size: u32_at(head, 12),
        fat_datetime: u32_at(head, 16),
        sample_rate: u32_at(head, 28),
        frames: u64_at(head, 20),
        channels: head.get(32).copied().unwrap_or(0),
        byte_depth: head.get(33).copied().unwrap_or(0),
        reserved: 0,
    };
    if stored
        != (Key {
            reserved: 0,
            ..*key
        })
    {
        return Err(Reject::Key);
    }
    let mut prev_end = table_end;
    for e in table.as_chunks::<ENTRY_LEN>().0 {
        let from = usize::try_from(u32_at(e, 4)).map_err(|_| Reject::Table)?;
        let len = usize::try_from(u32_at(e, 8)).map_err(|_| Reject::Table)?;
        let to = from.checked_add(len).ok_or(Reject::Table)?;
        if from < prev_end || to > bytes.len() {
            return Err(Reject::Table);
        }
        if crc32(bytes.get(from..to).ok_or(Reject::Table)?) != u32_at(e, 12) {
            return Err(Reject::SectionCrc);
        }
        prev_end = to;
    }
    if prev_end != bytes.len() {
        return Err(Reject::Table);
    }
    Ok(Sidecar { bytes, count })
}

impl<'a> Sidecar<'a> {
    /// The bytes of the section tagged `tag`, if the sidecar has one.
    #[must_use]
    pub fn section(&self, tag: u32) -> Option<&'a [u8]> {
        let table = self
            .bytes
            .get(HEADER_LEN..HEADER_LEN + self.count * ENTRY_LEN)?;
        let e = table
            .as_chunks::<ENTRY_LEN>()
            .0
            .iter()
            .find(|e| u32_at(&e[..], 0) == tag)?;
        let from = usize::try_from(u32_at(e, 4)).ok()?;
        let to = from.checked_add(usize::try_from(u32_at(e, 8)).ok()?)?;
        self.bytes.get(from..to)
    }

    /// The frame count its header names.
    #[must_use]
    pub fn frames(&self) -> u64 {
        u64_at(self.bytes, 20)
    }
}

/// Restores `sidecar`'s `PEAK` section into `index`, a final index of the sidecar's frames; how many level-1 entries
/// it held complete. Entries the index already has complete are left as they are. Thread mode.
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
pub fn restore_peaks(index: &PeakIndex, sidecar: &Sidecar<'_>) -> Result<u64, Reject> {
    if !index.is_final_at(sidecar.frames()) {
        return Err(Reject::Key);
    }
    let peak = sidecar.section(TAG_PEAK).ok_or(Reject::Length)?;
    let n = index.level1_entries();
    let words = peak.get(8..).ok_or(Reject::Length)?;
    if u64::from(u32_at(peak, 0)) != n || u64::try_from(words.len()).ok() != n.checked_mul(4) {
        return Err(Reject::Length);
    }
    let mut restored = 0;
    for (entry, w) in (0u64..).zip(words.as_chunks::<4>().0) {
        let e = u32::from_le_bytes(*w);
        if e != 0 {
            index.restore_level1(entry, e);
            restored += 1;
        }
    }
    Ok(restored)
}
