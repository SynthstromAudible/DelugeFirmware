//! Sample zones and sample playback in the forms official firmware before 2.1 used.

use super::sound::for_each_sound;
use crate::tree::Node;
use crate::upgrade::Context;
use crate::version::FirmwareVersion;
use std::path::{Path, PathBuf};

fn decimal(node: Option<&Node>) -> i64 {
    let text = node.and_then(Node::text).unwrap_or("");
    let (negative, rest) = match text.strip_prefix('-') {
        Some(rest) => (true, rest),
        None => (false, text),
    };
    let digits: String = rest.chars().take_while(char::is_ascii_digit).collect();
    let n: i64 = digits.parse().unwrap_or(0);
    if negative { -n } else { n }
}

/// A sample's length in frames and its rate, as the firmware works them out from its header.
struct SampleFacts {
    length: u64,
    rate: u64,
}

/// The file a sample path names on the card: as written, or, the card being FAT, ignoring case.
fn find_on_card(card: &Path, path: &str) -> Option<PathBuf> {
    let mut at = card.to_path_buf();
    for part in path.split(['/', '\\']).filter(|p| !p.is_empty()) {
        let exact = at.join(part);
        if exact.exists() {
            at = exact;
            continue;
        }
        let entry = std::fs::read_dir(&at)
            .ok()?
            .filter_map(Result::ok)
            .find(|e| e.file_name().to_string_lossy().eq_ignore_ascii_case(part))?;
        at = entry.path();
    }
    at.is_file().then_some(at)
}

fn sample_facts(card: &Path, path: &str) -> Option<SampleFacts> {
    let file = find_on_card(card, path)?;
    let bytes = std::fs::read(&file).ok()?;
    let header = deluge_audio_format::parse(&bytes).ok()?;
    let frame = u64::from(header.byte_depth) * u64::from(header.num_channels);
    if frame == 0 {
        return None;
    }
    let available =
        (bytes.len() as u64).saturating_sub(u64::from(header.audio_data_start_pos_bytes));
    let data = u64::from(header.audio_data_length_bytes).min(available);
    Some(SampleFacts {
        length: data / frame,
        rate: u64::from(header.sample_rate),
    })
}

/// One zone: its milliseconds (seconds folded in) and positions, read as the firmware reads a zone.
fn zone_times(zone: &Node) -> (i64, i64) {
    let start = decimal(zone.get("startSeconds")) * 1000 + decimal(zone.get("startMilliseconds"));
    let end = decimal(zone.get("endSeconds")) * 1000 + decimal(zone.get("endMilliseconds"));
    (start, end)
}

fn has_times(zone: &Node) -> bool {
    [
        "startSeconds",
        "startMilliseconds",
        "endSeconds",
        "endMilliseconds",
    ]
    .iter()
    .any(|k| zone.get(k).is_some())
}

fn drop_times(zone: &mut Node) {
    for key in [
        "startSeconds",
        "startMilliseconds",
        "endSeconds",
        "endMilliseconds",
    ] {
        while zone.remove(key).is_some() {}
    }
}

/// A zone in milliseconds, before official 2.1: once its sample loaded, the firmware made positions of it.
fn convert_zone(zone: &mut Node, file: &str, reversed: bool, ctx: &mut Context) {
    // A source with no file loads no sample: nothing converted its zone, and a current file's milliseconds are unread.
    if file.is_empty() {
        drop_times(zone);
        return;
    }
    let (start_ms, end_ms) = zone_times(zone);
    let Some(card) = ctx.card.clone() else {
        ctx.note(
            "millisecond zone",
            format!("{file}: no card given, so its zone stays in milliseconds"),
        );
        return;
    };
    let Some(facts) = sample_facts(&card, file) else {
        ctx.note(
            "millisecond zone",
            format!("{file}: not found on the card, so its zone stays in milliseconds"),
        );
        return;
    };
    let len = facts.length;
    let mut start = (decimal(zone.get("startSamplePos")).max(0) as u64).min(len);
    let mut end = decimal(zone.get("endSamplePos")).max(0) as u64;
    if end == 0 || end == 9_999_999 {
        end = len;
    }
    if end <= start {
        start = 0;
    }
    let mut converted = false;
    if start_ms != 0 && start == 0 {
        start = start_ms as u64 * facts.rate / 1000;
        converted = true;
    }
    if end_ms != 0 && (end == 0 || end == len) {
        end = end_ms as u64 * facts.rate / 1000;
        if end > len && end <= len + 45 {
            end = len;
        } else if end > len {
            ctx.note(
                "millisecond zone",
                format!(
                    "{file}: its end is {} frames past the sample's end",
                    end - len
                ),
            );
        }
        converted = true;
    }
    if converted && reversed {
        let old_start = start;
        start = len.saturating_sub(end);
        end = len.saturating_sub(old_start);
    }
    drop_times(zone);
    zone.set("startSamplePos", Node::Text(start.to_string()));
    zone.set("endSamplePos", Node::Text(end.to_string()));
}

