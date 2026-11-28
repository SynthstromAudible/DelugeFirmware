//! A DX7 on an oscillator, as every released firmware wrote one, becomes the sound's DX7 engine: the firmware plays a
//! DX7 only as a sound in DX7 mode, the DX7's settings in the sound's `dx7` entry and no oscillators.
//!
//! The DX7 engine has a level, an amplitude modulation and a pitch adjust of its own, which a DX7 on an oscillator took
//! from that oscillator's volume, pulse width and pitch adjust: those move onto the DX7's params (`dx7Level`,
//! `dx7AmpMod`, `dx7PitchAdjust`), with their cables, knobs and clips' automation. A DX7-mode sound has no oscillator
//! engine params, so the rest of them go, with their cables, knobs and automation: each cable, knob and automated
//! value dropped is noted, as is the other oscillator where it could sound. Ring modulation and FM never played a DX7:
//! what their load made of one is written instead.

use super::params::{clip_ref, clip_sound_is, for_each_clip, kit_drums_are};
use super::sound::for_each_sound;
use crate::tree::Node;
use crate::upgrade::Context;

/// A param a DX7 on an oscillator took from that oscillator: its value's name on osc A and osc B (a param set's
/// attribute), its name on each as a cable's destination or a knob's param, and the DX7's own (one name for both).
struct OscParam {
    value_a: &'static str,
    value_b: &'static str,
    param_a: &'static str,
    param_b: &'static str,
    dx7: &'static str,
}

const OSC_PARAMS: [OscParam; 3] = [
    OscParam {
        value_a: "oscAVolume",
        value_b: "oscBVolume",
        param_a: "oscAVolume",
        param_b: "oscBVolume",
        dx7: "dx7Level",
    },
    OscParam {
        value_a: "oscAPulseWidth",
        value_b: "oscBPulseWidth",
        param_a: "oscAPhaseWidth",
        param_b: "oscBPhaseWidth",
        dx7: "dx7AmpMod",
    },
    OscParam {
        value_a: "oscAPitchAdjust",
        value_b: "oscBPitchAdjust",
        param_a: "oscAPitch",
        param_b: "oscBPitch",
        dx7: "dx7PitchAdjust",
    },
];

impl OscParam {
    /// The param's value name on oscillator `osc`.
    fn value_on(&self, osc: &str) -> &'static str {
        if osc == "osc1" { self.value_a } else { self.value_b }
    }
    /// The param's name on oscillator `osc` as a cable's destination or a knob's param.
    fn param_on(&self, osc: &str) -> &'static str {
        if osc == "osc1" { self.param_a } else { self.param_b }
    }
}

/// The oscillator engines' params as a param set names their values: a DX7-mode sound saves none of them.
const OSC_ENGINE_VALUES: [&str; 17] = [
    "oscAVolume",
    "oscBVolume",
    "noiseVolume",
    "modulator1Amount",
    "modulator2Amount",
    "modulator1Feedback",
    "modulator2Feedback",
    "carrier1Feedback",
    "carrier2Feedback",
    "oscAPulseWidth",
    "oscBPulseWidth",
    "oscAWavetablePosition",
    "oscBWavetablePosition",
    "oscAPitchAdjust",
    "oscBPitchAdjust",
    "mod1PitchAdjust",
    "mod2PitchAdjust",
];

/// The same params as a cable's destination or a knob's param names them.
const OSC_ENGINE_PARAMS: [&str; 17] = [
    "oscAVolume",
    "oscBVolume",
    "noiseVolume",
    "modulator1Volume",
    "modulator2Volume",
    "modulator1Feedback",
    "modulator2Feedback",
    "carrier1Feedback",
    "carrier2Feedback",
    "oscAPhaseWidth",
    "oscBPhaseWidth",
    "oscAWavetablePosition",
    "oscBWavetablePosition",
    "oscAPitch",
    "oscBPitch",
    "modulator1Pitch",
    "modulator2Pitch",
];

/// Each oscillator setting the DX7 keeps, and its name in the `dx7` entry.
const DX7_KEYS: [(&str, &str); 6] = [
    ("transpose", "transpose"),
    ("cents", "cents"),
    ("retrigPhase", "retrigPhase"),
    ("dx7patch", "patch"),
    ("dx7enginemode", "engineMode"),
    ("dx7randomdetune", "randomDetune"),
];

/// A source volume's minimum (the word 0x80000000): at it the oscillator cannot sound.
const SILENT: i32 = i32::MIN;

fn mode(sound: &Node) -> &str {
    sound
        .get("mode")
        .and_then(Node::text)
        .unwrap_or("subtractive")
}

fn osc_type<'a>(sound: &'a Node, osc: &str) -> Option<&'a str> {
    sound.get(osc)?.get("type")?.text()
}

