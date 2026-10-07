//! The upgrader reads a file into the tree the firmware's parser builds, compares version stamps as the firmware does,
//! and writes the firmware's JSON.

use deluge_upgrade::tree::Node;
use deluge_upgrade::version::FirmwareVersion;

fn v(s: &str) -> FirmwareVersion {
    FirmwareVersion::parse(s)
}

#[test]
fn versions_order_as_the_firmware_orders_them() {
    // Every official version is older than every community one; an unparseable stamp is newest of all.
    assert!(v("4.1.4-alpha") < v("c1.0.0"));
    assert!(v("c1.2.1") < v("garbage"));
    // A pre-release is older than its release; pre-release tags compare as plain strings.
    assert!(v("2.1.0-beta") < v("2.1.0"));
    assert!(v("1.5.0-alpha") < v("1.5.0-pretest"));
    assert!(v("3.1.0-alpha") < v("3.1.0-alpha2"));
    assert!(v("2.0.0-beta") < v("2.1.0-beta"));
    assert!(v("c1.1.0") < v("c1.2.0"));
    // No stamp reads as the oldest official.
    assert_eq!(
        FirmwareVersion::OLDEST_OFFICIAL,
        FirmwareVersion::official(0, 0, 0)
    );
    assert!(FirmwareVersion::OLDEST_OFFICIAL < v("1.2.0"));
    // A number past 255, or a missing part, does not parse.
    assert_eq!(v("1.256.0"), FirmwareVersion::UNKNOWN);
    assert_eq!(v("4.1"), FirmwareVersion::UNKNOWN);
}

#[test]
fn a_document_is_stamped_by_its_root_attribute_or_a_sibling_element() {
    let sibling = Node::parse_xml(
        "<?xml version=\"1.0\"?><firmwareVersion>2.0.0-beta</firmwareVersion><sound mode=\"fm\"/>",
    )
    .unwrap();
    assert_eq!(FirmwareVersion::of_document(&sibling), v("2.0.0-beta"));
    let attribute = Node::parse_xml("<sound firmwareVersion=\"c1.2.0\" mode=\"fm\"/>").unwrap();
    assert_eq!(FirmwareVersion::of_document(&attribute), v("c1.2.0"));
    let none = Node::parse_xml("<sound mode=\"fm\"/>").unwrap();
    assert_eq!(
        FirmwareVersion::of_document(&none),
        FirmwareVersion::OLDEST_OFFICIAL
    );
}

#[test]
fn attributes_and_child_tags_are_the_same_entries() {
    let attrs =
        Node::parse_xml("<sound mode=\"fm\" polyphonic=\"poly\"><osc1 type=\"sine\"/></sound>")
            .unwrap();
    let tags = Node::parse_xml(
        "<sound><mode>fm</mode><polyphonic>poly</polyphonic><osc1><type>sine</type></osc1></sound>",
    )
    .unwrap();
    assert_eq!(attrs, tags);
}

#[test]
fn duplicate_keys_keep_their_order() {
    let doc = Node::parse_xml("<a><b x=\"1\"/><c/><b x=\"2\"/></a>").unwrap();
    let a = doc.get("a").unwrap();
    let keys: Vec<&str> = a.entries().iter().map(|(k, _)| k.as_str()).collect();
    assert_eq!(keys, ["b", "c", "b"]);
    assert_eq!(a.entries()[2].1.get("x").unwrap().text(), Some("2"));
}

#[test]
fn text_entities_comments_and_empty_elements_read_as_the_firmware_reads_them() {
    let doc = Node::parse_xml(
        "<!-- a comment --><s name=\"a &amp; b\"><!-- x --><clipName></clipName><n>5</n><e/></s>",
    )
    .unwrap();
    let s = doc.get("s").unwrap();
    assert_eq!(s.get("name").unwrap().text(), Some("a & b"));
    assert_eq!(s.get("clipName").unwrap(), &Node::Map(vec![])); // an empty element is an empty map
    assert_eq!(s.get("n").unwrap().text(), Some("5"));
    assert_eq!(s.get("e").unwrap(), &Node::Map(vec![]));
}

#[test]
fn malformed_xml_is_an_error_not_a_panic() {
    assert!(Node::parse_xml("<a><b></a>").is_err());
    assert!(Node::parse_xml("<a x=\"1></a>").is_err());
    assert!(Node::parse_xml("").is_err());
}

#[test]
fn json_writes_arrays_for_the_firmware_array_containers_and_numbers_bare() {
    let doc = Node::parse_xml(
        "<song x=\"5\" hex=\"0x10\" name=\"n\"><instruments><sound mode=\"fm\"/><kit/></instruments></song>",
    )
    .unwrap();
    let json = doc.to_json();
    let compact: String = json.split_whitespace().collect();
    assert_eq!(
        compact,
        r#"{"song":{"x":5,"hex":"0x10","name":"n","instruments":[{"sound":{"mode":"fm"}},{"kit":{}}]}}"#
    );
}

#[test]
fn json_escapes_text() {
    let doc = Node::parse_xml("<s name=\"a &quot;q&quot; \\ b\"/>").unwrap();
    let compact: String = doc.to_json().split_whitespace().collect();
    assert_eq!(
        compact,
        r#"{"s":{"name":"a"q"\\b"}}"#.replace("\"q\"", "\\\"q\\\"")
    );
}
