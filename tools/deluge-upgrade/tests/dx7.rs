//! A DX7 on an oscillator, as every released firmware wrote one, becomes the sound's DX7 engine.

use deluge_upgrade::passes::dx7::dx7_engine;
use deluge_upgrade::report::Report;
use deluge_upgrade::tree::Node;
use deluge_upgrade::upgrade::Context;
use std::path::Path;

/// `xml` through the DX7 pass, and the details of what it noted.
fn run(xml: &str) -> (Node, Vec<String>) {
    let mut doc = Node::parse_xml(xml).unwrap();
    let mut report = Report::default();
    let mut ctx = Context::new(Path::new("t.XML"), None, &mut report);
    dx7_engine(&mut doc, &mut ctx);
    let notes = report.findings.iter().map(|f| f.detail.clone()).collect();
    (doc, notes)
}

fn same(actual: &Node, xml: &str) {
    assert_eq!(actual, &Node::parse_xml(xml).unwrap());
}

#[test]
fn a_dx7_on_osc_1_with_a_silent_osc_2_becomes_the_dx7_engine() {
    let (doc, notes) = run(r#"<sound mode="subtractive">
        <osc1 type="dx7" transpose="-12" cents="5" retrigPhase="-1" dx7patch="ABCD" dx7enginemode="1"/>
        <osc2 type="square" transpose="0"/>
        <defaultParams oscAVolume="0x7FFFFFFF" oscBVolume="0x80000000" oscBPulseWidth="0x10000000">
            <patchCables><patchCable source="velocity" destination="oscAVolume" amount="0x3FFFFFE8"/></patchCables>
        </defaultParams>
    </sound>"#);
    same(
        &doc,
        r#"<sound mode="dx7">
        <dx7 transpose="-12" cents="5" retrigPhase="-1" patch="ABCD" engineMode="1"/>
        <defaultParams oscAVolume="0x7FFFFFFF" oscBVolume="0x80000000" oscBPulseWidth="0x10000000">
            <patchCables><patchCable source="velocity" destination="oscAVolume" amount="0x3FFFFFE8"/></patchCables>
        </defaultParams>
    </sound>"#,
    );
    assert!(notes.is_empty(), "{notes:?}");
}

#[test]
fn a_sound_without_a_mode_is_subtractive() {
    let (doc, _) = run(
        r#"<sound><osc1 type="dx7" dx7patch="AB"/><osc2 type="sine"/><defaultParams oscBVolume="0x80000000"/></sound>"#,
    );
    same(
        &doc,
        r#"<sound><dx7 patch="AB"/><defaultParams oscBVolume="0x80000000"/><mode>dx7</mode></sound>"#,
    );
}

#[test]
fn an_audible_other_oscillator_is_dropped_and_noted() {
    let (doc, notes) = run(r#"<sound mode="subtractive">
        <osc1 type="dx7" dx7patch="AB"/>
        <osc2 type="saw"/>
        <defaultParams oscBVolume="0x7FFFFFFF"/>
    </sound>"#);
    same(
        &doc,
        r#"<sound mode="dx7"><dx7 patch="AB"/><defaultParams oscBVolume="0x7FFFFFFF"/></sound>"#,
    );
    assert_eq!(notes, vec!["osc2 dropped: saw".to_string()]);
}

#[test]
fn an_unwritten_synced_or_patched_other_volume_counts_audible() {
    for xml in [
        r#"<sound><osc1 type="dx7"/><osc2 type="saw"/><defaultParams/></sound>"#,
        r#"<sound oscillatorSync="1"><osc1 type="dx7"/><osc2 type="saw"/><defaultParams oscBVolume="0x80000000"/></sound>"#,
        r#"<sound><osc1 type="dx7"/><osc2 type="saw"/><defaultParams oscBVolume="0x8000000000000000000000FF"/></sound>"#,
        r#"<sound><osc1 type="dx7"/><osc2 type="saw"/><defaultParams oscBVolume="0x80000000"><patchCables>
            <patchCable source="lfo1" destination="oscBVolume" amount="0x10000000"/></patchCables></defaultParams></sound>"#,
    ] {
        assert_eq!(run(xml).1, vec!["osc2 dropped: saw".to_string()], "{xml}");
    }
}

#[test]
fn a_dx7_on_osc_2_takes_osc_1s_params_cables_knobs_and_clip_automation() {
    let (doc, notes) = run(r#"<song>
        <instruments>
            <sound presetName="Bell" mode="subtractive">
                <osc1 type="square"/>
                <osc2 type="dx7" transpose="7" dx7patch="AB"/>
                <defaultParams oscAVolume="0x80000000" oscAPulseWidth="0x00000000" oscBVolume="0x40000000"
                               oscBPulseWidth="0x10000000" oscBPitchAdjust="0x00000100" volume="0x4CCCCCA8">
                    <patchCables>
                        <patchCable source="velocity" destination="oscBVolume" amount="0x3FFFFFE8"/>
                        <patchCable source="lfo1" destination="oscAPitchAdjust" amount="0x10000000"/>
                    </patchCables>
                </defaultParams>
                <modKnobs><modKnob controlsParam="oscBVolume"/><modKnob controlsParam="volume"/></modKnobs>
            </sound>
            <sound presetName="Other" mode="subtractive"><osc1 type="saw"/></sound>
        </instruments>
        <sessionClips>
            <instrumentClip instrumentPresetName="Bell">
                <soundParams oscAVolume="0x80000000" oscBVolume="0x2000000000000000000000FF"/>
            </instrumentClip>
            <instrumentClip instrumentPresetName="Other"><soundParams oscBVolume="0x20000000"/></instrumentClip>
        </sessionClips>
    </song>"#);
    same(
        &doc,
        r#"<song>
        <instruments>
            <sound presetName="Bell" mode="dx7">
                <dx7 transpose="7" patch="AB"/>
                <defaultParams oscAVolume="0x40000000" oscAPulseWidth="0x10000000" oscAPitchAdjust="0x00000100"
                               volume="0x4CCCCCA8">
                    <patchCables>
                        <patchCable source="velocity" destination="oscAVolume" amount="0x3FFFFFE8"/>
                    </patchCables>
                </defaultParams>
                <modKnobs><modKnob controlsParam="oscAVolume"/><modKnob controlsParam="volume"/></modKnobs>
            </sound>
            <sound presetName="Other" mode="subtractive"><osc1 type="saw"/></sound>
        </instruments>
        <sessionClips>
            <instrumentClip instrumentPresetName="Bell">
                <soundParams oscAVolume="0x2000000000000000000000FF"/>
            </instrumentClip>
            <instrumentClip instrumentPresetName="Other"><soundParams oscBVolume="0x20000000"/></instrumentClip>
        </sessionClips>
    </song>"#,
    );
    assert_eq!(
        notes,
        vec!["osc1's cable from lfo1 to oscAPitchAdjust dropped".to_string()]
    );
}

#[test]
fn a_kit_drums_dx7_on_osc_2_moves_its_rows_params() {
    let (doc, _) = run(r#"<song>
        <instruments>
            <kit presetName="Kit">
                <soundSources>
                    <sound mode="subtractive"><osc1 type="saw"/></sound>
                    <sound mode="subtractive"><osc1 type="sine"/><osc2 type="dx7"/>
                        <defaultParams oscAVolume="0x80000000" oscBVolume="0x7FFFFFFF"/></sound>
                </soundSources>
            </kit>
        </instruments>
        <sessionClips>
            <instrumentClip instrumentPresetName="Kit">
                <noteRows>
                    <noteRow drumIndex="0"><soundParams oscBVolume="0x10000000"/></noteRow>
                    <noteRow drumIndex="1"><soundParams oscBVolume="0x20000000"/></noteRow>
                </noteRows>
            </instrumentClip>
        </sessionClips>
    </song>"#);
    same(
        &doc,
        r#"<song>
        <instruments>
            <kit presetName="Kit">
                <soundSources>
                    <sound mode="subtractive"><osc1 type="saw"/></sound>
                    <sound mode="dx7"><dx7/>
                        <defaultParams oscAVolume="0x7FFFFFFF"/></sound>
                </soundSources>
            </kit>
        </instruments>
        <sessionClips>
            <instrumentClip instrumentPresetName="Kit">
                <noteRows>
                    <noteRow drumIndex="0"><soundParams oscBVolume="0x10000000"/></noteRow>
                    <noteRow drumIndex="1"><soundParams oscAVolume="0x20000000"/></noteRow>
                </noteRows>
            </instrumentClip>
        </sessionClips>
    </song>"#,
    );
}

#[test]
fn ring_modulation_and_fm_play_what_the_firmware_made_of_a_dx7() {
    let (ringmod, _) = run(
        r#"<sound mode="ringmod"><osc1 type="dx7" dx7patch="AB" dx7enginemode="1"/><osc2 type="saw"/></sound>"#,
    );
    same(
        &ringmod,
        r#"<sound mode="ringmod"><osc1 type="wavetable"/><osc2 type="saw"/></sound>"#,
    );
    let (fm, _) = run(
        r#"<sound mode="fm"><osc1 type="sine"/><osc2 type="dx7" dx7patch="AB" dx7randomdetune="3"/></sound>"#,
    );
    same(
        &fm,
        r#"<sound mode="fm"><osc1 type="sine"/><osc2 type="sine"/></sound>"#,
    );
}
