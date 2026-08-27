# deluge_ui_core / deluge_ui_oled / deluge_ui_grid

The immediate-mode toolkit for the Deluge UI stack (spec: `docs/superpowers/specs/2026-08-26-ui-toolkit-design.md`,
spec B of the UI stack redesign, §2).

Rules (CMake deps + the layer checker from the gfx plan, extended):
1. `ui-oled` and `ui-grid` never include each other.
2. None of the three includes `notify`, `model`, `libdeluge`, `l10n`, `gui/`, `hid/`.
3. No mutable globals; no heap in any widget; widgets are not templates (only `region`
   bodies are).
4. Every widget takes its state by reference from the caller and reads input only from the
   frame.

## Layout

```
deluge-ui-core   InputSnapshot, Response, RepaintGate, POD state structs, Style, LedFrame
                 deps: gfx-oled (Rect, FontId), gfx-grid (RGB) — types only
deluge-ui-oled   OledFrame + widgets (namespace deluge::ui::oled)      deps: ui-core, gfx-oled
deluge-ui-grid   GridFrame + components (namespace deluge::ui::grid)   deps: ui-core, gfx-grid
```

All three targets are `INTERFACE` libraries until their first `.cpp` lands (core in Task 2,
oled in Task 3, grid in Task 10 of the toolkit plan), at which point they become `STATIC`.
