//! A DX7 on an oscillator, as every released firmware wrote one, becomes the sound's DX7 engine: the firmware plays a
//! DX7 only as a sound in DX7 mode, the DX7's settings in the sound's `dx7` entry and no oscillators.
//!
//! The DX7 engine reads osc A's volume, pulse width and pitch adjust, so a DX7 that was on osc 2 takes osc B's in their
//! place, with their cables, knobs and clips' automation. The other oscillator is dropped; one that could sound is
//! noted. Ring modulation and FM never played a DX7: what their load made of one is written instead.

use super::params::{clip_ref, clip_sound_is, for_each_clip, kit_drums_are};
use super::sound::for_each_sound;
use crate::tree::Node;
use crate::upgrade::Context;

/// Each osc A param and the osc B param that takes its place when the DX7 was on osc 2.
const OSC_PARAMS: [(&str, &str); 3] = [
    ("oscAVolume", "oscBVolume"),
    ("oscAPulseWidth", "oscBPulseWidth"),
    ("oscAPitchAdjust", "oscBPitchAdjust"),
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

fn dx7_on_osc_2(sound: &Node) -> bool {
    dx7_osc(sound) == Some("osc2")
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

/// Osc B's params, in a param set of a sound whose DX7 was on osc 2, take osc A's places; osc A's cables go, each
/// returned for the report.
fn take_osc_b(params: &mut Node) -> Vec<String> {
    let mut dropped = Vec::new();
    if let Some(cables) = params.get_mut("patchCables").and_then(Node::entries_mut) {
        cables.retain(|(_, cable)| {
            let to = destination(cable).unwrap_or("");
            if OSC_PARAMS.iter().any(|(a, _)| *a == to) {
                let from = cable.get("source").and_then(Node::text).unwrap_or("?");
                dropped.push(format!("osc1's cable from {from} to {to} dropped"));
                return false;
            }
            true
        });
        for (_, cable) in cables.iter_mut() {
            if let Some((a, _)) = OSC_PARAMS
                .iter()
                .find(|(_, b)| destination(cable) == Some(b))
            {
                cable.set("destination", Node::Text((*a).to_string()));
            }
        }
    }
    if let Some(entries) = params.entries_mut() {
        entries.retain(|(key, _)| !OSC_PARAMS.iter().any(|(a, _)| a == key));
        for (key, _) in entries.iter_mut() {
            if let Some((a, _)) = OSC_PARAMS.iter().find(|(_, b)| b == key) {
                *key = (*a).to_string();
            }
        }
    }
    dropped
}

/// A knob of a sound whose DX7 was on osc 2 that turned an osc B param turns osc A's.
fn retarget_knobs(sound: &mut Node) {
    for container in ["modKnobs", "midiKnobs"] {
        for (_, knob) in sound
            .get_mut(container)
            .and_then(Node::entries_mut)
            .into_iter()
            .flatten()
        {
            let param = knob.get("controlsParam").and_then(Node::text);
            if let Some((a, _)) = OSC_PARAMS.iter().find(|(_, b)| param == Some(b)) {
                knob.set("controlsParam", Node::Text((*a).to_string()));
            }
        }
    }
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
    if osc == "osc2" {
        if let Some(params) = sound.get_mut("defaultParams") {
            notes.extend(take_osc_b(params));
        }
        retarget_knobs(sound);
    }
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

/// Every sound's DX7 on an oscillator becomes its DX7 engine, and a song's clips of one whose DX7 was on osc 2 move
/// their osc B params to osc A's.
pub fn dx7_engine(doc: &mut Node, ctx: &mut Context) {
    // The song as it was: which instruments had their DX7 on osc 2
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
            if clip_sound_is(&song, clip_ref(clip), dx7_on_osc_2)
                && let Some(params) = clip.get_mut("soundParams")
            {
                notes.extend(take_osc_b(params));
            }
            let drums = kit_drums_are(&song, clip, dx7_on_osc_2);
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
                    notes.extend(take_osc_b(params));
                }
            }
        });
    }
    for note in notes {
        ctx.note("dx7 engine", note);
    }
}
