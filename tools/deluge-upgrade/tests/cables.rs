//! Cables and knobs in their pre-3.2 and pre-4.0 forms, rewritten as the firmware read them.

use deluge_upgrade::passes::cables::{default_expression_cables, range_forms};
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

fn same(actual: &Node, xml: &str) {
    assert_eq!(actual, &Node::parse_xml(xml).unwrap());
}

#[test]
fn range_cables_control_the_depth_of_the_last_adjustable_cable() {
    same(
        &run(
            r#"<sound><defaultParams><patchCables>
                <patchCable source="lfo1" destination="range" amount="0x10000000"/>
                <patchCable source="envelope1" destination="pitch" amount="0x20000000" rangeAdjustable="1"/>
                <patchCable source="aftertouch" destination="range" amount="0x40000000"/>
                <patchCable source="note" destination="lpfFrequency" amount="0x05000000" rangeAdjustable="1"/>
            </patchCables></defaultParams></sound>"#,
            range_forms,
        ),
        r#"<sound><defaultParams><patchCables>
                <patchCable source="envelope1" destination="pitch" amount="0x20000000"/>
                <patchCable source="note" destination="lpfFrequency" amount="0x05000000"><depthControlledBy>
                    <patchCable source="lfo1" amount="0x10000000" polarity="bipolar"/>
                    <patchCable source="aftertouch" amount="0x40000000" polarity="unipolar"/>
                </depthControlledBy></patchCable>
            </patchCables></defaultParams></sound>"#,
    );
}

#[test]
fn range_cables_with_nothing_adjustable_go_nowhere() {
    same(
        &run(
            r#"<sound><defaultParams><patchCables><patchCable source="lfo1" destination="range" amount="0x1"/></patchCables></defaultParams></sound>"#,
            range_forms,
        ),
        r#"<sound><defaultParams><patchCables><patchCable source="lfo1" destination="none" amount="0x1"/></patchCables></defaultParams></sound>"#,
    );
}

#[test]
fn a_range_knob_keeps_its_slot_default_and_a_range_midi_knob_is_dropped() {
    let mut knobs = String::from("<modKnobs>");
    for i in 0..16 {
        knobs.push_str(if i == 12 {
            r#"<modKnob controlsParam="range"/>"#
        } else {
            r#"<modKnob controlsParam="pan"/>"#
        });
    }
    knobs.push_str("</modKnobs>");
    let doc = run(
        &format!(
            r#"<sound>{knobs}<midiKnobs><midiKnob controlsParam="range"/><midiKnob controlsParam="pan"/></midiKnobs></sound>"#
        ),
        range_forms,
    );
    let sound = doc.get("sound").unwrap();
    let knob = &sound.get("modKnobs").unwrap().entries()[12].1;
    assert_eq!(
        knob.get("controlsParam").and_then(Node::text),
        Some("portamento")
    );
    assert_eq!(sound.get("midiKnobs").unwrap().entries().len(), 1);
}

#[test]
fn a_pre_4_sound_gets_aftertouch_and_y_cables_unless_it_has_its_own() {
    let cables = |xml: &str| {
        let doc = run(xml, default_expression_cables);
        let set = doc
            .get("sound")
            .unwrap()
            .get("defaultParams")
            .unwrap()
            .get("patchCables")
            .unwrap();
        Node::Map(vec![("patchCables".to_string(), set.clone())])
    };
    same(
        &cables(r#"<sound mode="subtractive"><defaultParams/></sound>"#),
        r#"<patchCables>
            <patchCable source="aftertouch" destination="volume" polarity="unipolar" amount="0x2A3D7094"/>
            <patchCable source="y" destination="lpfFrequency" polarity="bipolar" amount="0x19999990"/>
        </patchCables>"#,
    );
    let fm = cables(r#"<sound mode="fm"><defaultParams/></sound>"#);
    assert_eq!(
        fm.get("patchCables").unwrap().entries()[1]
            .1
            .get("destination")
            .and_then(Node::text),
        Some("modulator1Volume")
    );
    assert_eq!(
        fm.get("patchCables").unwrap().entries()[1]
            .1
            .get("amount")
            .and_then(Node::text),
        Some("0x1333332C")
    );
    let own = cables(
        r#"<sound><defaultParams><patchCables><patchCable source="x" destination="pan" amount="0x1"/></patchCables></defaultParams></sound>"#,
    );
    assert_eq!(own.get("patchCables").unwrap().entries().len(), 1);
}

#[test]
fn a_full_cable_set_gets_no_more() {
    let mut set = String::new();
    for _ in 0..31 {
        set.push_str(r#"<patchCable source="lfo1" destination="pan" amount="0x1"/>"#);
    }
    let doc = run(
        &format!(
            r#"<sound><defaultParams><patchCables>{set}</patchCables></defaultParams></sound>"#
        ),
        default_expression_cables,
    );
    let n = doc
        .get("sound")
        .unwrap()
        .get("defaultParams")
        .unwrap()
        .get("patchCables")
        .unwrap()
        .entries()
        .len();
    assert_eq!(n, 32); // room for aftertouch only
}
