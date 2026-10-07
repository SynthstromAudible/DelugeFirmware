//! Param values official firmware recalculated on loading an old file: the volume a pre-1.2 sound or kit lost to
//! resonance, the arpeggiator rate of a pre-1.5 song's synth clips, and a pre-3.1 song's swing interval.

use super::sound::for_each_sound;
use crate::tree::Node;
use crate::upgrade::Context;

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

/// A pre-1.2 sound (not FM): the volume its resonance cost, given back; and a filter left fully open with a little
/// resonance has none.
pub fn sound_resonance(doc: &mut Node, ctx: &mut Context) {
    let mut missing = false;
    for_each_sound(doc, &mut |sound| {
        if sound.get("mode").and_then(Node::text) == Some("fm") {
            return;
        }
        let Some(params) = sound.get_mut("defaultParams") else {
            return;
        };
        let Some(resonance) = param(params, "lpfResonance") else {
            missing = true;
            return;
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
    });
    if missing {
        ctx.note(
            "resonance compensation",
            "a sound without an lpfResonance param was left as it is".into(),
        );
    }
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
pub fn kit_resonance(doc: &mut Node, ctx: &mut Context) {
    let mut paramless = false;
    for_each_kit(doc, &mut |kit| {
        let Some(params) = kit.get_mut("defaultParams") else {
            paramless = true;
            return;
        };
        if let Some(resonance) = params.get("lpf").and_then(|lpf| param(lpf, "resonance")) {
            compensate(params, resonance.value);
        }
        zero_filter_resonance(params);
    });
    if paramless {
        ctx.note(
            "kit params",
            "a kit from before 2.2 without params of its own got the defaults on load".into(),
        );
    }
}

/// A pre-1.5 song's synth clips with an unsynced arpeggiator: their arpeggiator rate was raised to keep its speed.
pub fn arpeggiator_rate(doc: &mut Node, _ctx: &mut Context) {
    let Some(song) = doc.get_mut("song") else {
        return;
    };
    for (_, clip) in song
        .get_mut("sessionClips")
        .and_then(Node::entries_mut)
        .into_iter()
        .flatten()
    {
        let arpeggiating = clip.get("arpeggiator").is_some_and(|arp| {
            let mode = arp.get("mode").and_then(Node::text).unwrap_or("off");
            mode != "off" && arp.get("syncLevel").and_then(Node::int) == Some(0)
        });
        if !arpeggiating {
            continue;
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
    }
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
    let mut file = level - 1 + magnitude;
    if file == 0 {
        if level == 1 {
            file = -1; // reads back clamped to 1
        } else {
            ctx.note(
                "swing interval",
                format!("level {level} cannot be written at tick magnitude {magnitude}"),
            );
            return;
        }
    }
    song.set("swingInterval", Node::Text(file.to_string()));
}
