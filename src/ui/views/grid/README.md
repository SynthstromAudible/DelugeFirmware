# deluge_ui_views_grid

Whole pad pictures: each view draws one surface of a scene, or one part of it, from a plain struct describing what it
shows. A view is a pure function of its struct and the frame: it reads no port, model, global or clock and formats no
word. The scene fills the struct (its `present_*` members) and calls the view from `render_grid`.

| target | holds | links |
|---|---|---|
| `deluge_ui_views_grid` (namespace `deluge::ui::views::grid`) | the grid views, one header per picture | `deluge_ui_widgets_grid` |

A `STATIC` library built as C++26. Its rules are `src/ui/views/oled/README.md`'s (finished words, retained widget state
as a trailing reference, the frame's clock and gates as frame input, no heap, templates or runtime division), with
these differences.

## Rules

- **A view may return `widgets::core::Response`** (or a struct of them): drawing is hit-testing, and on the grid pads
  are input. The same struct and the same frame input give the same pixels and the same `Response`. An OLED view
  returns `void`.
- **Retained widget state** (a keyboard's held keys, `WaveformState`, a tick-flash latch) is a reference parameter
  after the struct, so a trailing reference marks exactly the state a view may update.
- **Includes:** only `ui/views/grid/`, `ui/widgets/grid/`, `ui/widgets/core/`, `ui/gfx/grid/` and the standard library
  (`scripts/layers.toml`, rule `ui-views-grid-includes-outside`).
- Names say what the view draws and never reuse an OLED-first view's name.

## The views

| header | views | parity |
|---|---|---|
| `blink` | `blink` (`BlinkPads`: a shortcut blink's plan, counter step and whether the step plays) | the audio recorder, the file browser |
| `waveform` | `waveform` (a sample's peaks as full-height bars over the main pads, monochrome or coloured) | the marker editor, the file browser |
| `playhead` | `playhead` (`Playhead`: each row's tick square, painted, flashed, or both; the flash latch is the trailing `last`) | the marker editor |
| `marker` | `marker_overlay` (`MarkerOverlay`: each column's markers, their colours, the blink's suppression and flood) | the marker editor |
| `slicer` | `slice_marks` (`SliceMarks`: the manual mode's squashed waveform, each slice's mark, and the 64 slots) | the slicer |
| `keyboard` | `keyboard` (`KeyboardPads`: the text keyboard's layout and home row, optionally painted dark; the keyboard's state is the trailing `kb`) | the rename editor, the file browser |
| `favourites` | `favourites` (`FavouritePads`: the favourites row's colours, the bank row's beneath it or none, and the favourite and bank to flash; returns each row's `Response`) | the file browser |

Specs: `tests/unit/ui/views/grid/`.