/// The oscillator a subtractive sound plays a DX7 on: the first of osc 1 and osc 2 whose type is `dx7`.
fn dx7_osc(sound: &Node) -> Option<&'static str> {
    if mode(sound) != "subtractive" {
        return None;
    }
    ["osc1", "osc2"]
        .into_iter()
        .find(|osc| osc_type(sound, osc) == Some("dx7"))
}

fn destination(cable: &Node) -> Option<&str> {
    cable.get("destination").and_then(Node::text)
}

/// Whether the oscillator `osc` can sound, as the app decides a source is active: oscillator sync runs it, a cable to
/// its volume can raise it, and an unwritten, automated or unsilent volume plays.
fn audible(sound: &Node, osc: &str) -> bool {
    let volume = if osc == "osc1" {
        "oscAVolume"
    } else {
        "oscBVolume"
    };
    if sound
        .get("oscillatorSync")
        .and_then(Node::int)
        .is_some_and(|sync| sync != 0)
    {
        return true;
    }
    let params = sound.get("defaultParams");
    let patched = params
        .and_then(|p| p.get("patchCables"))
        .is_some_and(|cables| {
            cables
                .entries()
                .iter()
                .any(|(_, cable)| destination(cable) == Some(volume))
        });
    match params.and_then(|p| p.get(volume)).and_then(Node::text) {
        None => true,
        Some(text) => {
            let text = text.trim();
            let word = text
                .strip_prefix("0x")
                .and_then(|hex| u32::from_str_radix(hex.get(..8)?, 16).ok())
                .map_or_else(|| text.parse::<i32>().unwrap_or(0), |word| word as i32);
            patched || text.len() > "0x".len() + 8 || word > SILENT
        }
    }
}

/// Whether a param's text holds automation: nodes after its value's 8 hex digits.
fn automated(value: &Node) -> bool {
    value
        .text()
        .is_some_and(|text| text.trim().len() > "0x".len() + 8)
}

/// The DX7's params, in a param set of a sound whose DX7 was on `osc`, take that oscillator's values and cables; the
/// oscillator engines' other params go, with their cables. What was lost (a cable, an automated value) is returned.
fn move_onto_dx7(params: &mut Node, osc: &str, whose: &str) -> Vec<String> {
    let mut dropped = Vec::new();
    if let Some(cables) = params.get_mut("patchCables").and_then(Node::entries_mut) {
        for (_, cable) in cables.iter_mut() {
            if let Some(param) = OSC_PARAMS
                .iter()
                .find(|param| destination(cable) == Some(param.param_on(osc)))
            {
                cable.set("destination", Node::Text(param.dx7.to_string()));
            }
        }
        cables.retain(|(_, cable)| {
            let to = destination(cable).unwrap_or("");
            if OSC_ENGINE_PARAMS.contains(&to) {
                let from = cable.get("source").and_then(Node::text).unwrap_or("?");
                dropped.push(format!("{whose}cable from {from} to {to} dropped"));
                return false;
            }
            true
        });
    }
    // The firmware writes no cables block for none
    if params
        .get("patchCables")
        .is_some_and(|cables| matches!(cables, Node::Map(_)) && cables.entries().is_empty())
    {
        params.remove("patchCables");
    }
    let Some(entries) = params.entries_mut() else {
        return dropped;
    };
    let mut moved = Vec::new();
    for param in &OSC_PARAMS {
        if let Some(at) = entries
            .iter()
            .position(|(key, _)| key == param.value_on(osc))
        {
            moved.push((param.dx7.to_string(), entries.remove(at).1));
        }
    }
    entries.retain(|(key, value)| {
        if !OSC_ENGINE_VALUES.contains(&key.as_str()) {
            return true;
        }
        if automated(value) {
            dropped.push(format!("{whose}automation of {key} dropped"));
        }
        false
    });
    // Where the firmware writes them: after the wave fold, among the values before the param groups
    let at = entries
        .iter()
        .position(|(key, _)| key == "waveFold")
        .or_else(|| entries.iter().rposition(|(_, value)| value.text().is_some()))
        .map_or(0, |at| at + 1);
    for (offset, entry) in moved.into_iter().enumerate() {
        entries.insert(at + offset, entry);
    }
    dropped
}

