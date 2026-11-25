//! The fold-then-publish protocol under loom: two children fold into a parent's value and then set their mask bits; a
//! reader that sees the full mask must see both values folded in. Run with `RUSTFLAGS="--cfg loom"`.
#![cfg(loom)]

use deluge_storage_sample_peaks::Peak;
use loom::sync::Arc;
use loom::sync::atomic::{AtomicU32, Ordering};
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
