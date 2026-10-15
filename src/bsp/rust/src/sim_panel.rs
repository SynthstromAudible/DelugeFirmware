//! The emulator's (`dbt sim`) panel backend: the shared panel the simulator window renders, plugged
//! into the BSP through [`deluge_bsp::host_panel`]. Adapted from deluge-sdk's
//! `crates/deluge-sdk/src/plat/sim_panel.rs` (MIT OR Apache-2.0); deluge-sdk keeps its copy private
//! to its facade, and this image builds on the BSP directly.
//!
//! On the device the PIC co-processor drives the pads and LEDs, and the OLED is on its own SPI bus. On the host
//! the BSP's peripheral modules call the installed [`deluge_bsp::host_panel::HostPanel`] instead, and this
//! module keeps the shared panel ([`deluge_sim_link::SharedPanel`]):
//!
//! - every outbound PIC command ([`deluge_bsp::pic`]'s `tx`) is applied as the PIC would apply it. Pad colours go
//!   into a virtual PIC framebuffer, which a refresh (`DONE_SENDING_ROWS`) publishes, and the smooth-scroll
//!   commands shift that framebuffer as the PIC's own does;
//! - every OLED frame ([`deluge_bsp::oled`]'s host capture) is copied to the panel;
//! - the panel's input comes back as the PIC would send it: pad and button events as PIC bytes through
//!   [`deluge_bsp::pic::read_byte`], for the program's own PIC parser, and encoder turns as edges in
//!   [`deluge_bsp::encoder::ENCODER_DELTAS`], as the encoder interrupt records them.
//!
//! So a program drives the simulator through the same BSP calls it makes on the device. [`install`] is called
//! once, by the host runtime (inside `deluge_simulator::run_brain`), before the program starts; until then
//! the BSP's host arms stay the no-ops they otherwise are.

use std::collections::VecDeque;
use std::sync::atomic::Ordering;
use std::sync::{Mutex, OnceLock, PoisonError};

use deluge_sim_link::audio::BrainEnds;
use deluge_sim_link::{InputEvent, SharedPanel};

use deluge_bsp::rgb::{COLS, ROWS};

static PANEL: OnceLock<SharedPanel> = OnceLock::new();
static AUDIO: Mutex<Option<BrainEnds>> = Mutex::new(None);

/// Install the simulator's panel and the program's ends of the audio bridge, and plug the panel into
/// [`deluge_bsp::host_panel`]. Called once, before the program runs; a second call is ignored.
pub fn install(panel: SharedPanel, audio: BrainEnds) {
    if PANEL.set(panel).is_ok() {
        *AUDIO.lock().unwrap_or_else(PoisonError::into_inner) = Some(audio);
        deluge_bsp::host_panel::install(&SimPanel);
    }
}

/// The simulator's side of [`deluge_bsp::host_panel`].
struct SimPanel;

impl deluge_bsp::host_panel::HostPanel for SimPanel {
    fn display(&self, frame: &[u8]) {
        display(frame);
    }
    fn pic_command(&self, bytes: &[u8]) {
        pic_command(bytes);
    }
    fn next_pic_byte(&self) -> Option<u8> {
        next_input_byte()
    }
}

/// The installed panel, if a simulator is running.
pub fn panel() -> Option<&'static SharedPanel> {
    PANEL.get()
}

pub use deluge_sim_link::audio::{BLOCK_FRAMES, Sample};

/// Exchange one block with the simulator's audio device, if its output has room for the block: `render` is handed
/// the block's input (the frames the input device captured, silence where it has fallen behind) and replaces it with
/// the block's output. Returns whether it rendered.
///
/// The simulator's output device plays at the codec's rate, so a full output is the program's clock: call this
/// until it returns `false`, then wait, as the device's audio drive renders only as far as the DMA play head has
/// moved. Always `false` with no simulator installed.
pub fn render_block(render: impl FnOnce(&mut [Sample; BLOCK_FRAMES])) -> bool {
    let mut audio = AUDIO.lock().unwrap_or_else(PoisonError::into_inner);
    audio.as_mut().is_some_and(|ends| exchange(ends, render))
}

fn exchange(ends: &mut BrainEnds, render: impl FnOnce(&mut [Sample; BLOCK_FRAMES])) -> bool {
    use deluge_sim_link::audio::{Consumer, Observer, Producer};

    if ends.out.vacant_len() < BLOCK_FRAMES {
        return false;
    }
    let mut block = [[0.0; 2]; BLOCK_FRAMES];
    for frame in &mut block {
        *frame = ends.in_.try_pop().unwrap_or([0.0, 0.0]);
    }
    render(&mut block);
    ends.out.push_slice(&block);
    true
}

