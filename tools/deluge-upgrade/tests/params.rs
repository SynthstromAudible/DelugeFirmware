//! Param values official firmware recalculated on loading an old file.

use deluge_upgrade::passes::params::{
    arpeggiator_rate, kit_resonance, sound_resonance, swing_interval,
};
use deluge_upgrade::report::Report;
use deluge_upgrade::tree::Node;
use deluge_upgrade::upgrade::Context;
use std::path::Path;

fn run(xml: &str, pass: fn(&mut Node, &mut Context)) -> Node {
    let mut doc = Node::parse_xml(xml).unwrap();
    let mut report = Report::default();
    pass(
        &mut doc,
        &mut Context::new(Path::new("t.XML"), None, &mut report),
    );
    doc
}

fn param<'a>(doc: &'a Node, path: &[&str]) -> Option<&'a str> {
    let mut node = doc;
    for key in path {
        node = node.get(key)?;
    }
    node.text()
}

#[test]
fn a_pre_1_2_sound_gets_back_the_volume_its_resonance_cost() {
    // Resonance at its top (0x7FFFFFFF): the table's last step, 1024 units = 1/64 dB, under the 0.1 dB threshold.
    let doc = run(
        r#"<sound><defaultParams volume="0x40000000" lpfResonance="0x7FFFFFFF"/></sound>"#,
        sound_resonance,
    );
    assert_eq!(
        param(&doc, &["sound", "defaultParams", "volume"]),
        Some("0x40000000")
    );
    // Resonance at its bottom (0x80000000): 8192 units = 1/8 dB, shifted up from the volume's interval.
    let doc = run(
        r#"<sound><defaultParams volume="0x40000000" lpfResonance="0x80000000"/></sound>"#,
        sound_resonance,
    );
    assert_ne!(
        param(&doc, &["sound", "defaultParams", "volume"]),
        Some("0x40000000")
    );
}

#[test]
fn an_open_filter_with_a_little_resonance_has_none_unless_something_moves_it() {
    let doc = run(
        r#"<sound><defaultParams lpfFrequency="0x7FFFFFFF" lpfResonance="0x00000000"/></sound>"#,
        sound_resonance,
    );
    assert_eq!(
        param(&doc, &["sound", "defaultParams", "lpfResonance"]),
        Some("0x80000000")
    );
    let doc = run(
        r#"<sound><defaultParams lpfFrequency="0x7FFFFFFF" lpfResonance="0x00000000"><patchCables>
           <patchCable source="y" destination="lpfFrequency" amount="0x1"/></patchCables></defaultParams></sound>"#,
        sound_resonance,
    );
    assert_eq!(
        param(&doc, &["sound", "defaultParams", "lpfResonance"]),
        Some("0x00000000")
    );
    let fm = run(
        r#"<sound mode="fm"><defaultParams lpfFrequency="0x7FFFFFFF" lpfResonance="0x00000000"/></sound>"#,
        sound_resonance,
    );
    assert_eq!(
        param(&fm, &["sound", "defaultParams", "lpfResonance"]),
        Some("0x00000000")
    );
}

#[test]
fn a_pre_1_2_kit_zeroes_its_filters_as_the_firmware_checked_them() {
    let doc = run(
        r#"<kit><defaultParams volume="0x40000000"><lpf frequency="0x7FFFFFFF" resonance="0x00000000"/>
           <hpf frequency="0x80000000" resonance="0x20000000"/></defaultParams></kit>"#,
        kit_resonance,
    );
    assert_eq!(
        param(&doc, &["kit", "defaultParams", "lpf", "resonance"]),
        Some("0x80000000")
    );
    // The HPF's check reads the LPF's resonance, as official firmware's did: it held 0, not a quarter.
    assert_eq!(
        param(&doc, &["kit", "defaultParams", "hpf", "resonance"]),
        Some("0x20000000")
    );
}

#[test]
fn a_pre_1_5_synth_clip_unsynced_arpeggiator_runs_faster() {
    let doc = run(
        r#"<song><sessionClips><instrumentClip><arpeggiator mode="up" syncLevel="0"/>
           <soundParams arpeggiatorRate="0x10000000"/></instrumentClip>
           <instrumentClip><arpeggiator mode="up" syncLevel="7"/><soundParams arpeggiatorRate="0x10000000"/></instrumentClip>
           <instrumentClip><arpeggiator mode="up" syncLevel="0"/><soundParams arpeggiatorRate="0x7F000000"/></instrumentClip>
           </sessionClips></song>"#,
        arpeggiator_rate,
    );
    let clips = doc
        .get("song")
        .unwrap()
        .get("sessionClips")
        .unwrap()
        .entries();
    let rate = |i: usize| {
        clips[i]
            .1
            .get("soundParams")
            .unwrap()
            .get("arpeggiatorRate")
            .unwrap()
            .text()
            .unwrap()
    };
    assert_eq!(rate(0), "0x60000000");
    assert_eq!(rate(1), "0x10000000");
    assert_eq!(rate(2), "0x7FFFFFFF");
}

#[test]
fn a_pre_3_1_swing_interval_becomes_relative_to_the_tick_magnitude() {
    let doc = run(
        r#"<song swingInterval="7" inputTickMagnitude="2"/>"#,
        swing_interval,
    );
    assert_eq!(param(&doc, &["song", "swingInterval"]), Some("8"));
    let doc = run(r#"<song swingInterval="12"/>"#, swing_interval); // capped at 9, magnitude 1
    assert_eq!(param(&doc, &["song", "swingInterval"]), Some("9"));
    let doc = run(r#"<song swingInterval="0"/>"#, swing_interval);
    assert_eq!(param(&doc, &["song", "swingInterval"]), Some("0"));
    let doc = run(
        r#"<song swingInterval="1" inputTickMagnitude="0"/>"#,
        swing_interval,
    );
    assert_eq!(param(&doc, &["song", "swingInterval"]), Some("1")); // left as it was, and reported
}
