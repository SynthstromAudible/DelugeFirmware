//! Sample zones in milliseconds and sample playback, as official firmware before 2.1 read them.

use deluge_upgrade::passes::samples::{linear_interpolation, millisecond_zones};
use deluge_upgrade::passes::sound::old_sound_forms;
use deluge_upgrade::report::Report;
use deluge_upgrade::tree::Node;
use deluge_upgrade::upgrade::Context;
use deluge_upgrade::version::FirmwareVersion;
use std::path::{Path, PathBuf};

/// A card of its own for test `name`, holding SAMPLES/A.WAV: 4096 frames of 16-bit mono at 44.1 kHz. (Tests run in
/// parallel; one card each keeps a test from reading another's half-written file.)
fn card(name: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!(
        "deluge-upgrade-samples-{}-{name}",
        std::process::id()
    ));
    std::fs::create_dir_all(dir.join("SAMPLES")).unwrap();
    let frames: u32 = 4096;
    let data = frames * 2;
    let mut wav = Vec::new();
    wav.extend_from_slice(b"RIFF");
    wav.extend_from_slice(&(36 + data).to_le_bytes());
    wav.extend_from_slice(b"WAVEfmt ");
    wav.extend_from_slice(&16u32.to_le_bytes());
    wav.extend_from_slice(&1u16.to_le_bytes()); // PCM
    wav.extend_from_slice(&1u16.to_le_bytes()); // mono
    wav.extend_from_slice(&44_100u32.to_le_bytes());
    wav.extend_from_slice(&88_200u32.to_le_bytes());
    wav.extend_from_slice(&2u16.to_le_bytes());
    wav.extend_from_slice(&16u16.to_le_bytes());
    wav.extend_from_slice(b"data");
    wav.extend_from_slice(&data.to_le_bytes());
    wav.resize(wav.len() + data as usize, 0);
    std::fs::write(dir.join("SAMPLES").join("A.WAV"), wav).unwrap();
    dir
}

fn zone(osc: &str, version: &str, card: Option<&Path>) -> (Node, Report) {
    let mut doc = Node::parse_xml(&format!(
        r#"<sound firmwareVersion="{version}"><osc1 type="sample" fileName="samples/a.wav" {osc}</osc1></sound>"#
    ))
    .unwrap();
    let mut report = Report::default();
    let mut ctx = Context::new(Path::new("t.XML"), card, &mut report);
    ctx.version = FirmwareVersion::of_document(&doc);
    millisecond_zones(&mut doc, &mut ctx);
    let zone = doc
        .get("sound")
        .unwrap()
        .get("osc1")
        .unwrap()
        .get("zone")
        .unwrap()
        .clone();
    (zone, report)
}

fn positions(zone: &Node) -> (Option<&str>, Option<&str>) {
    (
        zone.get("startSamplePos").and_then(Node::text),
        zone.get("endSamplePos").and_then(Node::text),
    )
}

#[test]
fn a_zone_in_milliseconds_becomes_positions_at_the_sample_rate() {
    let card = card("positions");
    let (z, _) = zone(
        r#">"<zone startMilliseconds="20" endMilliseconds="80"/>"#,
        "2.0.0",
        Some(&card),
    );
    assert_eq!(positions(&z), (Some("882"), Some("3528")));
    assert!(z.get("startMilliseconds").is_none());
    let (z, _) = zone(
        r#">"<zone startSeconds="0" endSeconds="0" endMilliseconds="50"/>"#,
        "2.0.0",
        Some(&card),
    );
    assert_eq!(positions(&z), (Some("0"), Some("2205")));
}

#[test]
fn a_reversed_source_swaps_its_converted_zone() {
    let card = card("reversed");
    let (z, _) = zone(
        r#"reversed="1"><zone startMilliseconds="10" endMilliseconds="60"/>"#,
        "2.0.0",
        Some(&card),
    );
    assert_eq!(positions(&z), (Some("1450"), Some("3655")));
}

#[test]
fn an_early_2016_sounds_reversed_zone_is_converted_on_its_one_source() {
    let card = card("early");
    let mut doc = Node::parse_xml(
        r#"<sound><fileName>samples/a.wav</fileName><reversed>1</reversed><zone startMilliseconds="10" endMilliseconds="60"/></sound>"#,
    )
    .unwrap();
    let mut report = Report::default();
    let mut ctx = Context::new(Path::new("t.XML"), Some(&card), &mut report);
    ctx.version = FirmwareVersion::of_document(&doc);
    old_sound_forms(&mut doc, &mut ctx);
    millisecond_zones(&mut doc, &mut ctx);
    let sound = doc.get("sound").unwrap();
    assert_eq!(
        sound.entries().iter().filter(|(k, _)| k == "osc1").count(),
        1
    );
    let osc1 = sound.get("osc1").unwrap();
    assert_eq!(osc1.get("reversed").and_then(Node::text), Some("1"));
    assert_eq!(
        positions(osc1.get("zone").unwrap()),
        (Some("1450"), Some("3655"))
    );
}

