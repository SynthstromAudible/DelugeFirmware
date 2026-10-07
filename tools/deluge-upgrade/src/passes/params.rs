//! Param values official firmware recalculated on loading an old file: the volume a pre-1.2 sound or kit lost to
//! resonance, the arpeggiator rate of a pre-1.5 song's synth clips, and a pre-3.1 song's swing interval.

use super::sound::for_each_sound;
use crate::tree::Node;
use crate::upgrade::Context;
use crate::version::FirmwareVersion;

/// A param as a file holds it: its value, and the automation nodes after it (8 hex digits of value, 8 of position).
struct ParamText {
    value: i32,
    nodes: Vec<(i32, String)>,
    hex: bool,
}

impl ParamText {
    fn parse(text: &str) -> Self {
        let text = text.trim();
        match text.strip_prefix("0x") {
            Some(rest) if rest.len() >= 8 => {
                let value = u32::from_str_radix(&rest[..8], 16).unwrap_or(0) as i32;
                let nodes = rest.as_bytes()[8..]
                    .chunks(16)
                    .filter(|chunk| chunk.len() == 16)
                    .map(|chunk| {
                        let chunk = std::str::from_utf8(chunk).unwrap_or("0000000000000000");
                        (
                            u32::from_str_radix(&chunk[..8], 16).unwrap_or(0) as i32,
                            chunk[8..].to_string(),
                        )
                    })
                    .collect();
                ParamText {
                    value,
                    nodes,
                    hex: true,
                }
            }
            _ => {
                let digits: String = text
                    .chars()
                    .take(11)
                    .take_while(|c| *c == '-' || c.is_ascii_digit())
                    .collect();
                ParamText {
                    value: digits.parse().unwrap_or(0),
                    nodes: Vec::new(),
                    hex: false,
                }
            }
        }
    }

    fn automated(&self) -> bool {
        !self.nodes.is_empty()
    }

    /// Whether the param holds anything other than `value`: automation, or another value.
    fn holds_other_than(&self, value: i32) -> bool {
        self.automated() || self.value != value
    }

    fn map(&mut self, f: impl Fn(i32) -> i32) {
        self.value = f(self.value);
        for (value, _) in &mut self.nodes {
            *value = f(*value);
        }
    }

    fn text(&self) -> String {
        if !self.hex && self.nodes.is_empty() {
            return self.value.to_string();
        }
        let mut out = format!("0x{:08X}", self.value as u32);
        for (value, position) in &self.nodes {
            out.push_str(&format!("{:08X}{position}", *value as u32));
        }
        out
    }
}

fn param(params: &Node, name: &str) -> Option<ParamText> {
    params.get(name).and_then(Node::text).map(ParamText::parse)
}

fn set_param(params: &mut Node, name: &str, value: &ParamText) {
    params.set(name, Node::Text(value.text()));
}

/// The decibels of volume per eighth of the range (`dbIntervals`).
const DB_INTERVALS: [f32; 16] = [
    24.0, 12.1, 7.0, 5.0, 3.9, 3.2, 2.6, 2.4, 2.0, 1.8, 1.7, 1.5, 1.4, 1.3, 1.2, 1.1,
];

/// `shiftVolumeByDB`, in single precision as the firmware computes it (its integer division making the starting
/// fraction 0).
fn shift_volume_by_db(old: i32, mut offset: f32) -> i32 {
    let positive = (old as u32).wrapping_add(2_147_483_648);
    let mut interval = positive >> 28;
    if interval < 1 {
        return old;
    }
    let mut how_far: f32 = 0.0;
    loop {
        let db_this = DB_INTERVALS[interval as usize];
        let db_left = (1.0_f32 - how_far) * db_this;
        if db_left > offset {
            let wanted = offset / db_left;
            let new_how_far = how_far + wanted;
            let new_positive =
                (interval << 28).wrapping_add((new_how_far * 268_435_456.0_f32) as u32);
            return new_positive.wrapping_sub(2_147_483_648) as i32;
        }
        interval += 1;
        if interval == 16 {
            return 0xFFFF_FFFF_u32.wrapping_sub(2_147_483_648) as i32;
        }
        offset -= db_left;
        how_far = 0.0;
    }
}

