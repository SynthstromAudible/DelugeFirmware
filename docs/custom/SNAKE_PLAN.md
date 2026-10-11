# Native MDD-inspired Snake sequencer

This build integrates an original native sequencer into InstrumentClip. It is not the Max for Live patch. Synth, MIDI and CV clips are supported; Kit clips are excluded.

## Mode selection and lifecycle

The clip's OLED Sequencer menu selects Normal or MDD Snake. There is no MDD button-combination shortcut. SHIFT + SELECT retains the stock system-menu behavior. While MDD is active, SELECT opens its OLED settings: choose Sequencer and press SELECT to open the native mode selector. BACK on the MDD main screen opens the normal clip/synth menu without changing the playing mode. Running switches occur on the next 1/16 boundary and close outgoing notes. Stopped switches are immediate. Stock notes are retained. Stock note position/repeat/direction and the independent Snake tick are retained when switching, so returning resumes the previous mode. Clip automation and song transport continue in both modes.

Seven independent 16-cell lanes run simultaneously: Notes, Gates, Velocity, Custom 1–4. Selecting a lane only changes the editor. Clip settings, values, mutes, ranges, traversal, routes and Snake position serialize in mddSnakeV1. Clone copies these settings into independent storage. This custom format requires this build to interpret it; official firmware ignores it.

## Main pads and OLED

The upper-left 4x4 main pads are cells 1–16, read across each row from the top. Physical coordinates x=0..3, y=4..7. The row directly underneath, from left to right, is:

| Pad | Control |
| --- | --- |
| 1 | Notes, yellow |
| 2 | Gates, blue |
| 3 | Velocity, green |
| 4 | Custom, red |
| 5 | Random current lane, purple |
| 6 | Reset current lane values |
| 7 | Random all lanes |
| 8 | Reset all lane values |
| 9 | Song Root |
| 10 | Song Scale |
| 11 | Custom Map |
| 12 | Random Range |

Notes/Gates/Velocity switch immediately and display their name. Other control pads show their name while held. Hold the control and press/release SELECT to execute. Custom: hold its pad and SELECT, then turn SELECT to choose 1–4. Root/Scale use the same gesture and edit Deluge's shared song root/scale, enabling scale mode.

Short cell taps toggle that lane's mute. Hold a cell at least 350ms and turn SELECT to edit its value; releasing it does not also mute it. Hold cell 1 and press cell N to set the active lane's length to N. The last pressed cell wins. Length 1 is available in OLED settings.

With no control held, press SELECT to open settings. Turn to select Direction, Speed, Steps, Shift, Shape or Sequencer; press SELECT to edit, turn to change, press to return. BACK returns to the main display. Directions: Forward, Reverse, Pendulum, Random. Shapes: Rows, Columns, Snake Rows, Snake Columns, Spiral. Reverse applies to every shape. Random traversal can repeat cells. Speed: 1/4, 1/8, 1/16, 1/32, 1/32T, 1/64; fractional host ticks use rational timing, bounded by Deluge's tick resolution.

The selected lane's active cell lights, including muted cells. Other lanes continue without being shown. Selected-step editing, lane title, traversal, speed and settings appear on OLED.

## Native value and modulation bridge

Notes use MIDI note numbers 0–127, not a limited local scale index: 60 is C4 in MIDI notation. Deluge's own display names/octave preference applies. In scale mode, values quantize to the nearest pitch in the song's current key/scale (downward tie). Gates are note lengths relative to the current Notes step, not envelope decay/sustain. Gate zero and velocity zero are silent. Gate 100% lasts one Notes step; the normal synth envelope release follows note-off. Tick resolution imposes a minimum one-tick note length. Notes/Gates/Velocity mute suppresses new notes; Custom mute holds the previous output.

Gate and Custom values have unsigned 16-bit internal precision and normally display percentages. Notes/Velocity use 0–127. Custom values normalize to Deluge Q31 source values and enter native patch cables. No destination is pre-wired. Each Custom lane has four editor route slots; additional cables can be edited through the normal mod matrix. Only the clip which currently owns the synth's MDD source values can clear them; stopping an inactive clip must not reset another clip's Custom output. The four Snake sources have the same allowed/editable destination coverage as Envelope 1–3; supported global effect destinations remain available. Non-patchable effect settings are not made patchable by this feature.

Hold Map and press/release SELECT, then turn to choose slot; press SELECT to target, turn to choose the native parameter; press to depth, turn to change signed strength. BACK exits. Range selects random minimum/maximum via SELECT and rotation. Random respects that lane's range. Reset changes values, retaining mutes/traversal/routes. MIDI/CV note output is supported; Custom parameter routing is synth-only. There is no generated-note recording, undo integration or Kit sequencing yet.

## Plaits oscillator

Oscillator Source/Type offers one Plaits entry in Subtractive mode. Selecting it exposes Engine (also present in its oscillator submenu): 1 FM, 2 Wave, 3 VA. Engine is a real patched parameter for oscillator 1 or 2, supporting normal modulation sources and Snake Custom. Existing presets with the three previous Plaits type names still load.

For direct step selection, set the base Engine to 1, map a Custom lane to that oscillator's Engine at +100% depth. Its step editor then offers Engine 1–3 instead of percentage (for its first route slot). Those choices store 0, midpoint, full-scale. Other strengths, negative modulation, additional sources and a different base engine offset/clamp the result like normal Deluge modulation. Engine display shows the manually selected base, not the continuously modulated value.

Three real Mutable Instruments algorithms are compiled. Harmonics uses Wave Index, Timbre uses Pulse Width and Morph uses Feedback. Deluge supplies its own filters/envelopes. OUT only; no AUX/LPG or other Plaits algorithms. Live engine changes reuse reserved buffers and crossfade for 64 samples; audible transitions and device CPU/polyphony still require hardware tests.

## Compatibility and validation

Legacy sound-owned snakeCustomV1/V2 data remain readable for previous presets; the old sound-owned editor is hidden. Use the new clip sequencer and real Engine patch parameter for current workflows. Older direct Model Route data are not the current routing method.

Host checks cover gestures, shape/direction, independent clocks, rational timing, mutes, gate lifecycle, actual extracted InstrumentClip note output, serialization validation, matrix destination coverage, Engine parameter arithmetic, saved type menus and Plaits DSP. ARM release compilation is also checked. These are not physical-device validation of pad/OLED interaction, SD save/load, clock sync, audible switching or load. Firmware is experimental; the bootloader is unchanged. Back up SD content and use a disposable song first.

License: the original firmware remains GPL-3.0-or-later. Vendored Mutable Instruments files retain their MIT notices. No MDD Max patch code or graphics are copied.