/// The OLED's frame, 768 bytes page-major, as [`deluge_bsp::oled::FrameBuffer::as_bytes`] lays it out.
fn display(frame: &[u8]) {
    if let Some(panel) = panel() {
        panel.set_display(frame);
    }
}

// ── The virtual PIC ───────────────────────────────────────────────────────────

use deluge_bsp::pic::{
    CMD_DONE_SENDING_ROWS as DONE_SENDING_ROWS, CMD_LED_OFF_BASE as LED_OFF_BASE,
    CMD_LED_ON_BASE as LED_ON_BASE, CMD_SET_COLOUR_FOR_COLS_BASE as COLS_BASE,
    CMD_SET_GOLD_KNOB_0_INDICATORS as GOLD_KNOB_0, CMD_SET_GOLD_KNOB_1_INDICATORS as GOLD_KNOB_1,
    CMD_SET_SCROLL_DOWN as SCROLL_DOWN, CMD_SET_SCROLL_HORIZONTAL_BASE as SCROLL_HORIZONTAL_BASE,
    CMD_SET_SCROLL_ROW_BASE as SCROLL_ROW_BASE, CMD_SET_SCROLL_UP as SCROLL_UP,
};

/// The last byte of each ranged command: a base plus its largest argument.
const COLS_LAST: u8 = COLS_BASE + 8; // 9 column pairs
const LED_OFF_LAST: u8 = LED_OFF_BASE + 35; // 36 LEDs
const LED_ON_LAST: u8 = LED_ON_BASE + 35;
const SCROLL_ROW_LAST: u8 = SCROLL_ROW_BASE + 7; // 8 rows
const SCROLL_HORIZONTAL_LAST: u8 = SCROLL_HORIZONTAL_BASE + 3; // 2 flag bits

/// The PIC's pad framebuffer, `[col][row]`, and the horizontal scroll the last setup command started.
struct Pic {
    grid: [[[u8; 3]; ROWS]; COLS],
    /// `(direction, columns)`: +1 to scroll right, -1 left, over 16 or 18 columns.
    scroll: (i8, usize),
}

static PIC: Mutex<Pic> = Mutex::new(Pic {
    grid: [[[0; 3]; ROWS]; COLS],
    scroll: (1, 16),
});

/// Apply one outbound PIC command, as `pic::tx` sends it: exactly one whole command per call.
fn pic_command(bytes: &[u8]) {
    let Some(panel) = panel() else {
        return;
    };
    let Some((&cmd, args)) = bytes.split_first() else {
        return;
    };
    let mut pic = PIC.lock().unwrap_or_else(PoisonError::into_inner);
    match cmd {
        // Two columns' colours: the first 8 entries are column 2·pair, the next 8 column 2·pair+1.
        COLS_BASE..=COLS_LAST => {
            let pair = usize::from(cmd - COLS_BASE);
            for (i, rgb) in args.chunks_exact(3).take(16).enumerate() {
                let col = pair * 2 + i / 8;
                if col < COLS {
                    pic.grid[col][i % 8].copy_from_slice(rgb);
                }
            }
        }
        DONE_SENDING_ROWS => panel.set_all_pads(&all_pads(&pic.grid)),
        LED_OFF_BASE..=LED_OFF_LAST => panel.set_led(usize::from(cmd - LED_OFF_BASE), false),
        LED_ON_BASE..=LED_ON_LAST => panel.set_led(usize::from(cmd - LED_ON_BASE), true),
        GOLD_KNOB_0 | GOLD_KNOB_1 => {
            if let Ok(levels) = <[u8; 4]>::try_from(args) {
                panel.set_knob_indicator(usize::from(cmd - GOLD_KNOB_0), levels);
            }
        }
        SCROLL_HORIZONTAL_BASE..=SCROLL_HORIZONTAL_LAST => {
            let flags = cmd - SCROLL_HORIZONTAL_BASE;
            pic.scroll = (
                if flags & 1 != 0 { 1 } else { -1 },
                if flags & 2 != 0 { COLS } else { 16 },
            );
        }
        // One row of a horizontal scroll: shift that row a square and bring the colour in at the far end.
        SCROLL_ROW_BASE..=SCROLL_ROW_LAST => {
            let row = usize::from(cmd - SCROLL_ROW_BASE);
            if let Ok(rgb) = <[u8; 3]>::try_from(args) {
                let (direction, columns) = pic.scroll;
                shift_row(&mut pic.grid, row, direction, columns, rgb);
            }
        }
        // A vertical scroll: every row moves one step, and `args` is the incoming row, one colour per column.
        SCROLL_UP | SCROLL_DOWN => {
            let up = cmd == SCROLL_UP;
            for col in 0..COLS {
                if up {
                    pic.grid[col].copy_within(1.., 0);
                } else {
                    pic.grid[col].copy_within(..ROWS - 1, 1);
                }
                let incoming = args
                    .get(col * 3..col * 3 + 3)
                    .map_or([0; 3], |c| [c[0], c[1], c[2]]);
                pic.grid[col][if up { ROWS - 1 } else { 0 }] = incoming;
            }
            panel.set_all_pads(&all_pads(&pic.grid));
        }
        // Refresh time, OLED select/DC handshakes, version and button-state requests: nothing to show.
        _ => {}
    }
}

