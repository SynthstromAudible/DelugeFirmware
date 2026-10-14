//! Forms the community firmware still reads from official and early community files.

use deluge_upgrade::passes::community::{
    fm_filters_off, mod_wheel_to_y, old_arpeggiator_mode, old_note_data, song_compressor,
};
use deluge_upgrade::report::Report;
use deluge_upgrade::tree::Node;
use deluge_upgrade::upgrade::Context;
use deluge_upgrade::version::FirmwareVersion;
use std::path::Path;

fn run(xml: &str, version: &str, pass: fn(&mut Node, &mut Context)) -> Node {
    let mut doc = Node::parse_xml(xml).unwrap();
    let mut report = Report::default();
    let mut ctx = Context::new(Path::new("t.XML"), None, &mut report);
    ctx.version = FirmwareVersion::parse(version);
    pass(&mut doc, &mut ctx);
    doc
}

fn same(actual: &Node, xml: &str) {
    assert_eq!(actual, &Node::parse_xml(xml).unwrap());
}

#[test]
fn an_old_fm_sound_has_no_filters() {
    same(
        &run(
            r#"<sound mode="fm" lpfMode="12dB" hpfMode="HPLadder"/>"#,
            "4.1.4-alpha",
            fm_filters_off,
        ),
        r#"<sound mode="fm" lpfMode="Off" hpfMode="Off"/>"#,
    );
    same(
        &run(
            r#"<sound mode="subtractive" lpfMode="12dB"/>"#,
            "4.1.4-alpha",
            fm_filters_off,
        ),
        r#"<sound mode="subtractive" lpfMode="12dB"/>"#,
    );
}

#[test]
fn the_old_arpeggiator_mode_splits_while_the_split_settings_are_default() {
    same(
        &run(
            r#"<arpeggiator mode="both" syncLevel="7"/>"#,
            "4.1.4-alpha",
            old_arpeggiator_mode,
        ),
        r#"<arpeggiator arpMode="arp" noteMode="up" octaveMode="alt" syncLevel="7"/>"#,
    );
    same(
        &run(r#"<arpeggiator mode="down"/>"#, "", old_arpeggiator_mode),
        r#"<arpeggiator arpMode="arp" noteMode="down" octaveMode="down"/>"#,
    );
    same(
        &run(r#"<arpeggiator mode="off"/>"#, "", old_arpeggiator_mode),
        r#"<arpeggiator arpMode="off" noteMode="up" octaveMode="up"/>"#,
    );
    // A split setting read first keeps the old mode out.
    same(
        &run(
            r#"<arpeggiator noteMode="random" mode="up"/>"#,
            "",
            old_arpeggiator_mode,
        ),
        r#"<arpeggiator noteMode="random"/>"#,
    );
}

#[test]
fn old_note_data_becomes_split_probability() {
    // One note: position 0x10, length 0x20, velocity 0x40, lift 0x30, probability 5 (a plain probability).
    same(
        &run(
            r#"<noteRow noteDataWithLift="0x0000001000000020403005"/>"#,
            "c1.2.1",
            old_note_data,
        ),
        r#"<noteRow noteDataWithSplitProb="0x0000001000000020403005000000"/>"#,
    );
}

#[test]
fn old_probability_bytes_become_fill_and_iterance() {
    // Three notes: probability 0 (fill), 0x80 (not fill), 23 (iterance preset 3: 1 of 2).
    let out = run(
        r#"<noteRow noteDataWithLift="0x000000000000000140400000000000000000014040800000000000000001404017"/>"#,
        "4.1.4-alpha",
        old_note_data,
    );
    let hex = out
        .get("noteRow")
        .unwrap()
        .get("noteDataWithSplitProb")
        .unwrap()
        .text()
        .unwrap()
        .to_string();
    let notes: Vec<&str> = hex.as_bytes()[2..]
        .chunks(28)
        .map(|c| std::str::from_utf8(c).unwrap())
        .collect();
    assert_eq!(notes.len(), 3);
    assert_eq!(&notes[0][18..], "4014000002");
    assert_eq!(&notes[1][18..], "4014000001");
    assert_eq!(&notes[2][18..], "4014020100");
}

#[test]
fn plain_note_data_wins_and_takes_the_default_lift() {
    same(
        &run(
            r#"<noteRow noteData="0x0000000000000001407F" noteDataWithLift="0x00000000000000014011FF"/>"#,
            "4.1.4-alpha",
            old_note_data,
        ),
        r#"<noteRow noteDataWithSplitProb="0x000000000000000140407F000000"/>"#,
    );
}

#[test]
fn from_1_3_lift_data_beside_split_probability_is_dropped() {
    same(
        &run(
            r#"<noteRow noteDataWithLift="0x1" noteDataWithSplitProb="0x2"/>"#,
            "c1.3.0",
            old_note_data,
        ),
        r#"<noteRow noteDataWithSplitProb="0x2"/>"#,
    );
}

#[test]
fn the_song_compressor_is_its_audio_compressor_where_it_stands() {
    same(
        &run(
            r#"<song><songCompressor attack="1" release="2" thresh="3" ratio="4" compHPF="5" junk="6"/></song>"#,
            "4.1.4-alpha",
            song_compressor,
        ),
        r#"<song><audioCompressor attack="1" release="2" thresh="3" ratio="4" compHPF="5"/></song>"#,
    );
}

#[test]
fn a_song_compressor_and_an_audio_compressor_keep_their_order_so_the_later_still_wins() {
    same(
        &run(
            r#"<song><audioCompressor attack="1"/><songCompressor attack="2"/></song>"#,
            "4.1.4-alpha",
            song_compressor,
        ),
        r#"<song><audioCompressor attack="1"/><audioCompressor attack="2"/></song>"#,
    );
    same(
        &run(
            r#"<song><songCompressor attack="2"/><audioCompressor attack="1"/></song>"#,
            "4.1.4-alpha",
            song_compressor,
        ),
        r#"<song><audioCompressor attack="2"/><audioCompressor attack="1"/></song>"#,
    );
}

#[test]
fn an_old_midi_clip_mod_wheel_is_its_y_expression() {
    same(
        &run(
            r#"<instrumentClip><midiParams><param><cc>1</cc><value>0x10</value></param><param><cc>7</cc><value>0x20</value></param></midiParams></instrumentClip>"#,
            "4.1.4-alpha",
            mod_wheel_to_y,
        ),
        r#"<instrumentClip><midiParams><param><cc>7</cc><value>0x20</value></param></midiParams><expressionData yExpression="0x10"/></instrumentClip>"#,
    );
}

#[test]
fn old_note_data_that_does_not_parse_is_reported() {
    let mut doc = Node::parse_xml(r#"<noteRow noteData="0x0000000000000060ZZ14"/>"#).unwrap();
    let mut report = Report::default();
    let mut ctx = Context::new(Path::new("t.XML"), None, &mut report);
    ctx.version = FirmwareVersion::parse("4.1.4-alpha");
    old_note_data(&mut doc, &mut ctx);
    assert_eq!(report.findings.len(), 1);
}
