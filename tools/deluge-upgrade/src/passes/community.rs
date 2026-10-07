//! Forms the community firmware still reads from official and early community files: an FM sound's filter words, the
//! old arpeggiator mode, the old note-data encodings and the song compressor's old tag. The upgrader converts them all,
//! so that what it writes needs none of them.

use super::sound::for_each_sound;
use crate::tree::Node;
use crate::upgrade::Context;

fn text(value: impl ToString) -> Node {
    Node::Text(value.to_string())
}

/// An FM sound saved before community 1.2 may hold filter modes it never rendered: it reads them as off.
pub fn fm_filters_off(doc: &mut Node, _ctx: &mut Context) {
    for_each_sound(doc, &mut |sound| {
        if sound.get("mode").and_then(Node::text) == Some("fm") {
            sound.set("lpfMode", text("Off"));
            sound.set("hpfMode", text("Off"));
        }
    });
}

/// Every `arpeggiator` element: a sound's, a kit's, a clip's.
fn for_each_arpeggiator(node: &mut Node, f: &mut dyn FnMut(&mut Node)) {
    if let Some(entries) = node.entries_mut() {
        for (key, child) in entries.iter_mut() {
            if key == "arpeggiator" && matches!(child, Node::Map(_)) {
                f(child);
            }
            for_each_arpeggiator(child, f);
        }
    }
}

/// The old arpeggiator `mode` (up, down, both, random; anything else off), read before community 1.1 into the split
/// settings -- but only while they were still at their defaults (off, up, up) where it stood.
pub fn old_arpeggiator_mode(doc: &mut Node, _ctx: &mut Context) {
    for_each_arpeggiator(doc, &mut |arp| {
        let entries = arp.entries_mut().expect("a map");
        let mut converted = Vec::with_capacity(entries.len());
        let (mut arp_mode, mut note_mode, mut octave_mode) =
            ("off".to_string(), "up".to_string(), "up".to_string());
        for (key, value) in std::mem::take(entries) {
            match key.as_str() {
                "mode" => {
                    if arp_mode == "off" && note_mode == "up" && octave_mode == "up" {
                        let old = value.text().unwrap_or("");
                        let (a, n, o) = match old {
                            "up" => ("arp", "up", "up"),
                            "down" => ("arp", "down", "down"),
                            "both" => ("arp", "up", "alt"),
                            "random" => ("arp", "random", "random"),
                            _ => ("off", "up", "up"),
                        };
                        (arp_mode, note_mode, octave_mode) = (a.into(), n.into(), o.into());
                        converted.push(("arpMode".to_string(), text(a)));
                        converted.push(("noteMode".to_string(), text(n)));
                        converted.push(("octaveMode".to_string(), text(o)));
                    }
                    continue;
                }
                "arpMode" => arp_mode = value.text().unwrap_or("").to_string(),
                "noteMode" => note_mode = value.text().unwrap_or("").to_string(),
                "octaveMode" => octave_mode = value.text().unwrap_or("").to_string(),
                _ => {}
            }
            converted.push((key, value));
        }
        *entries = converted;
    });
}

/// The first official and early community iterance presets, by index from 1 (`iterancePresets`): divisor, steps.
const ITERANCE_PRESETS: [(u8, u8); 37] = [
    (0, 1),
    (0, 2),
    (2, 0b1),
    (2, 0b10),
    (3, 0b1),
    (3, 0b10),
    (3, 0b100),
    (4, 0b1),
    (4, 0b10),
    (4, 0b100),
    (4, 0b1000),
    (5, 0b1),
    (5, 0b10),
    (5, 0b100),
    (5, 0b1000),
    (5, 0b10000),
    (6, 0b1),
    (6, 0b10),
    (6, 0b100),
    (6, 0b1000),
    (6, 0b10000),
    (6, 0b100000),
    (7, 0b1),
    (7, 0b10),
    (7, 0b100),
    (7, 0b1000),
    (7, 0b10000),
    (7, 0b100000),
    (7, 0b1000000),
    (8, 0b1),
    (8, 0b10),
    (8, 0b100),
    (8, 0b1000),
    (8, 0b10000),
    (8, 0b100000),
    (8, 0b1000000),
    (8, 0b10000000),
];

const PROBABILITY_VALUES: u8 = 20;
const FILL: u8 = 2;
const NOT_FILL: u8 = 1;
const DEFAULT_LIFT: u8 = 64;

/// One note's old probability byte: a probability, or (as 0 and 128) fill and not-fill, or (just above the
/// probabilities) an iterance preset. Gives (probability, iterance, fill).
fn old_probability(byte: u8) -> (u8, u16, u8) {
    match byte {
        0 => (PROBABILITY_VALUES, 0, FILL),
        128 => (PROBABILITY_VALUES, 0, NOT_FILL),
        b if b > PROBABILITY_VALUES
            && usize::from(b) <= usize::from(PROBABILITY_VALUES) + ITERANCE_PRESETS.len() =>
        {
            let (divisor, steps) = ITERANCE_PRESETS[usize::from(b - PROBABILITY_VALUES - 1)];
            (
                PROBABILITY_VALUES,
                u16::from(divisor) << 8 | u16::from(steps),
                0,
            )
        }
        b => (b, 0, 0),
    }
}