/// The volume a pre-1.2 filter's resonance cost, in decibels (`oldResonanceCompensation`, interpolated).
fn resonance_compensation_db(resonance: i32) -> f32 {
    const TABLE: [i32; 9] = [8192, 4915, 2764, 1024, 0, 0, 0, 0, 0];
    let input = (resonance as u32).wrapping_add(2_147_483_648);
    let which = (input >> 29) as usize;
    let strength2 = ((input >> 13) & 65535) as i32;
    let strength1 = 65536 - strength2;
    let compensation = TABLE[which]
        .wrapping_mul(strength1)
        .wrapping_add(TABLE[which + 1].wrapping_mul(strength2));
    compensation as f32 / (1024 << 16) as f32
}

fn compensate(params: &mut Node, resonance: i32) {
    let db = resonance_compensation_db(resonance);
    if f64::from(db) > 0.1
        && let Some(mut volume) = param(params, "volume")
    {
        volume.map(|v| shift_volume_by_db(v, db));
        set_param(params, "volume", &volume);
    }
}

/// Whether any cable in `params` reaches `destination`.
fn patched_to(params: &Node, destination: &str) -> bool {
    params.get("patchCables").is_some_and(|set| {
        set.entries()
            .iter()
            .any(|(_, cable)| cable.get("destination").and_then(Node::text) == Some(destination))
    })
}

/// The pre-1.2 rule on one set of a sound's params: the volume its resonance cost, given back; and a filter left fully
/// open with a little resonance has none. False when the set has no resonance to go by.
fn sound_rule(params: &mut Node) -> bool {
    let Some(resonance) = param(params, "lpfResonance") else {
        return false;
    };
    compensate(params, resonance.value);
    let open = param(params, "lpfFrequency").is_some_and(|f| {
        !f.automated() && f.value >= 2_147_483_602 && !patched_to(params, "lpfFrequency")
    });
    if open && !resonance.automated() && (-23..=0).contains(&resonance.value) {
        set_param(
            params,
            "lpfResonance",
            &ParamText {
                value: i32::MIN,
                nodes: Vec::new(),
                hex: true,
            },
        );
    }
    true
}

/// A pre-1.2 sound (not FM): the rule on its own params; in a song, on each clip's params too (a synth clip's, a kit
/// clip's rows'), as the firmware applied it to every param set it loaded.
pub fn sound_resonance(doc: &mut Node, ctx: &mut Context) {
    let mut missing = false;
    for_each_sound(doc, &mut |sound| {
        if sound.get("mode").and_then(Node::text) == Some("fm") {
            return;
        }
        if let Some(params) = sound.get_mut("defaultParams") {
            missing |= !sound_rule(params);
        }
    });
    if let Some(song) = doc.get("song").cloned() {
        for_each_clip(doc, &mut |clip| {
            let fm = clip_is_fm(&song, clip_ref(clip));
            if !fm && let Some(params) = clip.get_mut("soundParams") {
                missing |= !sound_rule(params);
            }
        });
        for_each_clip(doc, &mut |clip| {
            let drums = kit_of_clip(&song, clip);
            for (_, row) in clip
                .get_mut("noteRows")
                .and_then(Node::entries_mut)
                .into_iter()
                .flatten()
            {
                let fm = row
                    .get("drumIndex")
                    .and_then(Node::int)
                    .and_then(|i| drums.get(usize::try_from(i).ok()?))
                    .is_some_and(|fm| *fm);
                if !fm && let Some(params) = row.get_mut("soundParams") {
                    missing |= !sound_rule(params);
                }
            }
            if let Some(params) = clip.get_mut("kitParams") {
                kit_rule(params);
            }
        });
        if let Some(params) = doc.get_mut("song").and_then(|s| s.get_mut("songParams")) {
            zero_filter_resonance(params);
        }
    }
    if missing {
        ctx.note(
            "resonance compensation",
            "a param set without an lpfResonance param was left as it is".into(),
        );
    }
}

/// Every clip of a song, session and arrangement-only.
pub(crate) fn for_each_clip(doc: &mut Node, f: &mut dyn FnMut(&mut Node)) {
    let Some(song) = doc.get_mut("song") else {
        return;
    };
    for container in ["sessionClips", "arrangementOnlyTracks"] {
        for (key, clip) in song
            .get_mut(container)
            .and_then(Node::entries_mut)
            .into_iter()
            .flatten()
        {
            if key == "instrumentClip" {
                f(clip);
            }
        }
    }
}

