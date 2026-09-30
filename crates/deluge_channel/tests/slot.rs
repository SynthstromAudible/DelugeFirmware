use deluge_channel::{Answer, AnswerSlot, Orphan, Take};

/// An answer that may carry a handle, as an open does.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum Outcome {
    Opened(u32),
    Failed,
}

impl Answer for Outcome {
    fn orphan(&self) -> Option<u32> {
        match self {
            Outcome::Opened(h) => Some(*h),
            Outcome::Failed => None,
        }
    }
}

/// Nonzero: a publish that dropped its stamp would leave 0.
const CARD: u32 = 3;

fn slot() -> AnswerSlot<String, Outcome> {
    AnswerSlot::new()
}

#[test]
fn an_answer_is_taken_once() {
    let mut s = slot();
    assert_eq!(s.publish("A".into(), Outcome::Opened(1), CARD), None);
    assert_eq!(s.take("A", CARD), Take::Ready(Outcome::Opened(1)));
    assert_eq!(s.take("A", CARD), Take::Absent { orphan: None });
    assert!(s.is_empty());
}

#[test]
fn another_key_leaves_the_answer() {
    let mut s = slot();
    s.publish("A".into(), Outcome::Opened(1), CARD);
    assert_eq!(s.take("B", CARD), Take::Absent { orphan: None });
    assert_eq!(s.key().map(String::as_str), Some("A"));
    assert_eq!(s.take("A", CARD), Take::Ready(Outcome::Opened(1)));
}

#[test]
fn a_stale_answer_is_removed_and_its_handle_orphaned() {
    let mut s = slot();
    s.publish("A".into(), Outcome::Opened(7), CARD);
    assert_eq!(
        s.take("A", CARD + 1),
        Take::Absent {
            orphan: Some(Orphan {
                handle: 7,
                generation: CARD
            })
        }
    );
    assert!(s.is_empty());
}

#[test]
fn a_stale_failure_orphans_nothing() {
    let mut s = slot();
    s.publish("A".into(), Outcome::Failed, CARD);
    assert_eq!(s.take("A", CARD + 1), Take::Absent { orphan: None });
    assert!(s.is_empty());
}

#[test]
fn publishing_over_an_answer_orphans_it() {
    let mut s = slot();
    s.publish("A".into(), Outcome::Opened(2), CARD);
    assert_eq!(
        s.publish("B".into(), Outcome::Failed, CARD),
        Some(Orphan {
            handle: 2,
            generation: CARD
        })
    );
    assert_eq!(s.take("B", CARD), Take::Ready(Outcome::Failed));
}

#[test]
fn a_displaced_answers_orphan_carries_its_own_generation() {
    let mut s = slot();
    s.publish("A".into(), Outcome::Opened(9), CARD);
    assert_eq!(
        s.publish("B".into(), Outcome::Failed, CARD + 5),
        Some(Orphan {
            handle: 9,
            generation: CARD
        })
    );
}

#[test]
fn discard_drops_only_its_own_key() {
    let mut s = slot();
    s.publish("A".into(), Outcome::Opened(4), CARD);
    assert_eq!(s.discard("B"), None);
    assert!(!s.is_empty());
    assert_eq!(
        s.discard("A"),
        Some(Orphan {
            handle: 4,
            generation: CARD
        })
    );
    assert!(s.is_empty());
}

#[test]
fn drain_empties_and_orphans() {
    let mut s = slot();
    s.publish("A".into(), Outcome::Opened(5), CARD);
    assert_eq!(
        s.drain(),
        Some(Orphan {
            handle: 5,
            generation: CARD
        })
    );
    assert!(s.is_empty());
    assert_eq!(s.drain(), None);
}
