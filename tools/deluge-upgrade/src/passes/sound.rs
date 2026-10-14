//! A sound's forms from before the official firmware's 2016 format, and its numbers for polyphony: each rewritten as
//! the entries the firmware's reader made of it.
//!
//! The old reader read params kept at the sound's top level into the sound's params, in document order with its
//! `defaultParams`: whichever came later won. A converted param goes into `defaultParams` so that the same one wins.

use crate::tree::Node;
use crate::upgrade::Context;

/// Every `sound` element of a document: a preset's root, a kit's rows, a song's synths.
pub fn for_each_sound(node: &mut Node, f: &mut dyn FnMut(&mut Node)) {
    if let Some(entries) = node.entries_mut() {
        for (key, child) in entries.iter_mut() {
            if key == "sound" && matches!(child, Node::Map(_)) {
                f(child);
            }
            for_each_sound(child, f);
        }
    }
}

/// A text node's integer as the firmware's `readTagOrAttributeValueInt` reads it: decimal only, up to the first other
/// character, 0 when there is none or it overflows.
fn decimal(node: &Node) -> i32 {
    let text = node.text().unwrap_or("");
    let (negative, digits) = match text.strip_prefix('-') {
        Some(rest) => (true, rest),
        None => (false, text),
    };
    let digits: String = digits.chars().take_while(char::is_ascii_digit).collect();
    if digits.is_empty() {
        return 0;
    }
    format!("{}{digits}", if negative { "-" } else { "" })
        .parse()
        .unwrap_or(0)
}

fn text(value: impl ToString) -> Node {
    Node::Text(value.to_string())
}

fn hex(value: i32) -> Node {
    Node::Text(format!("0x{:08X}", value as u32))
}

/// One param an old form sets, and where in the sound it was read.
struct Param {
    at: usize,
    group: Option<&'static str>,
    name: String,
    value: Node,
}

/// The params, transposes and settings one sound's old forms make.
#[derive(Default)]
struct Converted {
    params: Vec<Param>,
    /// Entries to put where the old form was: (position, key, value).
    entries: Vec<(usize, String, Node)>,
}

/// The params a sound's old top-level tags hold: `defaultParams` reads each under the same name
/// (`param_serialization.cpp`'s table).
const TOP_LEVEL_PARAMS: &[&str] = &[
    "noiseVolume",
    "portamento",
    "oscAPitchAdjust",
    "oscBPitchAdjust",
    "mod1PitchAdjust",
    "mod2PitchAdjust",
    "volume",
    "pan",
    "pitchAdjust",
    "reverbAmount",
    "waveFold",
];

/// The value a user setting of 0..50 stood for, as `getParamFromUserValue` makes it for most params.
fn user_value(user: u32) -> i32 {
    user.wrapping_mul(85_899_345).wrapping_sub(2_147_483_648) as i32
}

/// The old feedback form: the firmware stored the fed-back amount, and the reader works the knob back from it, its
/// `pow` and the `1 -` in single precision and the rest in double, as `sound.cpp` computes it.
fn mod_fx_feedback(stored: i32) -> i32 {
    let fraction = 1.0_f32 - stored as f32 / 2_147_483_648.0_f32;
    let knob = 1.0_f32 - fraction.powf(1.0_f32 / 3.0_f32);
    (f64::from(knob) / 0.74 * 4_294_967_296.0 - 2_147_483_648.0) as i32
}

/// The old offset form: the stored offset, scaled up.
fn mod_fx_offset(stored: i32) -> i32 {
    ((i64::from(stored) << 8) - 2_147_483_648) as i32
}