/// Shift `row` one square in `direction` across the first `columns` columns, as the app's own image moves
/// (`pad_leds.cpp`'s `horizontal::renderScroll`), and put `incoming` in the square that opens up.
fn shift_row(
    grid: &mut [[[u8; 3]; ROWS]; COLS],
    row: usize,
    direction: i8,
    columns: usize,
    incoming: [u8; 3],
) {
    if direction > 0 {
        for x in 0..columns - 1 {
            grid[x][row] = grid[x + 1][row];
        }
        grid[columns - 1][row] = incoming;
    } else {
        for x in (1..columns).rev() {
            grid[x][row] = grid[x - 1][row];
        }
        grid[0][row] = incoming;
    }
}

/// The grid as the panel's `set_all_pads` takes it: column-major `[r, g, b]`, `(col·ROWS + row)·3`.
fn all_pads(grid: &[[[u8; 3]; ROWS]; COLS]) -> [u8; deluge_sim_link::ALL_PADS_BYTES] {
    let mut buf = [0u8; deluge_sim_link::ALL_PADS_BYTES];
    for (col, rows) in grid.iter().enumerate() {
        for (row, rgb) in rows.iter().enumerate() {
            let o = (col * ROWS + row) * 3;
            buf[o..o + 3].copy_from_slice(rgb);
        }
    }
    buf
}

// ── Input ─────────────────────────────────────────────────────────────────────

/// The PIC's byte before a pad or button id that marks it released (`pic::Parser`'s `RESP_NEXT_PAD_OFF`).
const NEXT_IS_RELEASE: u8 = 252;
/// Button ids start here in the PIC's byte stream (`pic::Parser`: 144..=179).
const BUTTON_BASE: u8 = 144;

/// Panel input already encoded as PIC bytes, waiting to be read.
static INPUT_BYTES: Mutex<VecDeque<u8>> = Mutex::new(VecDeque::new());

fn next_input_byte() -> Option<u8> {
    let panel = panel()?;
    let mut bytes = INPUT_BYTES.lock().unwrap_or_else(PoisonError::into_inner);
    while bytes.is_empty() {
        encode_input(panel.pop_event()?, &mut bytes);
    }
    bytes.pop_front()
}

