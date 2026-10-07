//! A sound's old forms, each rewritten as the entries the firmware's reader made of it.

use deluge_upgrade::passes::sound::old_sound_forms;
use deluge_upgrade::report::Report;
use deluge_upgrade::tree::Node;
use deluge_upgrade::upgrade::Context;
use std::path::Path;

fn converted(xml: &str) -> Node {
    let mut doc = Node::parse_xml(xml).unwrap();
    let mut report = Report::default();
    old_sound_forms(
        &mut doc,
        &mut Context::new(Path::new("t.XML"), None, &mut report),
    );
    doc.get("sound").unwrap().clone()
}

fn same(a: &Node, xml: &str) {
    let expected = Node::parse_xml(xml).unwrap();
    assert_eq!(a, expected.get("sound").unwrap());
}

#[test]
fn ring_mod_wins_over_a_later_mode_and_an_earlier_one() {
    same(
        &converted(r#"<sound ringMod="1" mode="fm"/>"#),
        r#"<sound mode="ringmod"/>"#,
    );
    same(
        &converted(r#"<sound mode="fm" ringMod="1"/>"#),
        r#"<sound mode="fm" mode="ringmod"/>"#,
    );
    same(
        &converted(r#"<sound ringMod="0" mode="fm"/>"#),
        r#"<sound mode="fm"/>"#,
    );
}

#[test]
fn polyphony_numbers_become_their_words() {
    same(
        &converted(r#"<sound polyphonic="0"/>"#),
        r#"<sound polyphonic="auto"/>"#,
    );
    same(
        &converted(r#"<sound polyphonic="1"/>"#),
        r#"<sound polyphonic="poly"/>"#,
    );
    same(
        &converted(r#"<sound polyphonic="2"/>"#),
        r#"<sound polyphonic="choke"/>"#,
    );
    same(
        &converted(r#"<sound polyphonic="legato"/>"#),
        r#"<sound polyphonic="legato"/>"#,
    );
}

#[test]
fn oscillator_reset_off_frees_every_oscillator_where_it_was_read() {
    same(
        &converted(r#"<sound oscillatorReset="0"><osc1 retrigPhase="5"/></sound>"#),
        r#"<sound><osc1 retrigPhase="-1"/><osc2 retrigPhase="-1"/><modulator1 retrigPhase="-1"/>
           <modulator2 retrigPhase="-1"/><osc1 retrigPhase="5"/></sound>"#,
    );
    same(&converted(r#"<sound oscillatorReset="1"/>"#), r#"<sound/>"#);
}

#[test]
fn a_param_read_after_default_params_wins_and_one_before_does_not() {
    same(
        &converted(
            r#"<sound><volume>0x10</volume><defaultParams volume="0x20" pan="0x1"/><pan>0x30</pan></sound>"#,
        ),
        r#"<sound><defaultParams volume="0x20" pan="0x30"/></sound>"#,
    );
    same(
        &converted(r#"<sound><volume>0x10</volume><defaultParams pan="0x1"/></sound>"#),
        r#"<sound><defaultParams pan="0x1" volume="0x10"/></sound>"#,
    );
    same(
        &converted(r#"<sound><waveFold>0x7</waveFold></sound>"#),
        r#"<sound><defaultParams waveFold="0x7"/></sound>"#,
    );
}

#[test]
fn a_current_element_keeps_its_own_entries_and_gives_up_its_old_param() {
    same(
        &converted(
            r#"<sound><defaultParams/><lfo1 type="saw" rate="0x9"/><modulator1 transpose="2" volume="0x8"/></sound>"#,
        ),
        r#"<sound><defaultParams lfo1Rate="0x9" modulator1Amount="0x8"/><lfo1 type="saw"/><modulator1 transpose="2"/></sound>"#,
    );
}

#[test]
fn a_sound_delay_feedback_or_rate_is_the_delay_feedback_the_later_one_kept() {
    same(
        &converted(
            r#"<sound><defaultParams/><delay rate="0x11" feedback="0x22" pingPong="1"/></sound>"#,
        ),
        r#"<sound><defaultParams delayFeedback="0x22"/><delay pingPong="1"/></sound>"#,
    );
    same(
        &converted(r#"<sound><defaultParams/><delay feedback="0x22" rate="0x11"/></sound>"#),
        r#"<sound><defaultParams delayFeedback="0x11"/><delay/></sound>"#,
    );
}

#[test]
fn an_old_filter_mode_and_fx_type_count_only_without_the_sound_own_keys() {
    same(
        &converted(r#"<sound><lpf mode="12dB"/><fx type="flanger"/></sound>"#),
        r#"<sound lpfMode="12dB" modFXType="flanger"/>"#,
    );
    same(
        &converted(r#"<sound lpfMode="SVF_Notch"><lpf mode="12dB"/></sound>"#),
        r#"<sound lpfMode="SVF_Notch"/>"#,
    );
}

#[test]
fn a_filter_switched_off_reads_as_fully_open() {
    same(
        &converted(r#"<sound><defaultParams/><lpf status="0" frequency="0x0D000000"/></sound>"#),
        r#"<sound><defaultParams lpfFrequency="0x7FFFFFD2"/></sound>"#,
    );
}

#[test]
fn old_fx_feedback_and_offset_are_worked_back_to_their_knobs() {
    // Stored decimal values, as the old firmware wrote them; hex reads as 0, as the firmware reads it.
    same(
        &converted(
            r#"<sound><defaultParams/><fx feedback="1073741824" offset="4194304"/></sound>"#,
        ),
        r#"<sound><defaultParams modFXFeedback="0xC75E540E" modFXOffset="0xC0000000"/></sound>"#,
    );
}