/// A knob of a sound whose DX7 was on `osc` that turned one of that oscillator's moved params turns the DX7's; one that
/// turned another oscillator engine param turns nothing (a mod knob keeps its place) or goes (a MIDI knob). Each
/// dropped is returned.
fn retarget_knobs(sound: &mut Node, osc: &str) -> Vec<String> {
    let mut dropped = Vec::new();
    for container in ["modKnobs", "midiKnobs"] {
        let Some(knobs) = sound.get_mut(container).and_then(Node::entries_mut) else {
            continue;
        };
        for (_, knob) in knobs.iter_mut() {
            let controls = knob.get("controlsParam").and_then(Node::text);
            if let Some(param) = OSC_PARAMS
                .iter()
                .find(|param| controls == Some(param.param_on(osc)))
            {
                knob.set("controlsParam", Node::Text(param.dx7.to_string()));
            }
        }
        knobs.retain_mut(|(_, knob)| {
            let Some(controls) = knob.get("controlsParam").and_then(Node::text) else {
                return true;
            };
            if !OSC_ENGINE_PARAMS.contains(&controls) {
                return true;
            }
            dropped.push(format!("{container} knob on {controls} dropped"));
            if container == "modKnobs" {
                knob.set("controlsParam", Node::Text("none".to_string()));
                knob.remove("patchAmountFromSource");
                knob.remove("patchAmountFromSecondSource");
                return true;
            }
            false
        });
    }
    dropped
}

/// A subtractive sound's DX7 on `osc` becomes its DX7 engine; what was lost is returned for the report.
fn to_engine(sound: &mut Node, osc: &'static str) -> Vec<String> {
    let other = if osc == "osc1" { "osc2" } else { "osc1" };
    let mut notes = Vec::new();
    if audible(sound, other) {
        // A sound writes no type for a sine
        let kind = osc_type(sound, other).unwrap_or("sine");
        notes.push(format!("{other} dropped: {kind}"));
    }
    let mut dx7 = Node::Map(Vec::new());
    if let Some(from) = sound.get(osc) {
        for (key, name) in DX7_KEYS {
            if let Some(value) = from.get(key) {
                dx7.set(name, value.clone());
            }
        }
    }
    if let Some(entries) = sound.entries_mut() {
        let at = entries
            .iter()
            .position(|(key, _)| key == "osc1" || key == "osc2")
            .unwrap_or(entries.len());
        entries.retain(|(key, _)| key != "osc1" && key != "osc2");
        entries.insert(at.min(entries.len()), ("dx7".to_string(), dx7));
    }
    sound.set("mode", Node::Text("dx7".to_string()));
    if let Some(params) = sound.get_mut("defaultParams") {
        notes.extend(move_onto_dx7(params, osc, ""));
    }
    notes.extend(retarget_knobs(sound, osc));
    notes
}

/// A ring-modulated or FM sound's DX7 oscillator as the firmware's load left it: ring modulation takes nothing past a
/// wavetable, FM plays sines.
fn retype(sound: &mut Node, kind: &str) {
    for osc in ["osc1", "osc2"] {
        if osc_type(sound, osc) != Some("dx7") {
            continue;
        }
        if let Some(osc) = sound.get_mut(osc) {
            osc.set("type", Node::Text(kind.to_string()));
            for key in ["dx7patch", "dx7enginemode", "dx7randomdetune"] {
                osc.remove(key);
            }
        }
    }
}

/// Every sound's DX7 on an oscillator becomes its DX7 engine, and a song's clips of one move that oscillator's params
/// onto the DX7's.
pub fn dx7_engine(doc: &mut Node, ctx: &mut Context) {
    // The song as it was: which instruments had a DX7 on which oscillator
    let song = doc.get("song").cloned();
    let mut notes = Vec::new();
    for_each_sound(doc, &mut |sound| match mode(sound) {
        "ringmod" => retype(sound, "wavetable"),
        "fm" => retype(sound, "sine"),
        _ => {
            if let Some(osc) = dx7_osc(sound) {
                notes.extend(to_engine(sound, osc));
            }
        }
    });
    if let Some(song) = song {
        for_each_clip(doc, &mut |clip| {
            for osc in ["osc1", "osc2"] {
                let on_osc = |sound: &Node| dx7_osc(sound) == Some(osc);
                if clip_sound_is(&song, clip_ref(clip), on_osc)
                    && let Some(params) = clip.get_mut("soundParams")
                {
                    notes.extend(move_onto_dx7(params, osc, "a clip's "));
                }
                let drums = kit_drums_are(&song, clip, on_osc);
                for (_, row) in clip
                    .get_mut("noteRows")
                    .and_then(Node::entries_mut)
                    .into_iter()
                    .flatten()
                {
                    let moved = row
                        .get("drumIndex")
                        .and_then(Node::int)
                        .and_then(|i| drums.get(usize::try_from(i).ok()?))
                        .is_some_and(|moved| *moved);
                    if moved && let Some(params) = row.get_mut("soundParams") {
                        notes.extend(move_onto_dx7(params, osc, "a kit row's "));
                    }
                }
            }
        });
    }
    for note in notes {
        ctx.note("dx7 engine", note);
    }
}