/// How a clip names its instrument: its preset name, or slot and sub-slot.
fn clip_ref(clip: &Node) -> (Option<String>, Option<String>, Option<String>) {
    let get = |k: &str| clip.get(k).and_then(Node::text).map(str::to_string);
    (
        get("instrumentPresetName"),
        get("instrumentPresetSlot"),
        get("instrumentPresetSubSlot"),
    )
}

fn names(
    instrument: &Node,
    (name, slot, sub_slot): &(Option<String>, Option<String>, Option<String>),
) -> bool {
    let get = |k: &str| instrument.get(k).and_then(Node::text).map(str::to_string);
    (name.is_some() && get("presetName") == *name)
        || (slot.is_some() && get("presetSlot") == *slot && get("presetSubSlot") == *sub_slot)
}

fn clip_is_fm(song: &Node, reference: (Option<String>, Option<String>, Option<String>)) -> bool {
    song.get("instruments")
        .into_iter()
        .flat_map(Node::entries)
        .any(|(kind, synth)| {
            kind == "sound"
                && synth.get("mode").and_then(Node::text) == Some("fm")
                && names(synth, &reference)
        })
}

/// Whether each drum of the kit a clip plays is FM, by drum index.
fn kit_of_clip(song: &Node, clip: &Node) -> Vec<bool> {
    let reference = clip_ref(clip);
    song.get("instruments")
        .into_iter()
        .flat_map(Node::entries)
        .find(|(kind, kit)| kind == "kit" && names(kit, &reference))
        .map(|(_, kit)| {
            kit.get("soundSources")
                .into_iter()
                .flat_map(Node::entries)
                .map(|(kind, drum)| {
                    kind == "sound" && drum.get("mode").and_then(Node::text) == Some("fm")
                })
                .collect()
        })
        .unwrap_or_default()
}

/// The global effectable's filters, as a pre-1.2 kit or song left them: an open LPF's half resonance and an off
/// HPF's quarter resonance become none (`GlobalEffectable::compensateVolumeForResonance`, its checks as they are).
fn zero_filter_resonance(params: &mut Node) {
    let lpf_resonance = params.get("lpf").and_then(|lpf| param(lpf, "resonance"));
    let lpf_frequency = params.get("lpf").and_then(|lpf| param(lpf, "frequency"));
    let hpf_frequency = params.get("hpf").and_then(|hpf| param(hpf, "frequency"));
    if let (Some(frequency), Some(resonance)) = (&lpf_frequency, &lpf_resonance)
        && !frequency.automated()
        && frequency.value >= 2_147_483_602
        && !resonance.holds_other_than(0)
        && let Some(lpf) = params.get_mut("lpf")
    {
        set_param(
            lpf,
            "resonance",
            &ParamText {
                value: i32::MIN,
                nodes: Vec::new(),
                hex: true,
            },
        );
    }
    if let (Some(frequency), Some(resonance)) = (&hpf_frequency, &lpf_resonance)
        && !frequency.holds_other_than(i32::MIN)
        && !resonance.holds_other_than(-1_073_741_824)
        && let Some(hpf) = params.get_mut("hpf")
    {
        set_param(
            hpf,
            "resonance",
            &ParamText {
                value: i32::MIN,
                nodes: Vec::new(),
                hex: true,
            },
        );
    }
}

/// The pre-1.2 rule on a kit's params: the volume its own filter's resonance cost, given back, then its filters.
fn kit_rule(params: &mut Node) {
    if let Some(resonance) = params.get("lpf").and_then(|lpf| param(lpf, "resonance")) {
        compensate(params, resonance.value);
    }
    zero_filter_resonance(params);
}

/// Every `kit` element: a kit preset's root, a song's kits.
fn for_each_kit(node: &mut Node, f: &mut dyn FnMut(&mut Node)) {
    if let Some(entries) = node.entries_mut() {
        for (key, child) in entries.iter_mut() {
            if key == "kit" && matches!(child, Node::Map(_)) {
                f(child);
            }
            for_each_kit(child, f);
        }
    }
}

