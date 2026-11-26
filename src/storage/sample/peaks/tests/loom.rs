//! The fold-then-publish protocol under loom: two children fold into a parent's value and then set their mask bits; a
//! reader that sees the full mask must see both values folded in. Run with `RUSTFLAGS="--cfg loom"`.
#![cfg(loom)]

use deluge_storage_sample_peaks::Peak;
use loom::sync::Arc;
use loom::sync::atomic::{AtomicBool, AtomicU32, Ordering, fence};
use loom::thread;

fn fold_into(cell: &AtomicU32, e: u32) {
    let mut cur = cell.load(Ordering::Relaxed);
    loop {
        let next = Peak::encode_fold(cur, e);
        if next == cur {
            return;
        }
        match cell.compare_exchange_weak(cur, next, Ordering::Release, Ordering::Relaxed) {
            Ok(_) => return,
            Err(seen) => cur = seen,
        }
    }
}

#[test]
fn a_full_mask_implies_a_folded_value() {
    loom::model(|| {
        let value = Arc::new(AtomicU32::new(0));
        let mask = Arc::new(AtomicU32::new(0));
        let a = Peak { min: -5, max: 3 }.encode();
        let b = Peak { min: -1, max: 9 }.encode();
        let spawn = |bit: u32, e: u32| {
            let (value, mask) = (value.clone(), mask.clone());
            thread::spawn(move || {
                fold_into(&value, e);
                mask.fetch_or(bit, Ordering::AcqRel);
            })
        };
        let (t1, t2) = (spawn(1, a), spawn(2, b));
        if mask.load(Ordering::Acquire) == 3 {
            assert_eq!(
                Peak::decode(value.load(Ordering::Relaxed)),
                Peak { min: -5, max: 9 }
            );
        }
        t1.join().unwrap();
        t2.join().unwrap();
        assert_eq!(value.load(Ordering::Relaxed), Peak::encode_fold(a, b));
    });
}

/// A level-0 chunk retired by thread mode (its slot swapped to null) is freed only when the fold counter reads 0, so
/// a fold that loaded it before the swap never writes into freed memory.
#[test]
fn a_retired_chunk_is_freed_only_after_every_fold_that_saw_it() {
    loom::model(|| {
        let slot = Arc::new(AtomicU32::new(1)); // 1 = published, 0 = retired
        let folding = Arc::new(AtomicU32::new(0));
        let freed = Arc::new(AtomicBool::new(false));
        let (s, f, fr) = (slot.clone(), folding.clone(), freed.clone());
        let folder = thread::spawn(move || {
            f.fetch_add(1, Ordering::Relaxed);
            fence(Ordering::SeqCst);
            if s.load(Ordering::Acquire) == 1 {
                assert!(!fr.load(Ordering::Relaxed), "wrote into a freed chunk");
            }
            f.fetch_sub(1, Ordering::Release);
        });
        slot.swap(0, Ordering::SeqCst); // retire
        fence(Ordering::SeqCst); // the next trim pass
        if folding.load(Ordering::Acquire) == 0 {
            freed.store(true, Ordering::Relaxed); // reap
        }
        folder.join().unwrap();
    });
}
