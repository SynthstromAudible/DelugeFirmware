//! Patch cables and knobs in the forms firmware before 3.2 and 4.0 wrote: "range" cables and knobs, and the default
//! expression cables a sound from before 4.0 was given on load.

use super::sound::for_each_sound;
use crate::tree::Node;
use crate::upgrade::Context;

/// The most cables a set holds (`kMaxNumPatchCables`).
const MAX_CABLES: usize = 32;

fn text(value: &str) -> Node {
    Node::Text(value.to_string())
}

/// Every `patchCables` set in a document.
fn for_each_cable_set(node: &mut Node, f: &mut dyn FnMut(&mut Node)) {
    if let Some(entries) = node.entries_mut() {
        for (key, child) in entries.iter_mut() {
            if key == "patchCables" && matches!(child, Node::Map(_)) {
                f(child);
            }
            for_each_cable_set(child, f);
        }
    }
}

/// Whether a cable's amount holds anything: a value other than zero, or automation.
fn holds_something(amount: Option<&Node>) -> bool {
    amount.and_then(Node::text).is_some_and(|text| {
        let text = text.trim();
        let value = text
            .strip_prefix("0x")
            .or_else(|| text.strip_prefix("0X"))
            .unwrap_or(text);
        value.len() > 8 || value.trim_start_matches('0').chars().any(|c| c != '0')
    })
}

/// A cable firmware before 3.2 marked as the one the "range" cables adjust.
fn adjustable(cable: &Node) -> bool {
    cable
        .get("rangeAdjustable")
        .and_then(Node::int)
        .unwrap_or(0)
        != 0
        && cable.get("source").is_some()
        && cable.get("destination").is_some()
        && holds_something(cable.get("amount"))
}

/// Pre-3.2 "range" cables: each controls the depth of the last cable marked `rangeAdjustable`, which is how the
/// firmware reads them; with no such cable each is kept going nowhere (`destination="none"`), as the firmware saves it.
/// A moved cable keeps the polarity it read with: a depth controller's default (bipolar) is not an aftertouch cable's.
fn range_cables(set: &mut Node) {
    let entries = set.entries_mut().expect("a cable set is a map");
    let is_range = |cable: &Node| cable.get("destination").and_then(Node::text) == Some("range");
    let adjusted = entries
        .iter()
        .rposition(|(key, cable)| key == "patchCable" && adjustable(cable));
    let mut moved = Vec::new();
    let mut kept = Vec::new();
    for (index, (key, mut cable)) in std::mem::take(entries).into_iter().enumerate() {
        if key == "patchCable" && is_range(&cable) {
            cable.remove("rangeAdjustable");
            if adjusted.is_none() {
                cable.set("destination", text("none"));
                kept.push((key, cable));
                continue;
            }
            cable.remove("destination");
            if cable.get("polarity").is_none() {
                let aftertouch = cable.get("source").and_then(Node::text) == Some("aftertouch");
                cable.set(
                    "polarity",
                    text(if aftertouch { "unipolar" } else { "bipolar" }),
                );
            }
            moved.push((index, cable));
            continue;
        }
        cable.remove("rangeAdjustable");
        kept.push((key, cable));
    }
    if let Some(adjusted) = adjusted {
        let at = adjusted - moved.iter().filter(|(index, _)| *index < adjusted).count();
        let outer = &mut kept[at].1;
        if outer.get("depthControlledBy").is_none() {
            outer.set("depthControlledBy", Node::Map(vec![]));
        }
        let depth = outer.get_mut("depthControlledBy").expect("just made");
        for (_, cable) in moved {
            depth
                .entries_mut()
                .expect("a map")
                .push(("patchCable".into(), cable));
        }
    }
    *entries = kept;
}