/// Queue `event` as the PIC's bytes. An encoder turn has no PIC bytes (the encoders are wired to the CPU, not the
/// PIC): it becomes edges on its accumulator instead, four per detent as the decoder counts them, and wakes the
/// encoder task as the interrupt does.
fn encode_input(event: InputEvent, bytes: &mut VecDeque<u8>) {
    match event {
        InputEvent::Pad { x, y, pressed } => {
            if !pressed {
                bytes.push_back(NEXT_IS_RELEASE);
            }
            bytes.push_back(deluge_bsp::pic::pad_id(x, y));
        }
        InputEvent::Button { id, pressed } => {
            if !pressed {
                bytes.push_back(NEXT_IS_RELEASE);
            }
            bytes.push_back(BUTTON_BASE + id);
        }
        InputEvent::Encoder { index, delta } => {
            if let Some(edges) = deluge_bsp::encoder::ENCODER_DELTAS.get(usize::from(index)) {
                let _ = edges.try_update(Ordering::Relaxed, Ordering::Relaxed, |e| {
                    Some(e.saturating_add(delta.saturating_mul(4)))
                });
                deluge_bsp::encoder::ENCODER_WAKER.wake();
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_horizontal_scroll_row_shifts_left_and_enters_on_the_right() {
        let mut grid = [[[0u8; 3]; ROWS]; COLS];
        for (x, col) in grid.iter_mut().enumerate() {
            col[2] = [x as u8, 0, 0];
        }
        shift_row(&mut grid, 2, 1, 16, [99, 0, 0]);
        assert_eq!(grid[0][2], [1, 0, 0]);
        assert_eq!(grid[14][2], [15, 0, 0]);
        assert_eq!(grid[15][2], [99, 0, 0]);
        assert_eq!(
            grid[16][2],
            [16, 0, 0],
            "the sidebar is outside a 16-column scroll"
        );
    }

    /// Encode `event` and decode it with the program's own parser.
    fn round_trip(event: InputEvent) -> Vec<deluge_bsp::pic::Event> {
        let mut bytes = VecDeque::new();
        encode_input(event, &mut bytes);
        let mut parser = deluge_bsp::pic::Parser::new();
        bytes.into_iter().filter_map(|b| parser.push(b)).collect()
    }

    #[test]
    fn every_pad_press_and_release_decodes_to_its_own_coordinates() {
        use deluge_bsp::pic::{Event, pad_coords};
        for x in 0..18u8 {
            for y in 0..8u8 {
                for pressed in [true, false] {
                    let events = round_trip(InputEvent::Pad { x, y, pressed });
                    let [event] = events[..] else {
                        panic!("pad ({x},{y}) decoded to {events:?}");
                    };
                    let id = match event {
                        Event::PadPress { id } if pressed => id,
                        Event::PadRelease { id } if !pressed => id,
                        other => panic!("pad ({x},{y}) pressed={pressed} decoded to {other:?}"),
                    };
                    assert_eq!(pad_coords(id), (x, y));
                }
            }
        }
    }

    #[test]
    fn a_button_decodes_to_its_id() {
        use deluge_bsp::pic::Event;
        assert_eq!(
            round_trip(InputEvent::Button {
                id: 25,
                pressed: true
            }),
            [Event::ButtonPress { id: 25 }]
        );
        assert_eq!(
            round_trip(InputEvent::Button {
                id: 35,
                pressed: false
            }),
            [Event::ButtonRelease { id: 35 }]
        );
    }

    #[test]
    fn an_encoder_turn_adds_four_edges_a_detent() {
        use deluge_bsp::encoder::ENCODER_DELTAS;
        ENCODER_DELTAS[3].store(0, Ordering::Relaxed);
        assert!(
            round_trip(InputEvent::Encoder {
                index: 3,
                delta: -2
            })
            .is_empty()
        );
        assert_eq!(ENCODER_DELTAS[3].swap(0, Ordering::Relaxed), -8);
    }

    #[test]
    fn a_leftward_scroll_row_enters_on_the_left() {
        let mut grid = [[[0u8; 3]; ROWS]; COLS];
        for (x, col) in grid.iter_mut().enumerate() {
            col[0] = [x as u8, 0, 0];
        }
        shift_row(&mut grid, 0, -1, 18, [77, 0, 0]);
        assert_eq!(grid[0][0], [77, 0, 0]);
        assert_eq!(grid[1][0], [0, 0, 0]);
        assert_eq!(grid[17][0], [16, 0, 0]);
    }

    #[test]
    fn a_block_renders_its_input_into_the_output_while_the_output_has_room() {
        use deluge_sim_link::audio::{Consumer, Observer, Producer};

        let (mut brain, mut gui) = deluge_sim_link::audio::new_bridge();
        // Half a block of input: the rest of the block reads as silence.
        for n in 0..BLOCK_FRAMES / 2 {
            gui.in_.try_push([n as f32, -(n as f32)]).unwrap();
        }
        assert!(exchange(&mut brain, |block| {
            for s in block.iter_mut() {
                *s = [s[0] * 2.0, s[1] * 2.0];
            }
        }));
        let out: Vec<Sample> = gui.out.pop_iter().collect();
        assert_eq!(out.len(), BLOCK_FRAMES);
        assert_eq!(out[3], [6.0, -6.0]);
        assert_eq!(out[BLOCK_FRAMES - 1], [0.0, 0.0]);

        // Fill the output: the next block has no room, and is not rendered.
        while exchange(&mut brain, |_| {}) {}
        assert!(brain.out.vacant_len() < BLOCK_FRAMES);
        assert!(!exchange(&mut brain, |_| panic!(
            "rendered into a full output"
        )));
    }
}
