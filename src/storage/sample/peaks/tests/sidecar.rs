//! A sidecar written from an index and read back into a fresh one gives the same level 1; other sections come back
//! byte for byte; every damaged field is refused.
use deluge_storage_sample_peaks::sidecar::{self, HEADER_LEN, Key, Reject, Section, TAG_PEAK, tag};
use deluge_storage_sample_peaks::{Peak, PeakIndex, crc, mem};
use proptest::prelude::*;

const TAG_TEST: u32 = tag(*b"TEST");

fn key(frames: u64) -> Key {
    Key {
        first_cluster: 0x9A12_C4E0,
        file_size: 1_234_567,
        fat_datetime: 0x5A21_6C33,
        sample_rate: 44_100,
        frames,
        channels: 2,
        byte_depth: 3,
        reserved: 0,
    }
}

/// Folds `blocks` (block number, peak) into a fresh final index of `frames` frames.
fn filled(frames: u64, blocks: &[(u64, Peak)]) -> core::ptr::NonNull<PeakIndex> {
    mem::host::install();
    let index = PeakIndex::create(frames, false, frames).expect("index");
    // SAFETY: released by the caller.
    let i = unsafe { index.as_ref() };
    for &(b, p) in blocks {
        i.fold_block(b, p);
    }
    index
}

fn done(index: core::ptr::NonNull<PeakIndex>) {
    PeakIndex::release(index);
    PeakIndex::collect();
}

