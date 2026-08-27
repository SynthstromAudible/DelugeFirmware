# deluge_gfx_oled / deluge_gfx_grid

Pure drawing primitives for the Deluge UI stack (spec: docs/superpowers/specs/2026-08-26-ui-stack-redesign-design.md §6).

Rules (enforced by `scripts/check_gfx_layer.py`):
- May include only `gfx/**` and the C/C++ standard library.
- No mutable globals. No heap allocation in drawing paths.
- No knowledge of the board: geometry, dimensions and style arrive as values.
- `gfx/oled` and `gfx/grid` do not include each other.
