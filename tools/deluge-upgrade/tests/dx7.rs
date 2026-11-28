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

/// The names of the `defaultParams` entries of the document's sound, in order.
fn default_params(doc: &Node) -> Vec<String> {
    doc.get("sound")
        .and_then(|sound| sound.get("defaultParams"))
        .map(|params| params.entries().iter().map(|(key, _)| key.clone()).collect())
        .unwrap_or_default()
}

#[test]
fn a_dx7_on_osc_1_with_a_silent_osc_2_becomes_the_dx7_engine() {
    let (doc, notes) = run(r#"<sound mode="subtractive">
        <osc1 type="dx7" transpose="-12" cents="5" retrigPhase="-1" dx7patch="ABCD" dx7enginemode="1"/>
        <osc2 type="square" transpose="0"/>
        <defaultParams oscAVolume="0x7FFFFFFF" oscBVolume="0x80000000" oscBPulseWidth="0x10000000"
                       volume="0x4CCCCCA8">
            <patchCables><patchCable source="velocity" destination="oscAVolume" amount="0x3FFFFFE8"/></patchCables>
        </defaultParams>
    </sound>"#);
    same(
        &doc,
        r#"<sound mode="dx7">
        <dx7 transpose="-12" cents="5" retrigPhase="-1" patch="ABCD" engineMode="1"/>
        <defaultParams volume="0x4CCCCCA8" dx7Level="0x7FFFFFFF">
            <patchCables><patchCable source="velocity" destination="dx7Level" amount="0x3FFFFFE8"/></patchCables>
        </defaultParams>
    </sound>"#,
    );
    assert!(notes.is_empty(), "{notes:?}");
}

#[test]
fn a_sound_without_a_mode_is_subtractive() {
    let (doc, _) = run(
        r#"<sound><osc1 type="dx7" dx7patch="AB"/><osc2 type="sine"/><defaultParams oscBVolume="0x80000000" volume="0x4CCCCCA8"/></sound>"#,
    );
    same(
        &doc,
        r#"<sound><dx7 patch="AB"/><defaultParams volume="0x4CCCCCA8"/><mode>dx7</mode></sound>"#,
    );
}

#[test]
fn an_audible_other_oscillator_is_dropped_and_noted() {
    let (doc, notes) = run(r#"<sound mode="subtractive">
        <osc1 type="dx7" dx7patch="AB"/>
        <osc2 type="saw"/>
        <defaultParams oscBVolume="0x7FFFFFFF" volume="0x4CCCCCA8"/>
    </sound>"#);
    same(
        &doc,
        r#"<sound mode="dx7"><dx7 patch="AB"/><defaultParams volume="0x4CCCCCA8"/></sound>"#,
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
        let notes = run(xml).1;
        assert_eq!(notes.first().map(String::as_str), Some("osc2 dropped: saw"), "{xml}");
    }
}

/// A song whose instrument `Bell` plays a DX7 on `osc` (its params named with `letter`) with a value, a cable, a mod
/// knob, a MIDI knob and clip automation on each of the three params it takes, other oscillator engine params with a
/// cable, knobs and automation of their own, and an instrument `Other` with none of it.
fn dx7_song(osc: &str, letter: &str) -> String {
    let other = if osc == "osc1" { "osc2" } else { "osc1" };
    format!(
        r#"<song>
        <instruments>
            <sound presetName="Bell" mode="subtractive">
                <{osc} type="dx7" transpose="7" dx7patch="AB"/>
                <{other} type="square"/>
                <defaultParams osc{letter}Volume="0x40000000" osc{letter}PulseWidth="0x10000000"
                               osc{letter}PitchAdjust="0x00000100" noiseVolume="0x80000000"
                               modulator1Amount="0x8000000000000000000000FF" volume="0x4CCCCCA8"
                               waveFold="0x80000000">
                    <patchCables>
                        <patchCable source="velocity" destination="osc{letter}Volume" amount="0x3FFFFFE8"/>
                        <patchCable source="lfo1" destination="osc{letter}PhaseWidth" amount="0x10000000">
                            <depthControlledBy><patchCable source="envelope2" amount="0x08000000"/></depthControlledBy>
                        </patchCable>
                        <patchCable source="lfo2" destination="osc{letter}Pitch" amount="0x20000000"/>
                        <patchCable source="random" destination="noiseVolume" amount="0x20000000"/>
                    </patchCables>
                </defaultParams>
                <modKnobs><modKnob controlsParam="osc{letter}Volume"/><modKnob controlsParam="osc{letter}PhaseWidth"
                    patchAmountFromSource="lfo1"/><modKnob controlsParam="noiseVolume"/><modKnob
                    controlsParam="volume"/></modKnobs>
                <midiKnobs><midiKnob controlsParam="osc{letter}Pitch" cc="74"/><midiKnob controlsParam="noiseVolume"
                    cc="75"/></midiKnobs>
            </sound>
            <sound presetName="Other" mode="subtractive"><osc1 type="saw"/></sound>
        </instruments>
        <sessionClips>
            <instrumentClip instrumentPresetName="Bell">
                <soundParams osc{letter}Volume="0x2000000000000000000000FF" osc{letter}PulseWidth="0x30000000"
                             osc{letter}PitchAdjust="0x0000020000000000000000F0"
                             noiseVolume="0x2000000000000000000000FF" pan="0x00000000"/>
            </instrumentClip>
            <instrumentClip instrumentPresetName="Other"><soundParams osc{letter}Volume="0x20000000"/></instrumentClip>
        </sessionClips>
    </song>"#
    )
}

/// What `dx7_song(_, letter)` upgrades to: the DX7's params hold the oscillator's values, cables, knobs and clip
/// automation, and no oscillator engine param is left on the DX7-mode sound.
fn dx7_song_upgraded(letter: &str) -> String {
    format!(
        r#"<song>
        <instruments>
            <sound presetName="Bell" mode="dx7">
                <dx7 transpose="7" patch="AB"/>
                <defaultParams volume="0x4CCCCCA8" waveFold="0x80000000" dx7Level="0x40000000"
                               dx7AmpMod="0x10000000" dx7PitchAdjust="0x00000100">
                    <patchCables>
                        <patchCable source="velocity" destination="dx7Level" amount="0x3FFFFFE8"/>
                        <patchCable source="lfo1" destination="dx7AmpMod" amount="0x10000000">
                            <depthControlledBy><patchCable source="envelope2" amount="0x08000000"/></depthControlledBy>
                        </patchCable>
                        <patchCable source="lfo2" destination="dx7PitchAdjust" amount="0x20000000"/>
                    </patchCables>
                </defaultParams>
                <modKnobs><modKnob controlsParam="dx7Level"/><modKnob controlsParam="dx7AmpMod"
                    patchAmountFromSource="lfo1"/><modKnob controlsParam="none"/><modKnob
                    controlsParam="volume"/></modKnobs>
                <midiKnobs><midiKnob controlsParam="dx7PitchAdjust" cc="74"/></midiKnobs>
            </sound>
            <sound presetName="Other" mode="subtractive"><osc1 type="saw"/></sound>
        </instruments>
        <sessionClips>
            <instrumentClip instrumentPresetName="Bell">
                <soundParams pan="0x00000000" dx7Level="0x2000000000000000000000FF" dx7AmpMod="0x30000000"
                             dx7PitchAdjust="0x0000020000000000000000F0"/>
            </instrumentClip>
            <instrumentClip instrumentPresetName="Other"><soundParams osc{letter}Volume="0x20000000"/></instrumentClip>
        </sessionClips>
    </song>"#
    )
}

/// What the pass notes for `dx7_song`: the other oscillator (a square at its unwritten, full volume), and the other
/// oscillator engine params' cable, automation and knobs.
fn dx7_song_notes(other: &str) -> Vec<String> {
    vec![
        format!("{other} dropped: square"),
        "cable from random to noiseVolume dropped".to_string(),
        "automation of modulator1Amount dropped".to_string(),
        "modKnobs knob on noiseVolume dropped".to_string(),
        "midiKnobs knob on noiseVolume dropped".to_string(),
        "a clip's automation of noiseVolume dropped".to_string(),
    ]
}

#[test]
fn a_dx7_on_osc_1_takes_osc_as_params_cables_knobs_and_clip_automation() {
    let (doc, notes) = run(&dx7_song("osc1", "A"));
    same(&doc, &dx7_song_upgraded("A"));
    assert_eq!(notes, dx7_song_notes("osc2"));
}

#[test]
fn a_dx7_on_osc_2_takes_osc_bs_params_cables_knobs_and_clip_automation() {
    let (doc, notes) = run(&dx7_song("osc2", "B"));
    same(&doc, &dx7_song_upgraded("B"));
    assert_eq!(notes, dx7_song_notes("osc1"));
}

#[test]
fn a_dx7_drops_the_other_oscillators_params_and_cables() {
    let (doc, notes) = run(r#"<sound mode="subtractive">
        <osc1 type="square"/><osc2 type="dx7"/>
        <defaultParams oscAVolume="0x80000000" oscAPitchAdjust="0x00000100" oscBVolume="0x40000000">
            <patchCables><patchCable source="lfo1" destination="oscAPitch" amount="0x10000000"/></patchCables>
        </defaultParams>
    </sound>"#);
    same(
        &doc,
        r#"<sound mode="dx7"><dx7/><defaultParams dx7Level="0x40000000"/></sound>"#,
    );
    assert_eq!(notes, vec!["cable from lfo1 to oscAPitch dropped".to_string()]);
}

#[test]
fn a_kit_drums_dx7_moves_its_rows_params_onto_the_dx7s() {
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
                    <noteRow drumIndex="1"><soundParams oscAVolume="0x80000000" oscBVolume="0x20000000"/></noteRow>
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
                        <defaultParams dx7Level="0x7FFFFFFF"/></sound>
                </soundSources>
            </kit>
        </instruments>
        <sessionClips>
            <instrumentClip instrumentPresetName="Kit">
                <noteRows>
                    <noteRow drumIndex="0"><soundParams oscBVolume="0x10000000"/></noteRow>
                    <noteRow drumIndex="1"><soundParams dx7Level="0x20000000"/></noteRow>
                </noteRows>
            </instrumentClip>
        </sessionClips>
    </song>"#,
    );
}

#[test]
fn a_dx7_mode_sound_keeps_no_oscillator_engine_param_and_no_zero_pitch_adjust() {
    let (doc, _) = run(r#"<sound mode="subtractive"><osc1 type="dx7"/><osc2 type="saw"/>
        <defaultParams oscAVolume="0x7FFFFFFF" oscAPulseWidth="0x00000000" oscAPitchAdjust="0x00000000"
                       oscAWavetablePosition="0x00000000"
                       oscBVolume="0x80000000" oscBPulseWidth="0x00000000" oscBWavetablePosition="0x00000000"
                       noiseVolume="0x80000000" modulator1Amount="0x80000000" modulator1Feedback="0x80000000"
                       modulator2Amount="0x80000000" modulator2Feedback="0x80000000" carrier1Feedback="0x80000000"
                       carrier2Feedback="0x80000000" oscBPitchAdjust="0x00000000" mod1PitchAdjust="0x00000000"
                       mod2PitchAdjust="0x00000000" volume="0x4CCCCCA8"/></sound>"#);
    assert_eq!(
        default_params(&doc),
        vec!["volume".to_string(), "dx7Level".to_string(), "dx7AmpMod".to_string()]
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
