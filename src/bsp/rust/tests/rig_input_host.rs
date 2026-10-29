//! Host test for the rig harness's down-lines: the script gestures a host sends over RTT.

use deluge_bsp_rust::peripherals::rig_harness::input::{Command, parse};

#[test]
fn parses_every_command() {
    assert_eq!(
        parse("B 6 1 1"),
        Ok(Command::Button {
            x: 6,
            y: 1,
            down: true
        })
    );
    assert_eq!(
        parse("B 6 1 0"),
        Ok(Command::Button {
            x: 6,
            y: 1,
            down: false
        })
    );
    assert_eq!(
        parse("P 3 5 1 100"),
        Ok(Command::Pad {
            x: 3,
            y: 5,
            down: true,
            vel: 100
        })
    );
    // Without a velocity a press takes the PIC path's default.
    assert_eq!(
        parse("P 17 7 1"),
        Ok(Command::Pad {
            x: 17,
            y: 7,
            down: true,
            vel: 255
        })
    );
    assert_eq!(
        parse("P 3 5 0"),
        Ok(Command::Pad {
            x: 3,
            y: 5,
            down: false,
            vel: 255
        })
    );
    assert_eq!(parse("E 3 4"), Ok(Command::Encoder { idx: 3, edges: 4 }));
    assert_eq!(parse("N"), Ok(Command::NoPresses));
    assert_eq!(parse("S"), Ok(Command::Resend));
    // A trailing carriage return (a telnet line ending) is not part of the command.
    assert_eq!(parse("S\r"), Ok(Command::Resend));
}

#[test]
fn rejects_bad_lines() {
    for bad in [
        "",
        "P 3",
        "P 18 0 1",
        "P 0 8 1",
        "B x 1 1",
        "B 9 0 1",
        "B 0 4 1",
        "E 6 1",
        "E 9 1",
        "P 3 5 2",
        "S 1",
        "Q",
        "P 3 5 1 256",
    ] {
        assert!(parse(bad).is_err(), "{bad:?} parsed");
    }
    assert!(parse(&"P".repeat(200)).is_err());
}

#[test]
fn encoder_edges_keep_sign() {
    assert_eq!(parse("E 2 -8"), Ok(Command::Encoder { idx: 2, edges: -8 }));
}

#[test]
fn queued_edges_go_out_one_per_tick() {
    use deluge_bsp_rust::peripherals::rig_harness::input::EdgePacer;
    let mut p = EdgePacer::default();
    p.add(3, 3);
    p.add(4, -2);
    // One edge per encoder per tick, in its own direction, until each is spent.
    assert_eq!(p.tick(), [0, 0, 0, 1, -1, 0]);
    assert_eq!(p.tick(), [0, 0, 0, 1, -1, 0]);
    assert_eq!(p.tick(), [0, 0, 0, 1, 0, 0]);
    assert_eq!(p.tick(), [0; 6]);
    // A turn back while edges are pending cancels them first.
    p.add(1, 2);
    p.add(1, -3);
    assert_eq!(p.tick(), [0, -1, 0, 0, 0, 0]);
    assert_eq!(p.tick(), [0; 6]);
}
