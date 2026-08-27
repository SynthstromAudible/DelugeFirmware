# deluge_gfx_oled / deluge_gfx_grid

Pure drawing primitives for the Deluge UI stack (spec: docs/superpowers/specs/2026-08-26-ui-stack-redesign-design.md §6).

Rules (enforced by `scripts/check_gfx_layer.py`):
- May include only `gfx/**` and the C/C++ standard library.
- No mutable globals. No heap allocation in drawing paths.
- No knowledge of the board: geometry, dimensions and style arrive as values.
- `gfx/oled` and `gfx/grid` do not include each other.

## Deferred

- `RGB` and the palette (`gfx/grid/rgb.h`, `gfx/grid/palette.h`) live at their new path but are
  still declared in the **global namespace** (palette contents under `deluge::gui::colours`,
  unchanged). Moving `RGB` into `deluge::gfx::grid` touches roughly 600 call sites across the
  codebase and was out of scope for the file move (Task 10); do it as its own follow-up pass.
- `gfx/grid/sine_table.{h,cpp}` duplicates `util::sineWaveSmall` (`src/deluge/util/lookuptables/lookuptables.cpp`)
  and a local port of `util/waves.h`'s `getSine`, so that `RGB::fromHue`/`fromHuePastel` don't pull
  `util/**` into `gfx/**`. This is a verbatim data/logic duplication, not a shared source of truth —
  if the audio-side table ever changes, `gfx/grid/sine_table.cpp` will silently drift out of sync
  unless someone remembers to update both. Worth resolving by hoisting the table into a place both
  `util/` and `gfx/` can depend on, if that's ever worth the churn.

Run the check locally with `python3 scripts/check_gfx_layer.py`; it is also the `gfx_layer_check` ctest.

Deferred to later phases (tracked in the UI redesign spec): `RGB` into the `deluge::gfx::grid`
namespace; the raw `uint8_t[]` graphics still on `OLED`; zoom/explode/expand-collapse tweens.
