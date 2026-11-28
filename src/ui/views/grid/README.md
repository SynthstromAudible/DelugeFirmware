# deluge_ui_views_grid

Whole pad pictures: each view draws one surface of a scene, or one part of it, from a plain struct describing what it
shows. A view is a pure function of its struct and the frame: it reads no port, model, global or clock and formats no
word. The scene fills the struct (its `present_*` members) and calls the view from `render_grid`.

| target | holds | links |
|---|---|---|
| `deluge_ui_views_grid` (namespace `deluge::ui::views::grid`) | the grid views, one header per picture | `deluge_ui_widgets_grid` |

A `STATIC` library built as C++26. Its rules are `src/ui/views/oled/README.md`'s, with these differences.

## Rules

- **Words arrive finished**, as `std::string_view`s into text the scene owns and keeps alive until the view returns.
  A data struct holds values and views only.
- **A view may return `widgets::core::Response`** (or a struct of them): drawing is hit-testing, and on the grid pads
  are input. The same struct and the same frame input give the same pixels and the same `Response`. An OLED view
  returns `void`.
- **Retained widget state** (a keyboard's held keys, `WaveformState`, a tick-flash latch) is a reference parameter
  after the struct, owned by the scene. It is never a field of the struct, a static or a member of the view, so a
  trailing reference marks exactly the state a view may update.
- **The frame's clock and gates are frame input**: a view may call a widget that reads `f.now_ms()` or arms
  `f.grid_gate()` itself, and passes the frame on without reading it.
- **Includes:** only `ui/views/grid/`, `ui/widgets/grid/`, `ui/widgets/core/`, `ui/gfx/grid/` and the standard library
  (`scripts/layers.toml`, rule `ui-views-grid-includes-outside`).
- No heap, no templates, and no `/` or `%` by a runtime value in drawing code.
- Names say what the view draws and never reuse an OLED-first view's name.

## The views

| header | views | parity |
|---|---|---|
| `blink` | `blink` (`BlinkPads`: a shortcut blink's plan, counter step and whether the step plays) | the audio recorder |

Specs: `tests/unit/ui/views/grid/`.