/// Rewrites old note data (`noteData`, 20 hex digits a note; `noteDataWithLift`, 22) as `noteDataWithSplitProb` (28:
/// position 8, length 8, velocity 2, lift 2, probability 2, iterance 4, fill 2).
fn split_probability(hex: &str, width: usize) -> Option<String> {
    let digits = hex.strip_prefix("0x")?;
    let mut out = String::from("0x");
    for note in digits.as_bytes().chunks(width).filter(|n| n.len() == width) {
        let note = std::str::from_utf8(note).ok()?;
        let byte = |at: usize| u8::from_str_radix(&note[at..at + 2], 16).ok();
        let (lift, probability_at) = if width == 22 {
            let lift = byte(18)?;
            (
                if lift == 0 || lift > 127 {
                    DEFAULT_LIFT
                } else {
                    lift
                },
                20,
            )
        } else {
            (DEFAULT_LIFT, 18)
        };
        let (probability, iterance, fill) = old_probability(byte(probability_at)?);
        out.push_str(&format!(
            "{}{lift:02X}{probability:02X}{iterance:04X}{fill:02X}",
            &note[..18]
        ));
    }
    Some(out)
}

/// Every `noteRow` element.
fn for_each_note_row(node: &mut Node, f: &mut dyn FnMut(&mut Node)) {
    if let Some(entries) = node.entries_mut() {
        for (key, child) in entries.iter_mut() {
            if key == "noteRow" && matches!(child, Node::Map(_)) {
                f(child);
            }
            for_each_note_row(child, f);
        }
    }
}

/// A row's notes as the firmware chose among its encodings: plain `noteData` always; else, before community 1.3,
/// `noteDataWithLift`; else `noteDataWithSplitProb`. The chosen one is rewritten as split probability.
pub fn old_note_data(doc: &mut Node, ctx: &mut Context) {
    let before_1_3 = ctx.version < crate::version::FirmwareVersion::community(1, 3, 0);
    for_each_note_row(doc, &mut |row| {
        let chosen = if let Some(plain) = row.get("noteData").and_then(Node::text) {
            Some((plain.to_string(), 20))
        } else if before_1_3 && let Some(lift) = row.get("noteDataWithLift").and_then(Node::text) {
            Some((lift.to_string(), 22))
        } else {
            None
        };
        let Some((hex, width)) = chosen else {
            // Not read: an encoding the firmware passed over.
            if !before_1_3 {
                row.remove("noteDataWithLift");
            }
            return;
        };
        if let Some(converted) = split_probability(&hex, width) {
            row.remove("noteData");
            row.remove("noteDataWithLift");
            row.remove("noteDataWithIteranceAndFill");
            row.set("noteDataWithSplitProb", text(converted));
        }
    });
}

/// A song's compressor under its old tag: read as the song's `audioCompressor` where it stands (its five settings,
/// no blend), so that whichever of the two comes later still wins each setting.
pub fn song_compressor(doc: &mut Node, _ctx: &mut Context) {
    let Some(entries) = doc.get_mut("song").and_then(Node::entries_mut) else {
        return;
    };
    for (key, value) in entries.iter_mut() {
        if key != "songCompressor" {
            continue;
        }
        let mut compressor = Node::Map(vec![]);
        for setting in ["attack", "release", "thresh", "ratio", "compHPF"] {
            if let Some(v) = value.get(setting) {
                compressor.set(setting, v.clone());
            }
        }
        *key = "audioCompressor".into();
        *value = compressor;
    }
}

/// A MIDI clip's mod wheel (CC 1) automation, read before community 1.1 as its Y expression (`instrument_clip.cpp`):
/// moved into the clip's `expressionData`, which wins only where the file read it after the CC.
pub fn mod_wheel_to_y(doc: &mut Node, _ctx: &mut Context) {
    fn walk(node: &mut Node) {
        let Some(entries) = node.entries_mut() else {
            return;
        };
        for (key, child) in entries.iter_mut() {
            if key == "instrumentClip" {
                convert(child);
            }
            walk(child);
        }
    }
    fn convert(clip: &mut Node) {
        let Some(entries) = clip.entries_mut() else {
            return;
        };
        let Some(params_at) = entries.iter().position(|(k, _)| k == "midiParams") else {
            return;
        };
        let Some(params) = entries[params_at].1.entries_mut() else {
            return;
        };
        let Some(at) = params.iter().position(|(k, p)| {
            k == "param" && p.get("cc").and_then(Node::text).map(str::trim) == Some("1")
        }) else {
            return;
        };
        let Some(value) = params.remove(at).1.get("value").cloned() else {
            return;
        };
        let expression_at = entries.iter().position(|(k, _)| k == "expressionData");
        let read_later = expression_at.is_some_and(|e| e > params_at);
        let expression = match expression_at {
            Some(e) => &mut entries[e].1,
            None => {
                entries.push(("expressionData".into(), Node::Map(vec![])));
                &mut entries.last_mut().expect("just pushed").1
            }
        };
        if !(read_later && expression.get("yExpression").is_some()) {
            expression.set("yExpression", value);
        }
    }
    walk(doc);
}