/// A pre-1.2 kit: the volume its own filter's resonance cost, given back, then its filters as above.
pub fn kit_resonance(doc: &mut Node, _ctx: &mut Context) {
    for_each_kit(doc, &mut |kit| {
        if let Some(params) = kit.get_mut("defaultParams") {
            kit_rule(params);
        }
    });
}

/// A kit's, song's or clip's params with each filter's morph nested in its `lpf` / `hpf` element, as community nightlies
/// wrote them in 2024: the flat `lpfMorph` / `hpfMorph` the releases write, where the nested one was read, unless a flat
/// one is read after it.
pub fn nested_filter_morph(doc: &mut Node, _ctx: &mut Context) {
    fn params(set: &mut Node) {
        let Some(entries) = set.entries_mut() else {
            return;
        };
        for filter in ["lpf", "hpf"] {
            let flat = format!("{filter}Morph");
            let Some(at) = entries
                .iter()
                .position(|(k, v)| k == filter && v.get("morph").is_some())
            else {
                continue;
            };
            let morph = entries[at]
                .1
                .entries_mut()
                .and_then(|e| {
                    e.iter()
                        .position(|(k, _)| k == "morph")
                        .map(|i| e.remove(i).1)
                })
                .expect("found above");
            if entries[at + 1..].iter().any(|(k, _)| *k == flat) {
                continue;
            }
            let earlier = entries[..at].iter().filter(|(k, _)| *k == flat).count();
            entries.retain(|(k, _)| *k != flat);
            entries.insert(at - earlier, (flat, morph));
        }
    }
    fn walk(node: &mut Node, in_sound: bool) {
        let Some(entries) = node.entries_mut() else {
            return;
        };
        for (key, child) in entries.iter_mut() {
            if !in_sound
                && matches!(
                    key.as_str(),
                    "defaultParams" | "songParams" | "kitParams" | "params"
                )
            {
                params(child);
            }
            walk(child, key == "sound");
        }
    }
    walk(doc, false);
}

/// A kit preset from before 2.2 without params of its own was given the defaults on load: an empty `defaultParams`
/// is read as them. Before 1.2 those defaults' resonance also raised the kit's volume by 0.125 dB, which is reported.
pub fn paramless_kit(doc: &mut Node, ctx: &mut Context) {
    let Some(kit) = doc.get_mut("kit") else {
        return;
    };
    if kit.get("defaultParams").is_some() {
        return;
    }
    kit.set("defaultParams", Node::Map(vec![]));
    if ctx.version < FirmwareVersion::official(1, 2, 0) {
        ctx.note(
            "kit params",
            "a kit from before 1.2 without params gets the defaults without the 0.125 dB its default resonance \
             added to its volume"
                .into(),
        );
    }
}

/// A pre-1.5 song's synth clips with an unsynced arpeggiator: their arpeggiator rate was raised to keep its speed.
pub fn arpeggiator_rate(doc: &mut Node, _ctx: &mut Context) {
    for_each_clip(doc, &mut |clip| {
        let arpeggiating = clip.get("arpeggiator").is_some_and(|arp| {
            let mode = arp.get("mode").and_then(Node::text).unwrap_or("off");
            mode != "off" && arp.get("syncLevel").and_then(Node::int) == Some(0)
        });
        if !arpeggiating {
            return;
        }
        if let Some(params) = clip.get_mut("soundParams")
            && let Some(mut rate) = param(params, "arpeggiatorRate")
        {
            rate.map(|v| {
                (i64::from(v) + (1 << 30) + (1 << 28))
                    .clamp(i64::from(i32::MIN), i64::from(i32::MAX)) as i32
            });
            rate.hex = true;
            set_param(params, "arpeggiatorRate", &rate);
        }
    });
}

/// A pre-3.1 song's swing interval was a sync level of its own; from 3.1 it is relative to the input tick magnitude.
pub fn swing_interval(doc: &mut Node, ctx: &mut Context) {
    let Some(song) = doc.get_mut("song") else {
        return;
    };
    let Some(interval) = song.get("swingInterval").and_then(Node::int) else {
        return;
    };
    let magnitude = song
        .get("inputTickMagnitude")
        .and_then(Node::int)
        .unwrap_or(1);
    let level = i64::from(interval as u8).min(9);
    if level == 0 {
        return;
    }
    // The reader takes the stored value as a byte: no value below 1 reads back as a level.
    let file = level - 1 + magnitude;
    if file < 1 {
        ctx.note(
            "swing interval",
            format!("level {level} cannot be written at tick magnitude {magnitude}"),
        );
        return;
    }
    song.set("swingInterval", Node::Text(file.to_string()));
}