fn written(i: &PeakIndex, k: &Key, extras: &[Section<'_>]) -> Vec<u8> {
    let mut buf = vec![0u8; sidecar::len(i, extras).expect("len")];
    assert_eq!(sidecar::write(i, k, extras, &mut buf), Some(buf.len()));
    buf
}

#[test]
fn crc32_is_zlibs() {
    assert_eq!(crc::crc32(b"123456789"), 0xCBF4_3926);
    assert_eq!(crc::crc32(b""), 0);
}

proptest! {
    #[test]
    fn a_written_sidecar_restores_the_same_level_1_and_its_other_sections(
        frames in 1u64..3_000_000,
        picks in proptest::collection::vec((0u64..12_000, -32768i32..32767, 0i32..32768), 0..400),
        extra in proptest::collection::vec(any::<u8>(), 0..100),
    ) {
        let blocks: Vec<(u64, Peak)> = picks.iter()
            .map(|&(b, lo, span)| (b % frames.div_ceil(256), Peak { min: lo as i16, max: (lo + span).min(32767) as i16 }))
            .collect();
        let a = filled(frames, &blocks);
        // SAFETY: released below.
        let ai = unsafe { a.as_ref() };
        let k = key(frames);
        let buf = written(ai, &k, &[Section { tag: TAG_TEST, bytes: &extra }]);
        let opened = sidecar::open(&buf, &k).expect("open");
        prop_assert_eq!(opened.section(TAG_TEST), Some(&extra[..]));
        prop_assert!(opened.section(TAG_PEAK).is_some());
        prop_assert_eq!(opened.section(tag(*b"NONE")), None);
        let b = filled(frames, &[]);
        // SAFETY: released below.
        let bi = unsafe { b.as_ref() };
        prop_assert_eq!(sidecar::restore_peaks(bi, &opened), Ok(ai.complete_level1()));
        prop_assert_eq!(bi.complete_level1(), ai.complete_level1());
        for e in 0..ai.level1_entries() {
            prop_assert_eq!(bi.level1_word_for_test(e), ai.level1_word_for_test(e), "entry {}", e);
        }
        prop_assert_eq!(bi.next_unscanned(0).is_none(), ai.next_unscanned(0).is_none());
        done(a);
        done(b);
    }

    #[test]
    fn a_restore_into_an_index_already_filling_adds_only_what_is_missing(
        whole_entries in proptest::collection::vec(0u64..40, 0..40),
        loose in proptest::collection::vec(0u64..640, 0..200),
        split in 0usize..840,
    ) {
        // Whole level-1 entries (all 16 blocks) and loose blocks, so some entries are complete on both sides of the
        // split and some only after it.
        let frames = 640 * 256;
        let peak = |b: u64| Peak { min: -((b % 300) as i16), max: (b % 500) as i16 };
        let all: Vec<(u64, Peak)> = whole_entries.iter().flat_map(|&e| (e * 16..e * 16 + 16))
            .chain(loose.iter().copied())
            .map(|b| (b, peak(b)))
            .collect();
        let whole = filled(frames, &all);
        // SAFETY: released below.
        let wi = unsafe { whole.as_ref() };
        let buf = written(wi, &key(frames), &[]);
        let part = filled(frames, &all[..split.min(all.len())]);
        // SAFETY: released below.
        let pi = unsafe { part.as_ref() };
        sidecar::restore_peaks(pi, &sidecar::open(&buf, &key(frames)).expect("open")).expect("restore");
        for &(b, p) in &all[split.min(all.len())..] {
            pi.fold_block(b, p); // the scan arriving after the restore
        }
        prop_assert_eq!(pi.complete_level1(), wi.complete_level1());
        for e in 0..wi.level1_entries() {
            prop_assert_eq!(pi.level1_word_for_test(e), wi.level1_word_for_test(e));
        }
        done(whole);
        done(part);
    }
}

/// A sidecar of 100,000 frames with its first 20 blocks folded and a 6-byte TEST section.
fn sample_bytes() -> (Vec<u8>, u64) {
    let frames = 100_000;
    let blocks: Vec<(u64, Peak)> = (0..20).map(|b| (b, Peak { min: -7, max: 9 })).collect();
    let a = filled(frames, &blocks);
    // SAFETY: released below.
    let buf = written(
        unsafe { a.as_ref() },
        &key(frames),
        &[Section {
            tag: TAG_TEST,
            bytes: b"abcdef",
        }],
    );
    done(a);
    (buf, frames)
}

fn restore_into_fresh(bytes: &[u8], k: &Key, frames: u64) -> Result<u64, Reject> {
    let b = filled(frames, &[]);
    // SAFETY: released below.
    let r = sidecar::open(bytes, k).and_then(|s| sidecar::restore_peaks(unsafe { b.as_ref() }, &s));
    done(b);
    r
}

/// Offset of the first section's bytes (the table's first entry, offset field).
fn first_section_at(bytes: &[u8]) -> usize {
    u32::from_le_bytes(bytes[HEADER_LEN + 4..HEADER_LEN + 8].try_into().unwrap()) as usize
}

#[test]
fn each_damaged_field_is_refused() {
    let (good, frames) = sample_bytes();
    let k = key(frames);
    assert_eq!(restore_into_fresh(&good, &k, frames), Ok(1)); // 16 blocks make level-1 entry 0 complete
    let flip = |at: usize| {
        let mut b = good.clone();
        b[at] ^= 0x40;
        b
    };
    assert_eq!(restore_into_fresh(&flip(0), &k, frames), Err(Reject::Magic));
    assert_eq!(
        restore_into_fresh(&flip(4), &k, frames),
        Err(Reject::Version)
    );
    assert_eq!(
        restore_into_fresh(&flip(9), &k, frames),
        Err(Reject::HeaderCrc)
    );
    assert_eq!(
        restore_into_fresh(&flip(HEADER_LEN + 1), &k, frames),
        Err(Reject::HeaderCrc)
    ); // the table is covered
    assert_eq!(
        restore_into_fresh(&flip(first_section_at(&good)), &k, frames),
        Err(Reject::SectionCrc)
    );
    assert_eq!(
        restore_into_fresh(&good[..good.len() - 1], &k, frames),
        Err(Reject::Table)
    );
    assert_eq!(
        restore_into_fresh(&good[..10], &k, frames),
        Err(Reject::Short)
    );
    for other in [
        Key {
            first_cluster: 1,
            ..k
        },
        Key { file_size: 1, ..k },
        Key {
            fat_datetime: 1,
            ..k
        },
        Key {
            sample_rate: 48_000,
            ..k
        },
        Key { channels: 1, ..k },
        Key { byte_depth: 2, ..k },
    ] {
        assert_eq!(restore_into_fresh(&good, &other, frames), Err(Reject::Key));
    }
    assert_eq!(
        restore_into_fresh(
            &good,
            &Key {
                frames: frames + 1,
                ..k
            },
            frames + 1
        ),
        Err(Reject::Key)
    );
}

#[test]
fn an_index_of_another_length_refuses_the_peaks() {
    let (good, frames) = sample_bytes();
    let s = sidecar::open(&good, &key(frames)).expect("open");
    let b = filled(frames * 2, &[]);
    // SAFETY: released below.
    assert_eq!(
        sidecar::restore_peaks(unsafe { b.as_ref() }, &s),
        Err(Reject::Key)
    );
    done(b);
}

#[test]
fn a_partial_sidecar_keeps_its_incomplete_entries_incomplete() {
    let (good, frames) = sample_bytes();
    let b = filled(frames, &[]);
    // SAFETY: released below.
    let bi = unsafe { b.as_ref() };
    sidecar::restore_peaks(bi, &sidecar::open(&good, &key(frames)).expect("open"))
        .expect("restore");
    assert_eq!(bi.next_unscanned(0), Some(16 * 256)); // entry 1 was incomplete, so none of its blocks is restored
    done(b);
}

proptest! {
    #[test]
    fn the_level_1_count_matches_the_complete_entries_through_a_take_turning_final(
        frames in 1u64..200_000, picks in proptest::collection::vec(0u64..800, 0..800),
    ) {
        mem::host::install();
        let p = PeakIndex::create(0, true, frames).expect("index");
        // SAFETY: released below.
        let i = unsafe { p.as_ref() };
        i.reserve(frames);
        i.set_frames(frames, false);
        let blocks = frames.div_ceil(256);
        for &b in &picks {
            i.fold_block(b % blocks, Peak { min: -1, max: 1 });
        }
        i.set_frames(frames, true);
        let brute = (0..i.level1_entries()).filter(|&e| i.level1_word_for_test(e) != 0).count() as u64;
        prop_assert_eq!(i.complete_level1(), brute);
        done(p);
    }
}
