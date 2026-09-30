//! The meter counts a synchronous card call only after boot and only on the interaction tier.
#![cfg(not(target_os = "none"))]

use deluge_bsp_rust::boot;
use deluge_bsp_rust::tier;
use deluge_bsp_rust::tier_meter;

use tier_meter::Entry;

/// Every counter here is process-global, so the cases run in order in one test.
#[test]
fn note_counts_only_post_boot_interaction() {
    tier_meter::note(Entry::FileOpen);
    assert_eq!(
        tier_meter::count(Entry::FileOpen),
        0,
        "boot's synchronous calls are legal and never counted"
    );

    boot::mark_finished();
    {
        let _s = tier::enter(tier::Tier::Storage);
        tier_meter::note(Entry::FileOpen);
    }
    {
        let _a = tier::enter(tier::Tier::Audio);
        tier_meter::note(Entry::FileOpen);
    }
    assert_eq!(
        tier_meter::count(Entry::FileOpen),
        0,
        "only the interaction tier is counted"
    );

    tier_meter::note(Entry::FileOpen);
    tier_meter::note(Entry::BlockRead);
    assert_eq!(tier_meter::count(Entry::FileOpen), 1);
    assert_eq!(tier_meter::count(Entry::BlockRead), 1);
    assert_eq!(tier_meter::total(), 2);
    assert_eq!(Entry::FileOpen.name(), "file_open");
    assert_eq!(Entry::BlockRead.name(), "block_read");

    // The C ABI agrees with the Rust accessors.
    let n = tier_meter::deluge_tier_sync_entries();
    assert_eq!(n as usize, 29);
    let idx = (0..n)
        .find(|&i| {
            let p = tier_meter::deluge_tier_sync_name(i);
            unsafe { core::ffi::CStr::from_ptr(p) }.to_str() == Ok("file_open")
        })
        .expect("file_open is an entry");
    assert_eq!(tier_meter::deluge_tier_sync_count(idx), 1);
    assert_eq!(
        unsafe { core::ffi::CStr::from_ptr(tier_meter::deluge_tier_sync_name(n)) }.to_bytes(),
        b""
    );
}