/// Before official 3.2 a clip's bend range was always 12 and not saved: a clip with no expression of its own is given
/// it. (Its MPE bend range came from the device's settings, as it still does for a clip that does not save one.)
pub fn bend_range(doc: &mut Node, _ctx: &mut Context) {
    for_each_clip(doc, &mut |clip| {
        if clip.get("expressionData").is_some() || clip.get("bendRange").is_some() {
            return;
        }
        let expressive_cc = clip.get("midiParams").is_some_and(|params| {
            params.entries().iter().any(|(_, p)| {
                p.get("cc").and_then(Node::text).is_some_and(|cc| {
                    ["bend", "aftertouch", "channelPressure"]
                        .iter()
                        .any(|n| cc.trim().eq_ignore_ascii_case(n))
                })
            })
        });
        if !expressive_cc {
            clip.set("bendRange", Node::Text("12".into()));
        }
    });
}

/// Before official 1.2 a clip's params were read onto a copy of its instrument's, and a kit row's onto a copy of its
/// drum's: a param the clip or row does not hold is its instrument's or drum's.
pub fn clip_param_overlay(doc: &mut Node, _ctx: &mut Context) {
    let Some(song) = doc.get("song").cloned() else {
        return;
    };
    let instrument_params = |reference: &(Option<String>, Option<String>, Option<String>)| {
        song.get("instruments")
            .into_iter()
            .flat_map(Node::entries)
            .find(|(kind, instrument)| kind == "sound" && names(instrument, reference))
            .and_then(|(_, synth)| synth.get("defaultParams").cloned())
    };
    // A kit's drums in the order a row's `drumIndex` counts them; only a sound drum has params.
    let kit_drums = |reference: &(Option<String>, Option<String>, Option<String>)| {
        song.get("instruments")
            .into_iter()
            .flat_map(Node::entries)
            .find(|(kind, instrument)| kind == "kit" && names(instrument, reference))
            .and_then(|(_, kit)| kit.get("soundSources").cloned())
    };
    fn overlay(params: &mut Node, defaults: &Node) {
        for (key, value) in defaults.entries() {
            if params.get(key).is_none() {
                params.set(key, value.clone());
            }
        }
    }
    for_each_clip(doc, &mut |clip| {
        let reference = clip_ref(clip);
        if let Some(defaults) = instrument_params(&reference)
            && let Some(params) = clip.get_mut("soundParams")
        {
            overlay(params, &defaults);
        }
        let Some(drums) = kit_drums(&reference) else {
            return;
        };
        let Some(rows) = clip.get_mut("noteRows").and_then(Node::entries_mut) else {
            return;
        };
        for (_, row) in rows.iter_mut() {
            let drum = row
                .get("drumIndex")
                .and_then(Node::int)
                .and_then(|i| usize::try_from(i).ok())
                .and_then(|i| drums.entries().get(i));
            if let Some((kind, drum)) = drum
                && kind == "sound"
                && let Some(defaults) = drum.get("defaultParams")
                && let Some(params) = row.get_mut("soundParams")
            {
                overlay(params, defaults);
            }
        }
    });
}

/// Before official 2.0 a synth clip's pulse width on a source that was not a square was cleared on load; the upgrader
/// does not follow a clip to its sources' types, so it reports such a song.
pub fn pulse_width(doc: &mut Node, ctx: &mut Context) {
    let mut found = false;
    for_each_clip(doc, &mut |clip| {
        if let Some(params) = clip.get("soundParams") {
            found |= ["oscAPulseWidth", "oscBPulseWidth"]
                .iter()
                .any(|p| param(params, p).is_some_and(|v| v.holds_other_than(0)));
        }
    });
    if found {
        ctx.note(
            "pulse width",
            "a pre-2.0 synth clip's pulse width is left as it is; the firmware cleared it on a \
                                 source that was not a square"
                .into(),
        );
    }
}
