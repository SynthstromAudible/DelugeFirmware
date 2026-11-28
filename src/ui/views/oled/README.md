# deluge_ui_views_oled

Whole OLED screens for the OLED-first UI: each view draws one screen, a resting screen or a moment, from a plain
struct describing what it shows. A view is a pure function of its argument: it reads no port, model, global or clock,
and it formats nothing. The scene that will call it fills the struct from its ports.

| target | holds | links |
|---|---|---|
| `deluge_ui_views_oled` (namespace `deluge::ui::views::oled`) | the views, one header per family, and their shared row layout (`detail/`) | `deluge_ui_widgets_oled` |

A `STATIC` library built as C++26. Nothing calls it yet: no scene, port or firmware path renders a view.

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
- No animation: a moving mark (a playhead, a fill) is a field of the struct.

## The row layout (`detail/`)

| helper | draws |
|---|---|
| `row_y` | text row n's y: `style.title_height + 3`, then `style.row_height` apart |
| `body_rect` | the full-width rect from a text row to the frame's bottom |
| `text_pair` / `text_rows` | a label left and a value right in Apple7, the label clipped 4 px short of the value |
| `big_text` | a big value in MetricBold20, centred |
| `name_centred` | a name in the title face, centred |

## The views

| header | views |
|---|---|
| `session` | `session_resting`, `session_queued`, `clip_held`, `section_held` |

Each view's layout follows its sketch on the OLED canvas (boards "Resting", "Moments" and "Pages").

Specs: `tests/unit/ui/views/oled/`; goldens: `tests/golden/ui/views/oled/`.