/// Every zone of a sound's sample sources, with the file it plays and whether the source plays it reversed.
fn for_each_zone(sound: &mut Node, f: &mut dyn FnMut(&mut Node, &str, bool)) {
    for osc in ["osc1", "osc2"] {
        let Some(source) = sound.get_mut(osc) else {
            continue;
        };
        // Only a sample source keeps its zones: the firmware drops any other source's.
        if !is_sample(source) {
            continue;
        }
        let reversed = decimal(source.get("reversed")) != 0;
        let file = source
            .get("fileName")
            .and_then(Node::text)
            .unwrap_or("")
            .to_string();
        if let Some(zone) = source.get_mut("zone") {
            f(zone, &file, reversed);
        }
        if let Some(ranges) = source.get_mut("sampleRanges").and_then(Node::entries_mut) {
            for (_, range) in ranges.iter_mut() {
                let file = range
                    .get("fileName")
                    .and_then(Node::text)
                    .unwrap_or("")
                    .to_string();
                if let Some(zone) = range.get_mut("zone") {
                    f(zone, &file, reversed);
                }
            }
        }
    }
}

/// Zones in milliseconds: converted to positions for a file older than official 2.1; dropped unread for a newer one,
/// as the firmware ignores them.
pub fn millisecond_zones(doc: &mut Node, ctx: &mut Context) {
    let before_2_1 = ctx.version < FirmwareVersion::official_tagged(2, 1, 0, "beta");
    let mut zones: Vec<(String, bool)> = Vec::new();
    // First pass collects; the conversion needs the context mutably, so it runs per zone through a pointer-free walk.
    for_each_sound(doc, &mut |sound| {
        for_each_zone(sound, &mut |zone, file, reversed| {
            if has_times(zone) {
                zones.push((file.to_string(), reversed));
            }
        });
    });
    let mut pending = zones.into_iter();
    for_each_sound(doc, &mut |sound| {
        for_each_zone(sound, &mut |zone, _, _| {
            if !has_times(zone) {
                return;
            }
            let Some((file, reversed)) = pending.next() else {
                return;
            };
            if before_2_1 {
                convert_zone(zone, &file, reversed, ctx);
            } else {
                drop_times(zone);
            }
        });
    });
}

fn is_sample(source: &Node) -> bool {
    source.get("type").and_then(Node::text) == Some("sample")
}

/// Before official 2.1 a song's synths played samples with linear interpolation, and a kit's drums did when tuned away
/// from their sample's pitch (`song.cpp`, `instrument_clip.cpp`).
pub fn linear_interpolation(doc: &mut Node, ctx: &mut Context) {
    let Some(song) = doc.get_mut("song") else {
        return;
    };
    let Some(instruments) = song.get_mut("instruments").and_then(Node::entries_mut) else {
        return;
    };
    let mut reports = Vec::new();
    for (kind, instrument) in instruments.iter_mut() {
        match kind.as_str() {
            "sound" => {
                for osc in ["osc1", "osc2"] {
                    if let Some(source) = instrument.get_mut(osc)
                        && is_sample(source)
                    {
                        source.set("linearInterpolation", Node::Text("1".into()));
                    }
                }
            }
            "kit" => {
                let Some(drums) = instrument
                    .get_mut("soundSources")
                    .and_then(Node::entries_mut)
                else {
                    continue;
                };
                for (key, drum) in drums.iter_mut() {
                    if key != "sound" {
                        continue;
                    }
                    let drum_tuned = decimal(drum.get("transpose")) != 0;
                    for osc in ["osc1", "osc2"] {
                        if let Some(source) = drum.get_mut(osc)
                            && is_sample(source)
                        {
                            // A sample source's own transpose and cents were folded into its ranges before
                            // the clips looked, so only the drum's counts.
                            if drum_tuned {
                                source.set("linearInterpolation", Node::Text("1".into()));
                            } else {
                                reports.push(
                                    "a kit drum's sample interpolation also depended on its clips' pitch params, \
                                     which the upgrader does not read"
                                        .to_string(),
                                );
                            }
                        }
                    }
                }
            }
            _ => {}
        }
    }
    if !reports.is_empty() {
        ctx.note("linear interpolation", reports.remove(0));
    }
}