/// Reads one sound's old forms into `converted`, and removes them (or their old children) from the sound.
fn collect(sound: &mut Node, converted: &mut Converted) {
    let entries = sound.entries_mut().expect("a sound is a map");
    let mut kept: Vec<(String, Node)> = Vec::with_capacity(entries.len());
    // The transposes as the reader holds them at each point: sources set, modulators add.
    let mut source_transpose = [0i64; 2];
    let mut ring_mod = false;
    for (key, mut value) in std::mem::take(entries) {
        let at = kept.len();
        let mut param = |name: &str, value: Node, group: Option<&'static str>| {
            converted.params.push(Param {
                at,
                group,
                name: name.to_string(),
                value,
            });
        };
        match key.as_str() {
            "mode" if ring_mod => continue,
            "ringMod" => {
                if decimal(&value) == 1 {
                    ring_mod = true;
                    converted.entries.push((at, "mode".into(), text("ringmod")));
                }
                continue;
            }
            "oscillatorA" | "oscillatorB" => {
                let s = usize::from(key == "oscillatorB");
                let osc = if s == 0 { "osc1" } else { "osc2" };
                let mut source = Node::Map(vec![]);
                for (k, v) in value.entries() {
                    match k.as_str() {
                        "type" => source.set("type", v.clone()),
                        "volume" => param(
                            if s == 0 { "oscAVolume" } else { "oscBVolume" },
                            v.clone(),
                            None,
                        ),
                        "phaseWidth" => param(
                            if s == 0 {
                                "oscAPulseWidth"
                            } else {
                                "oscBPulseWidth"
                            },
                            v.clone(),
                            None,
                        ),
                        "note" if s == 0 => {
                            let offset = i64::from(decimal(v).clamp(1, 127) - 60);
                            for (i, t) in source_transpose.iter_mut().enumerate() {
                                *t += offset;
                                let name = if i == 0 { "osc1" } else { "osc2" };
                                let element = Node::Map(vec![("transpose".into(), text(*t))]);
                                converted.entries.push((at, name.into(), element));
                                let modulator = Node::Map(vec![("transpose".into(), text(offset))]);
                                let m = if i == 0 { "modulator1" } else { "modulator2" };
                                converted.entries.push((at, m.into(), modulator));
                            }
                        }
                        "transpose" if s == 1 => {
                            source_transpose[1] += i64::from(decimal(v));
                            source.set("transpose", text(source_transpose[1]));
                        }
                        "cents" if s == 1 => source.set("cents", text(decimal(v))),
                        _ => {}
                    }
                }
                if !source.entries().is_empty() {
                    converted.entries.push((at, osc.into(), source));
                }
                continue;
            }
            "osc1" | "osc2" => {
                if let Some(t) = value.get("transpose") {
                    source_transpose[usize::from(key == "osc2")] = i64::from(decimal(t));
                }
            }
            "modulator1" | "modulator2" => {
                if let Some(volume) = value.remove("volume") {
                    param(
                        if key == "modulator1" {
                            "modulator1Amount"
                        } else {
                            "modulator2Amount"
                        },
                        volume,
                        None,
                    );
                }
            }
            "lfo1" | "lfo2" | "lfo3" | "lfo4" => {
                if let Some(rate) = value.remove("rate") {
                    let name = match key.as_str() {
                        "lfo1" => "lfo1Rate",
                        "lfo2" => "lfo2Rate",
                        "lfo3" => "lfo3Rate",
                        _ => "lfo4Rate",
                    };
                    param(name, rate, None);
                }
            }
            "delay" => {
                // Official firmware read both of these into the delay's feedback: the later one is what it kept.
                let olds: Vec<Node> = value
                    .entries()
                    .iter()
                    .filter(|(k, _)| k == "feedback" || k == "rate")
                    .map(|(_, v)| v.clone())
                    .collect();
                if let Some(last) = olds.into_iter().last() {
                    value.remove("feedback");
                    value.remove("rate");
                    value.remove("feedback");
                    value.remove("rate");
                    param("delayFeedback", last, None);
                }
            }
            "fx" => {
                for (k, v) in value.entries() {
                    match k.as_str() {
                        "rate" => param("modFXRate", v.clone(), None),
                        "depth" => param("modFXDepth", v.clone(), None),
                        "feedback" => {
                            param("modFXFeedback", hex(mod_fx_feedback(decimal(v))), None)
                        }
                        "offset" => param("modFXOffset", hex(mod_fx_offset(decimal(v))), None),
                        "type" => converted.entries.push((at, "fxType".into(), v.clone())),
                        _ => {}
                    }
                }
                continue;
            }
            "lpf" | "hpf" => {
                let prefix = key.clone();
                let mut switched_on = true;
                for (k, v) in value.entries() {
                    match k.as_str() {
                        "status" => switched_on = decimal(v).clamp(0, 1) == 1,
                        "frequency" => param(&format!("{prefix}Frequency"), v.clone(), None),
                        "resonance" => param(&format!("{prefix}Resonance"), v.clone(), None),
                        "morph" => param(&format!("{prefix}Morph"), v.clone(), None),
                        "mode" if prefix == "lpf" => {
                            converted.entries.push((at, "lpfModeOld".into(), v.clone()))
                        }
                        _ => {}
                    }
                }
                if !switched_on {
                    param(&format!("{prefix}Frequency"), hex(user_value(50)), None);
                }
                continue;
            }
            "envelope1" | "envelope2" | "envelope3" | "envelope4" => {
                let group: &'static str = match key.as_str() {
                    "envelope1" => "envelope1",
                    "envelope2" => "envelope2",
                    "envelope3" => "envelope3",
                    _ => "envelope4",
                };
                for (k, v) in value.entries() {
                    param(k, v.clone(), Some(group));
                }
                continue;
            }
            "fileName" => {
                // An early-2016 sound: its one sample, and the params such a sound started with.
                converted.entries.push((
                    at,
                    "osc1".into(),
                    Node::Map(vec![
                        ("type".into(), text("sample")),
                        ("fileName".into(), value.clone()),
                    ]),
                ));
                for (name, user) in [
                    ("attack", 0),
                    ("decay", 20),
                    ("sustain", 50),
                    ("release", 0),
                ] {
                    param(name, hex(user_value(user)), Some("envelope1"));
                }
                param("oscAVolume", hex(user_value(50)), None);
                param("oscBVolume", hex(user_value(0)), None);
                continue;
            }
            "continuous" => {
                converted.entries.push((
                    at,
                    "osc1".into(),
                    Node::Map(vec![("loopMode".into(), value)]),
                ));
                continue;
            }
            "reversed" => {
                converted.entries.push((
                    at,
                    "osc1".into(),
                    Node::Map(vec![("reversed".into(), value)]),
                ));
                continue;
            }
            "zone" => {
                converted.entries.push((
                    at,
                    "osc1".into(),
                    Node::Map(vec![("zone".into(), value)]),
                ));
                continue;
            }
            "oscillatorReset" => {
                if decimal(&value) == 0 {
                    for element in ["osc1", "osc2", "modulator1", "modulator2"] {
                        let phase = Node::Map(vec![("retrigPhase".into(), text(-1))]);
                        converted.entries.push((at, element.into(), phase));
                    }
                }
                continue;
            }
            "polyphonic" => {
                if let Some(t) = value.text() {
                    let word = match t {
                        "0" => Some("auto"),
                        "2" => Some("choke"),
                        "mono" | "auto" | "legato" | "choke" | "poly" => None,
                        _ => Some("poly"),
                    };
                    if let Some(word) = word {
                        value = text(word);
                    }
                }
            }
            other => {
                if TOP_LEVEL_PARAMS.contains(&other) {
                    param(other, value, None);
                    continue;
                }
            }
        }
        kept.push((key, value));
    }
    *entries = kept;
}

