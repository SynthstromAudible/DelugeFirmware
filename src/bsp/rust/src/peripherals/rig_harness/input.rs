//! The rig harness's down-lines: script gestures from the host, one ASCII line each on RTT down
//! channel 0.
//!
//! | Line | Gesture |
//! |---|---|
//! | `B <x> <y> <0\|1>` | button at matrix (x, y), as the app's button events carry it, up or down |
//! | `P <x> <y> <0\|1> [vel]` | pad at grid (x, y) released or pressed; velocity 255 if omitted |
//! | `E <idx> <edges>` | quadrature edges on the app's encoder ordinal `idx`, signed |
//! | `N` | no presses |
//! | `S` | resend the whole panel, then `@R` |

/// The longest line accepted.
pub const MAX_LINE: usize = 64;

/// Button matrix columns and rows (the PIC's button id is `x + y * 9`).
const BUTTON_COLS: u8 = 9;
const BUTTON_ROWS: u8 = 4;
/// Grid columns (main pads and sidebar) and rows.
const PAD_COLS: u8 = 18;
const PAD_ROWS: u8 = 8;
/// Encoders, by the app's ordinal.
const ENCODERS: u8 = 6;
/// The velocity a PIC press carries.
pub const DEFAULT_VELOCITY: u8 = 255;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Command {
    Button { x: u8, y: u8, down: bool },
    Pad { x: u8, y: u8, down: bool, vel: u8 },
    Encoder { idx: u8, edges: i32 },
    NoPresses,
    Resend,
}

/// Why a line was refused.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ParseError;

fn field<T: core::str::FromStr>(f: Option<&str>) -> Result<T, ParseError> {
    f.ok_or(ParseError)?.parse().map_err(|_| ParseError)
}

fn below(v: u8, limit: u8) -> Result<u8, ParseError> {
    if v < limit { Ok(v) } else { Err(ParseError) }
}

fn flag(f: Option<&str>) -> Result<bool, ParseError> {
    match f {
        Some("1") => Ok(true),
        Some("0") => Ok(false),
        _ => Err(ParseError),
    }
}

pub fn parse(line: &str) -> Result<Command, ParseError> {
    let line = line.trim_end_matches(['\r', '\n']);
    if line.len() > MAX_LINE {
        return Err(ParseError);
    }
    let mut f = line.split(' ');
    let cmd = match f.next() {
        Some("B") => Command::Button {
            x: below(field(f.next())?, BUTTON_COLS)?,
            y: below(field(f.next())?, BUTTON_ROWS)?,
            down: flag(f.next())?,
        },
        Some("P") => Command::Pad {
            x: below(field(f.next())?, PAD_COLS)?,
            y: below(field(f.next())?, PAD_ROWS)?,
            down: flag(f.next())?,
            vel: match f.next() {
                Some(v) => v.parse().map_err(|_| ParseError)?,
                None => DEFAULT_VELOCITY,
            },
        },
        Some("E") => Command::Encoder {
            idx: below(field(f.next())?, ENCODERS)?,
            edges: field(f.next())?,
        },
        Some("N") => Command::NoPresses,
        Some("S") => Command::Resend,
        _ => return Err(ParseError),
    };
    match f.next() {
        None => Ok(cmd),
        Some(_) => Err(ParseError),
    }
}
