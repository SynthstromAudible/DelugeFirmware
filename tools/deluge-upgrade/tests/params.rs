//! Param values official firmware recalculated on loading an old file.

use deluge_upgrade::passes::params::{
    arpeggiator_rate, bend_range, clip_param_overlay, kit_resonance, nested_filter_morph,
    paramless_kit, sound_resonance, swing_interval,
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

#[test]
fn an_arrangement_only_clip_is_converted_as_a_session_clip_is() {
    let doc = run(
        r#"<song><arrangementOnlyTracks><instrumentClip><arpeggiator mode="up" syncLevel="0"/>
           <soundParams arpeggiatorRate="0x10000000"/></instrumentClip></arrangementOnlyTracks></song>"#,
        arpeggiator_rate,
    );
    assert_eq!(
        param(
            doc.get("song")
                .unwrap()
                .get("arrangementOnlyTracks")
                .unwrap()
                .get("instrumentClip")
                .unwrap(),
            &["soundParams", "arpeggiatorRate"]
        ),
        Some("0x60000000")
    );
    let doc = run(
        r#"<song><arrangementOnlyTracks><instrumentClip/></arrangementOnlyTracks></song>"#,
        bend_range,
    );
    assert_eq!(
        param(
            &doc,
            &[
                "song",
                "arrangementOnlyTracks",
                "instrumentClip",
                "bendRange"
            ]
        ),
        Some("12")
    );
}

#[test]
fn a_pre_3_2_clip_without_expression_of_its_own_bends_twelve_semitones() {
    let doc = run(
        r#"<song><sessionClips><instrumentClip/><instrumentClip><expressionData/></instrumentClip>
           <instrumentClip><midiParams><param><cc>bend</cc></param></midiParams></instrumentClip>
           <instrumentClip bendRange="2"/></sessionClips></song>"#,
        bend_range,
    );
    let clips = doc
        .get("song")
        .unwrap()
        .get("sessionClips")
        .unwrap()
        .entries();
    let bend = |i: usize| clips[i].1.get("bendRange").and_then(Node::text);
    assert_eq!(bend(0), Some("12"));
    assert_eq!(bend(1), None);
    assert_eq!(bend(2), None);
    assert_eq!(bend(3), Some("2"));
}

#[test]
fn a_pre_1_2_synth_clip_takes_a_param_it_does_not_hold_from_its_synth() {
    let doc = run(
        r#"<song><instruments><sound presetName="A"><defaultParams volume="0x10" pan="0x20"/></sound></instruments>
           <sessionClips><instrumentClip instrumentPresetName="A"><soundParams pan="0x30"/></instrumentClip></sessionClips></song>"#,
        clip_param_overlay,
    );
    let clip = ["song", "sessionClips", "instrumentClip", "soundParams"];
    assert_eq!(
        param(&doc, &[&clip[..], &["volume"]].concat()),
        Some("0x10")
    );
    assert_eq!(param(&doc, &[&clip[..], &["pan"]].concat()), Some("0x30"));
}

#[test]
fn a_pre_1_2_drum_row_takes_a_param_it_does_not_hold_from_its_drum() {
    let doc = run(
        r#"<song><instruments><kit presetName="K"><soundSources>
           <sound><defaultParams volume="0x11"/></sound><sound><defaultParams volume="0x22" pan="0x33"/></sound>
           </soundSources></kit></instruments>
           <sessionClips><instrumentClip instrumentPresetName="K"><noteRows>
           <noteRow drumIndex="1"><soundParams pan="0x44"/></noteRow></noteRows></instrumentClip></sessionClips></song>"#,
        clip_param_overlay,
    );
    let row = [
        "song",
        "sessionClips",
        "instrumentClip",
        "noteRows",
        "noteRow",
        "soundParams",
    ];
    assert_eq!(param(&doc, &[&row[..], &["volume"]].concat()), Some("0x22"));
    assert_eq!(param(&doc, &[&row[..], &["pan"]].concat()), Some("0x44"));
}

#[test]
fn a_pre_2_2_kit_preset_without_params_gets_the_defaults_the_firmware_gave_it() {
    let doc = run(r#"<kit><soundSources/></kit>"#, paramless_kit);
    assert_eq!(
        doc.get("kit").unwrap().get("defaultParams"),
        Some(&Node::Map(vec![]))
    );
    let doc = run(r#"<kit><defaultParams volume="0x1"/></kit>"#, paramless_kit);
    assert_eq!(
        param(&doc, &["kit", "defaultParams", "volume"]),
        Some("0x1")
    );
}

#[test]
fn a_nested_filter_morph_in_kit_and_song_params_becomes_flat() {
    let doc = run(
        r#"<kit><defaultParams><lpf frequency="0x1" morph="0x2"/><hpf morph="0x3"/></defaultParams></kit>"#,
        nested_filter_morph,
    );
    assert_eq!(
        param(&doc, &["kit", "defaultParams", "lpfMorph"]),
        Some("0x2")
    );
    assert_eq!(
        param(&doc, &["kit", "defaultParams", "hpfMorph"]),
        Some("0x3")
    );
    assert_eq!(
        param(&doc, &["kit", "defaultParams", "lpf", "frequency"]),
        Some("0x1")
    );
    assert!(
        doc.get("kit")
            .unwrap()
            .get("defaultParams")
            .unwrap()
            .get("lpf")
            .unwrap()
            .get("morph")
            .is_none()
    );
    // A flat morph read after the nested one wins; one read before it does not.
    let doc = run(
        r#"<song><songParams lpfMorph="0x5"><lpf morph="0x6"/></songParams></song>"#,
        nested_filter_morph,
    );
    assert_eq!(
        param(&doc, &["song", "songParams", "lpfMorph"]),
        Some("0x6")
    );
    let doc = run(
        r#"<song><songParams><lpf morph="0x6"/><lpfMorph>0x7</lpfMorph></songParams></song>"#,
        nested_filter_morph,
    );
    assert_eq!(
        param(&doc, &["song", "songParams", "lpfMorph"]),
        Some("0x7")
    );
}

#[test]
fn a_pre_1_2_sound_without_a_volume_gets_back_what_its_default_volume_lost() {
    let with_default = run(
        r#"<sound><defaultParams lpfResonance="0x80000000" volume="0x4CCCCCA8"/></sound>"#,
        sound_resonance,
    );
    let without = run(
        r#"<sound><defaultParams lpfResonance="0x80000000"/></sound>"#,
        sound_resonance,
    );
    let volume = |doc: &Node| param(doc, &["sound", "defaultParams", "volume"]).map(str::to_string);
    assert_ne!(volume(&with_default).as_deref(), Some("0x4CCCCCA8"));
    assert_eq!(volume(&without), volume(&with_default));
}
