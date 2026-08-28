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

## `gfx::grid::Rect`: splits and inset

`split_rows(at)`/`split_cols(at)` (`gfx/grid/rect.h`) split into a `(before, after)` pair at a
single row/column position `at` -- the same two-way-at-a-position shape as `gfx::oled::Rect`'s
siblings (`split_rows`/`split_cols`, `oled/geometry.h:50-51`, used at `ui/oled/misc.cpp:126` as
`rect.split_rows(rect.h / 2)`) and the SDK's `deluge-grid-toolkit::Frame::split_rows`/`split_cols`
(`imode.rs:367-398`). Task-6 fix round 1 replaced an earlier `(n, i)` n-way form here (slice `i`
of `n` equal bands) that matched neither sibling -- no production call site used it, and no
distinctly-named n-way helper was added in its place since none has a caller yet. `inset(int32_t)`
shrinks by one uniform amount on every side (negative grows it) -- the OLED sibling's shape, not
its per-side `Insets`; nothing in the grid toolkit needs asymmetric insets yet.

## `gfx::grid::composite`

`composite(base, overlay, mode, out)` (`gfx/grid/composite.{h,cpp}`) flattens one `PadGrid`
overlay onto another per a `BlendMode`, writing into `out` (which may alias `base`) and marking
only the rows of `out` whose colour actually changed dirty. Ported from
`deluge-grid-toolkit::layer` (`layer.rs:17-25` for the enum, `layer.rs:84-112` for
`apply_layer`), whose `GridLayer` cells are `Option<Color>` -- `None` meaning transparent. This
`PadGrid` has no such channel, so black (`RGB{0, 0, 0}`) stands in for "nothing here" wherever the
SDK's `None` would have:

| mode | rule | vs. the SDK |
|---|---|---|
| `Replace` | overlay pad wins wherever it is **non-black**; a black overlay pad leaves `base` showing through | adapted -- the SDK's `Replace` (`layer.rs:91-93`) always overwrites for any non-`None` cell, transparency alone gating it, not colour. Since every `PadGrid` cell is a concrete colour, a non-black overlay pad is what stands in for "this was painted" |
| `Overlay` | `base` pad wins unless it is currently **black**, in which case the overlay pad shows through (regardless of the overlay's own colour) | verbatim (`layer.rs:94-99`) -- the SDK already treats a black *destination* cell as empty, so the substitution above changes nothing here |
| `Tint` | each channel is `base * overlay / 255`, truncating (no rounding) | verbatim (`layer.rs:100-106`) |

`tests/spec/gfx/composite_spec.cpp` hand-computes one cell per corner case for each mode (a black
overlay pad, a black base pad, a full-white tint identity, and a truncating tint), pins the
dirty-row marking and in-place aliasing, and freezes a golden `.state` per mode
(`tests/golden/gfx/grid/composite_{replace,overlay,tint}.state`).

Deferred to later phases (tracked in the UI redesign spec): `RGB` into the `deluge::gfx::grid`
namespace; the raw `uint8_t[]` graphics still on `OLED`; zoom/explode/expand-collapse tweens.