/// A knob's assignment before any file sets it (`Sound`'s constructor), by its place in `modKnobs`.
const DEFAULT_KNOBS: [(&str, Option<&str>); 16] = [
    ("pan", None),
    ("volumePostFX", None),
    ("lpfResonance", None),
    ("lpfFrequency", None),
    ("env1Release", None),
    ("env1Attack", None),
    ("delayFeedback", None),
    ("delayRate", None),
    ("reverbAmount", None),
    ("volumePostReverbSend", Some("compressor")),
    ("pitch", Some("lfo1")),
    ("lfo1Rate", None),
    ("portamento", None),
    ("stutterRate", None),
    ("bitcrushAmount", None),
    ("sampleRateReduction", None),
];

/// A pre-3.2 "range" knob: the firmware discards it, leaving its knob as it starts. A "range" MIDI knob is dropped.
fn range_knobs(sound: &mut Node) {
    if let Some(knobs) = sound.get_mut("modKnobs").and_then(Node::entries_mut) {
        for (index, (_, knob)) in knobs.iter_mut().enumerate() {
            if knob.get("controlsParam").and_then(Node::text) == Some("range")
                && let Some((param, source)) = DEFAULT_KNOBS.get(index)
            {
                let mut entries = vec![("controlsParam".to_string(), text(param))];
                if let Some(source) = source {
                    entries.push(("patchAmountFromSource".into(), text(source)));
                }
                *knob = Node::Map(entries);
            }
        }
    }
}

fn drop_range_midi_knobs(node: &mut Node) {
    if let Some(entries) = node.entries_mut() {
        for (key, child) in entries.iter_mut() {
            if key == "midiKnobs"
                && let Some(knobs) = child.entries_mut()
            {
                knobs.retain(|(_, knob)| {
                    knob.get("controlsParam").and_then(Node::text) != Some("range")
                });
            }
            drop_range_midi_knobs(child);
        }
    }
}

/// Every cable and knob in its pre-3.2 "range" form: read so by every firmware.
pub fn range_forms(doc: &mut Node, _ctx: &mut Context) {
    for_each_cable_set(doc, &mut range_cables);
    for_each_sound(doc, &mut range_knobs);
    drop_range_midi_knobs(doc);
}

fn count_cables(set: &Node) -> usize {
    set.entries()
        .iter()
        .map(|(key, cable)| {
            usize::from(key == "patchCable")
                + cable.get("depthControlledBy").map_or(0, count_cables)
        })
        .sum()
}

/// A sound from before official 4.0 was given aftertouch to volume and Y to its filter (to its first modulator's
/// volume, in FM) on load, unless a cable of its own already took aftertouch, X or Y, and while its set had room.
pub fn default_expression_cables(doc: &mut Node, _ctx: &mut Context) {
    for_each_sound(doc, &mut |sound| {
        let fm = sound.get("mode").and_then(Node::text) == Some("fm");
        let Some(defaults) = sound.get_mut("defaultParams") else {
            return;
        };
        if defaults.get("patchCables").is_none() {
            defaults.set("patchCables", Node::Map(vec![]));
        }
        let set = defaults.get_mut("patchCables").expect("just made");
        let expressive = set.entries().iter().any(|(key, cable)| {
            key == "patchCable"
                && matches!(
                    cable.get("source").and_then(Node::text),
                    Some("aftertouch" | "x" | "y")
                )
        });
        if expressive {
            return;
        }
        let (y_destination, y_amount) = if fm {
            ("modulator1Volume", "0x1333332C")
        } else {
            ("lpfFrequency", "0x19999990")
        };
        for (source, destination, amount, polarity) in [
            ("aftertouch", "volume", "0x2A3D7094", "unipolar"),
            ("y", y_destination, y_amount, "bipolar"),
        ] {
            if count_cables(set) >= MAX_CABLES {
                return;
            }
            let cable = Node::Map(vec![
                ("source".into(), text(source)),
                ("destination".into(), text(destination)),
                ("polarity".into(), text(polarity)),
                ("amount".into(), text(amount)),
            ]);
            set.entries_mut()
                .expect("a map")
                .push(("patchCable".into(), cable));
        }
    });
}
