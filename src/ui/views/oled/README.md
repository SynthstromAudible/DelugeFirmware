# deluge_ui_views_oled

Whole OLED screens for the OLED-first UI: each view draws one screen, a resting screen or a moment, from a plain
struct describing what it shows. A view is a pure function of its argument: it reads no port, model, global or clock,
and it formats nothing. The scene that will call it fills the struct from its ports.

| target | holds | links |
|---|---|---|
| `deluge_ui_views_oled` (namespace `deluge::ui::views::oled`) | the views, one header per family, and their shared row layout (`detail/`) | `deluge_ui_widgets_oled` |

A `STATIC` library built as C++26. The parity views (the "parity" column below) are drawn by their scene; the rest are
not called yet. The pad pictures are the grid views' (`src/ui/views/grid/README.md`), whose rules are these.

## Rules

- **Words arrive finished.** Every word on a screen (titles such as "UNDO", labels such as "NEXT", composite text
  such as "IN 2 BARS" or "SW 56%", note names) is a `std::string_view` the scene has already formatted and
  localised. Numbers arrive as numbers only where a widget draws a quantity: a fill, a level, boxes, spans,
  positions.
- **Layout comes from the frame.** Rows and the body are placed from the frame's bounds and its `Style` (title
  height, row pitch), so each view draws on the 128x43 visible panel and the 128x64 one; every view has a golden
  for each.
- **Includes:** only `ui/views/oled/`, `ui/widgets/oled/`, `ui/widgets/core/`, `ui/gfx/oled/` and the standard
  library (`scripts/layers.toml`, rule `ui-views-oled-includes-outside`).
- No animation of the view's own: a moving mark (a playhead, a fill) is a field of the struct. A widget given the
  frame may animate (next rule).
- **Leaf rules, for both view libraries:** no heap, no templates, and no `/` or `%` by a runtime value in drawing code.
- **The frame's clock and gates are frame input.** A view may call a widget that reads `f.now_ms()`, arms `f.gate()`
  or formats its own quantity (`scroll_text`, `zoom_transition`, `sample_position`); the view itself reads no clock
  and formats no word, it only passes the frame on. Retained widget state (`TextEditState`, `ScrollText`) is a
  trailing reference parameter of the view, never a field of its struct, a static or a member.

## The row layout (`detail/`)

| helper | draws |
|---|---|
| `row_y` | text row n's y: `style.title_height + 3`, then `style.row_height` apart |
| `last_row_y` | the last text row's y: 8 rows above the bottom |
| `body_rect` | the full-width rect from a text row to the frame's bottom |
| `text_pair` / `text_rows` | a label left and a value right in Apple7, the label clipped 4 px short of the value |
| `text_centred` | one Apple7 row, centred |
| `title_short_of` | a title stopping short of a mark the view draws on the title band's right |
| `big_text` | a big value in MetricBold20, centred |
| `name_centred` | a name in the title face, centred |
| `list_rows` | rows in `list`, the current one selected (or none), reading no input |

## The views

| header | views | parity |
|---|---|---|
| `session` | `session_resting`, `session_queued`, `clip_held`, `section_held` |  |
| `arranger` | `arranger_resting`, `arranger_clip_held` |  |
| `clip` | `instrument_clip_resting`, `audio_clip_resting`, `kit_clip_resting`, `cv_clip_resting` |  |
| `keyboard` | `keyboard_playing`, `scale_held`, `velocity_drums` |  |
| `recording` | `record_arming`, `count_in`, `overdub`, `resampling`, `stem_export` |  |
| `editing` | `zoom_held`, `quantize_held`, `step_held`, `automation_lane_moment`, `slicer`, `moving_kit_row`, `randomized_drums`, `undo` |  |
| `playing` | `auditioning_row`, `stutter_held`, `tap_tempo`, `midi_learn`, `mod_source_held`, `loading_song` |  |
| `master` | `master_scope`, `master_scope_xy` |  |
| `recorder` | `recorder_status` | the audio recorder |
| `marker` | `marker_position` | the marker editor |
| `rename` | `rename_window` (`RenameWindow`: the title and the name, with the text editor's state as the trailing `edit`) | the rename editor |
| `browser` | `browser_list` (`BrowserList`: the title, the file icons, the edited name and the rows, each a name, a folder flag, the selection and whether it side-scrolls; the text editor's state and the row's scroller are the trailing `edit` and `row_scroll`), `browser_text_x` | the file browser |

Each view's layout follows its sketch on the OLED canvas (boards "Resting", "Moments" and "Pages").

Specs: `tests/unit/ui/views/oled/`; goldens: `tests/golden/ui/views/oled/`.