/// Puts the params into `defaultParams` so that the same value wins -- a param read after `defaultParams` replaces its
/// entry; one read before it is kept only where `defaultParams` has none -- and the converted entries where their old
/// forms were. Positions are in the sound with its old forms removed.
fn place(sound: &mut Node, converted: Converted) {
    let entries = sound.entries_mut().expect("a sound is a map");
    if !converted.params.is_empty() {
        let existing = entries.iter().position(|(k, _)| k == "defaultParams");
        let defaults_at = existing.unwrap_or_else(|| {
            // No current-form entry sets a param outside defaultParams, so a new one may go last.
            entries.push(("defaultParams".into(), Node::Map(vec![])));
            entries.len() - 1
        });
        let mut written_by_old: Vec<(Option<&'static str>, String)> = Vec::new();
        let defaults = &mut entries[defaults_at].1;
        for p in converted.params {
            let after_defaults = existing.is_none() || p.at > defaults_at;
            if let Some(group) = p.group
                && defaults.get(group).is_none()
            {
                defaults.set(group, Node::Map(vec![]));
            }
            let target = match p.group {
                Some(group) => defaults.get_mut(group).expect("the group just made"),
                None => &mut *defaults,
            };
            let ours = written_by_old
                .iter()
                .any(|(g, n)| *g == p.group && *n == p.name);
            if after_defaults || ours || target.get(&p.name).is_none() {
                target.set(&p.name, p.value);
                written_by_old.push((p.group, p.name));
            }
        }
    }
    // Latest position first, so earlier positions stay valid; entries at one position keep their order.
    let mut placed = converted.entries;
    placed.reverse();
    placed.sort_by_key(|(at, _, _)| std::cmp::Reverse(*at));
    for (at, key, value) in placed {
        let key = match key.as_str() {
            // The old filter and mod-FX words: read only where the sound has its own key.
            "lpfModeOld" | "fxType" => {
                let own = if key == "lpfModeOld" {
                    "lpfMode"
                } else {
                    "modFXType"
                };
                if entries.iter().any(|(k, _)| k == own) {
                    continue;
                }
                own.to_string()
            }
            _ => key,
        };
        entries.insert(at.min(entries.len()), (key, value));
    }
}

/// A sound reads a repeated source or modulator element into the one it has, a later setting replacing an earlier: the
/// converted fragments are merged into the first, so that every later reader of the document sees one element.
fn merge_repeated_elements(sound: &mut Node) {
    let entries = sound.entries_mut().expect("a sound is a map");
    for name in ["osc1", "osc2", "modulator1", "modulator2"] {
        let Some(first) = entries
            .iter()
            .position(|(k, v)| k == name && matches!(v, Node::Map(_)))
        else {
            continue;
        };
        let mut index = 0;
        let mut later = Vec::new();
        entries.retain(|(k, v)| {
            let repeated = index > first && k == name && matches!(v, Node::Map(_));
            if repeated {
                later.push(v.clone());
            }
            index += 1;
            !repeated
        });
        for fragment in later {
            for (key, value) in fragment.entries() {
                entries[first].1.set(key, value.clone());
            }
        }
    }
}

/// A sound's mode is the last one it reads: an earlier `mode` (left beside a converted ring mod) is dropped, so every
/// later reader of the document sees the one that counts.
fn keep_last_mode(sound: &mut Node) {
    let entries = sound.entries_mut().expect("a sound is a map");
    if let Some(last) = entries.iter().rposition(|(k, _)| k == "mode") {
        let mut index = 0;
        entries.retain(|(k, _)| {
            let keep = k != "mode" || index == last;
            index += 1;
            keep
        });
    }
}

/// Every old form of every sound in the document.
pub fn old_sound_forms(doc: &mut Node, _ctx: &mut Context) {
    for_each_sound(doc, &mut |sound| {
        let mut converted = Converted::default();
        collect(sound, &mut converted);
        place(sound, converted);
        merge_repeated_elements(sound);
        keep_last_mode(sound);
    });
}
