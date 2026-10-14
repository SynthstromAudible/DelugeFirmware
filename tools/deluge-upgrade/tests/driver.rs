//! The upgrader runs each conversion a file's version stamp calls for, and stamps what it writes as the firmware's
//! current version.

use deluge_upgrade::passes::Pass;
use deluge_upgrade::report::Report;
use deluge_upgrade::tree::Node;
use deluge_upgrade::upgrade::{Context, upgrade_with};
use deluge_upgrade::version::FirmwareVersion;
use std::path::Path;

fn mark(doc: &mut Node, _ctx: &mut Context) {
    if let Some(sound) = doc.get_mut("sound") {
        sound.set("marked", Node::Text("1".into()));
    }
}

fn complain(_doc: &mut Node, ctx: &mut Context) {
    ctx.note("test-pass", "nothing to convert".into());
}

const BEFORE_C120: &[Pass] = &[Pass {
    name: "mark",
    before: Some(FirmwareVersion::community(1, 2, 0)),
    run: mark,
}];

fn upgraded(xml: &str, passes: &[Pass], report: &mut Report) -> Node {
    let mut doc = Node::parse_xml(xml).unwrap();
    let mut ctx = Context::new(Path::new("test.XML"), None, report);
    upgrade_with(&mut doc, &mut ctx, passes);
    doc
}

#[test]
fn a_pass_runs_on_a_file_older_than_its_version_and_not_on_a_newer_one() {
    let mut report = Report::default();
    let old = upgraded(
        "<sound firmwareVersion=\"4.1.4-alpha\" mode=\"fm\"/>",
        BEFORE_C120,
        &mut report,
    );
    assert_eq!(
        old.get("sound").unwrap().get("marked").and_then(Node::text),
        Some("1")
    );
    let new = upgraded(
        "<sound firmwareVersion=\"c1.2.1\" mode=\"fm\"/>",
        BEFORE_C120,
        &mut report,
    );
    assert!(new.get("sound").unwrap().get("marked").is_none());
    let unstamped = upgraded("<sound mode=\"fm\"/>", BEFORE_C120, &mut report);
    assert_eq!(
        unstamped
            .get("sound")
            .unwrap()
            .get("marked")
            .and_then(Node::text),
        Some("1")
    );
}

#[test]
fn the_output_is_stamped_as_the_firmware_current_version_first_and_only_once() {
    let mut report = Report::default();
    let doc = upgraded(
        "<firmwareVersion>2.0.0</firmwareVersion><sound firmwareVersion=\"2.0.0\" mode=\"fm\"/>",
        &[],
        &mut report,
    );
    let (first_key, first_value) = &doc.entries()[0];
    assert_eq!(first_key, "firmwareVersion");
    assert_eq!(
        first_value.text(),
        Some(FirmwareVersion::current().stamp().as_str())
    );
    assert!(FirmwareVersion::current() > FirmwareVersion::community(1, 2, 1));
    let stamps = doc
        .entries()
        .iter()
        .filter(|(k, _)| k == "firmwareVersion")
        .count();
    assert_eq!(stamps, 1);
    assert!(doc.get("sound").unwrap().get("firmwareVersion").is_none());
    assert_eq!(
        FirmwareVersion::of_document(&doc),
        FirmwareVersion::current()
    );
}

#[test]
fn a_pass_with_no_version_runs_on_every_file_and_its_findings_are_reported() {
    let mut report = Report::default();
    let always = [Pass {
        name: "complain",
        before: None,
        run: complain,
    }];
    upgraded("<sound firmwareVersion=\"c1.3.0\"/>", &always, &mut report);
    assert_eq!(report.findings.len(), 1);
    assert_eq!(report.findings[0].file, "test.XML");
    assert_eq!(report.findings[0].conversion, "test-pass");
    assert!(report.to_json().contains("nothing to convert"));
}

#[test]
fn a_file_that_is_not_utf_8_keeps_its_bytes_and_is_reported() {
    let mut source = br#"<sound presetName="Caf"#.to_vec();
    source.push(0xE9);
    source.extend_from_slice(br#""/>"#);
    let mut report = Report::default();
    let mut ctx = Context::new(Path::new("test.XML"), None, &mut report);
    let written = deluge_upgrade::upgrade::upgrade_bytes(&source, &mut ctx).unwrap();
    let at = written.windows(3).position(|w| w == b"Caf").unwrap();
    assert_eq!(written[at + 3], 0xE9);
    assert_eq!(written[at + 4], b'"');
    assert_eq!(report.findings.len(), 1);
}
