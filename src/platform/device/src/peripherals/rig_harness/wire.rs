//! The rig harness's up-lines: each panel change as one text line on RTT channel 0, between the
//! log lines. A line is `@<kind> <seq> <us> ...`: `seq` counts every `@` line, so the host sees a
//! gap when RTT drops one, and `us` is microseconds since boot.
//!
//! | Kind | Fields after `seq us` |
//! |---|---|
//! | `O` | width, height, the frame in base64 (SSD1309 page layout) |
//! | `C` | column pair index, 16 pads × RGB in base64 |
//! | `L` | LED index, 0 or 1 |
//! | `G` | knob, four levels |
//! | `F` | pad index (`y + x * 8`), PIC colour index (0 = current) |
//! | `M` | dimmer interval, refresh time |
//! | `X` | — (the PIC scrolled pads itself) |
//! | `T` | — (heartbeat) |
//! | `D` | flashes the shadow could not hold |
//! | `R` | — (ready; follows a full state) |
//! | `E` | the command line that could not be applied |

use core::fmt::{self, Write};

use super::shadow::{COLUMN_PAIRS, Change, LED_COUNT, Shadow};

/// The longest line: an OLED frame's base64 plus its header.
pub const MAX_LINE: usize = 1088;

/// Standard base64 with padding: a third smaller than hex on a link that is the harness's bottleneck.
fn base64(bytes: &[u8], out: &mut impl Write) -> fmt::Result {
    const ALPHABET: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for chunk in bytes.chunks(3) {
        let b = [
            chunk[0],
            *chunk.get(1).unwrap_or(&0),
            *chunk.get(2).unwrap_or(&0),
        ];
        let n = (b[0] as u32) << 16 | (b[1] as u32) << 8 | b[2] as u32;
        for i in 0..4 {
            if i <= chunk.len() {
                out.write_char(ALPHABET[(n >> (18 - 6 * i)) as usize & 63] as char)?;
            } else {
                out.write_char('=')?;
            }
        }
    }
    Ok(())
}

/// Writes the line for `change`, with the shadow's current value.
pub fn encode(change: &Change, s: &Shadow, seq: u32, us: u64, out: &mut impl Write) -> fmt::Result {
    match *change {
        Change::Oled => {
            let (frame, (w, h)) = s.oled();
            let n = (w as usize * h as usize / 8).min(frame.len());
            write!(out, "@O {seq} {us} {w} {h} ")?;
            base64(&frame[..n], out)?;
        }
        Change::Columns(idx) => {
            write!(out, "@C {seq} {us} {idx} ")?;
            base64(s.columns(idx), out)?;
        }
        Change::Led(id) => write!(out, "@L {seq} {us} {id} {}", s.led(id) as u8)?,
        Change::Ring(knob) => {
            let [a, b, c, d] = s.ring(knob);
            write!(out, "@G {seq} {us} {knob} {a} {b} {c} {d}")?;
        }
        Change::Flash(idx, colour) => write!(out, "@F {seq} {us} {idx} {colour}")?,
        Change::Pic => {
            let (dimmer, refresh) = s.pic();
            write!(out, "@M {seq} {us} {dimmer} {refresh}")?;
        }
        Change::Scroll => write!(out, "@X {seq} {us}")?,
    }
    out.write_char('\n')
}

pub fn heartbeat(seq: u32, us: u64, out: &mut impl Write) -> fmt::Result {
    writeln!(out, "@T {seq} {us}")
}

pub fn dropped(seq: u32, us: u64, n: u32, out: &mut impl Write) -> fmt::Result {
    writeln!(out, "@D {seq} {us} {n}")
}

pub fn ready(seq: u32, us: u64, out: &mut impl Write) -> fmt::Result {
    writeln!(out, "@R {seq} {us}")
}

pub fn error(seq: u32, us: u64, line: &str, out: &mut impl Write) -> fmt::Result {
    writeln!(out, "@E {seq} {us} {line}")
}

/// Emits the whole panel, one line per OLED, column pair, LED, ring and the PIC settings, each
/// numbered by `next_seq`.
pub fn full_state(
    s: &Shadow,
    us: u64,
    mut next_seq: impl FnMut() -> u32,
    mut emit: impl FnMut(&str),
) -> fmt::Result {
    let changes = core::iter::once(Change::Oled)
        .chain((0..COLUMN_PAIRS as u8).map(Change::Columns))
        .chain((0..LED_COUNT as u8).map(Change::Led))
        .chain((0..2).map(Change::Ring))
        .chain(core::iter::once(Change::Pic));
    for change in changes {
        let mut line: heapless::String<MAX_LINE> = heapless::String::new();
        encode(&change, s, next_seq(), us, &mut line)?;
        emit(&line);
    }
    Ok(())
}