#[test]
fn a_kit_drum_tuned_only_at_its_sample_oscillator_keeps_its_interpolation() {
    // Official firmware folded a sample source's transpose and cents into its ranges before the clips looked.
    let mut doc = Node::parse_xml(
        r#"<song><instruments><kit><soundSources><sound><osc1 type="sample" transpose="3" cents="5"/></sound></soundSources></kit></instruments></song>"#,
    )
    .unwrap();
    let mut report = Report::default();
    linear_interpolation(
        &mut doc,
        &mut Context::new(Path::new("t.XML"), None, &mut report),
    );
    let drum = &doc
        .get("song")
        .unwrap()
        .get("instruments")
        .unwrap()
        .entries()[0]
        .1
        .get("soundSources")
        .unwrap()
        .entries()[0]
        .1;
    assert!(
        drum.get("osc1")
            .unwrap()
            .get("linearInterpolation")
            .is_none()
    );
}

#[test]
fn an_end_just_past_the_sample_is_its_end_and_one_far_past_is_reported() {
    let card = card("overshoot");
    let (z, report) = zone(r#">"<zone endMilliseconds="93"/>"#, "2.0.0", Some(&card));
    assert_eq!(positions(&z).1, Some("4096"));
    assert!(report.findings.is_empty());
    let (z, report) = zone(r#">"<zone endMilliseconds="100"/>"#, "2.0.0", Some(&card));
    assert_eq!(positions(&z).1, Some("4410"));
    assert_eq!(report.findings.len(), 1);
}

#[test]
fn without_its_sample_a_zone_stays_in_milliseconds_and_is_reported() {
    let (z, report) = zone(r#">"<zone startMilliseconds="20"/>"#, "2.0.0", None);
    assert!(z.get("startMilliseconds").is_some());
    assert_eq!(report.findings.len(), 1);
}

#[test]
fn from_2_1_milliseconds_are_dropped_unread() {
    let (z, report) = zone(
        r#">"<zone startMilliseconds="20" startSamplePos="7"/>"#,
        "2.1.0",
        None,
    );
    assert!(z.get("startMilliseconds").is_none());
    assert_eq!(z.get("startSamplePos").and_then(Node::text), Some("7"));
    assert!(report.findings.is_empty());
}

#[test]
fn a_song_synth_plays_its_samples_with_linear_interpolation_and_a_kit_drum_when_tuned() {
    let mut doc = Node::parse_xml(
        r#"<song><instruments><sound><osc1 type="sample"/><osc2 type="saw"/></sound>
           <kit><soundSources><sound transpose="2"><osc1 type="sample"/></sound></soundSources></kit></instruments></song>"#,
    )
    .unwrap();
    let mut report = Report::default();
    linear_interpolation(
        &mut doc,
        &mut Context::new(Path::new("t.XML"), None, &mut report),
    );
    let instruments = doc.get("song").unwrap().get("instruments").unwrap();
    let synth = &instruments.entries()[0].1;
    assert_eq!(
        synth
            .get("osc1")
            .unwrap()
            .get("linearInterpolation")
            .and_then(Node::text),
        Some("1")
    );
    assert!(
        synth
            .get("osc2")
            .unwrap()
            .get("linearInterpolation")
            .is_none()
    );
    let drum = &instruments.entries()[1]
        .1
        .get("soundSources")
        .unwrap()
        .entries()[0]
        .1;
    assert_eq!(
        drum.get("osc1")
            .unwrap()
            .get("linearInterpolation")
            .and_then(Node::text),
        Some("1")
    );
}

#[test]
fn a_zone_with_no_sample_file_has_nothing_to_convert() {
    let mut doc = Node::parse_xml(
        r#"<sound firmwareVersion="2.0.0"><osc1 type="sample"><zone startMilliseconds="20"/></osc1></sound>"#,
    )
    .unwrap();
    let mut report = Report::default();
    let mut ctx = Context::new(Path::new("t.XML"), None, &mut report);
    ctx.version = FirmwareVersion::of_document(&doc);
    millisecond_zones(&mut doc, &mut ctx);
    let zone = doc
        .get("sound")
        .unwrap()
        .get("osc1")
        .unwrap()
        .get("zone")
        .unwrap();
    assert!(zone.get("startMilliseconds").is_none());
    assert!(report.findings.is_empty());
}
