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
  `util/**` into `gfx/**`. `gfx/grid/exp_table.{h,cpp}` does the same for `util::expTableSmall` plus
  `interpolateTable` (`util/functions.cpp:487`) and `increaseMagnitude` (`util/functions.h:362`),
  which `tween.cpp`'s `zoom` needs. Both are verbatim data/logic duplications, not shared sources of
  truth — if the audio-side tables ever change, these copies will silently drift out of sync unless
  someone remembers to update both. Worth resolving by hoisting them into a place both `util/` and
  `gfx/` can depend on, if that's ever worth the churn.

Run the check locally with `python3 scripts/check_gfx_layer.py`; it is also the `gfx_layer_check` ctest.

## `DELUGE_GFX_CONTRACT`

`gfx/contract.h`'s `DELUGE_GFX_CONTRACT(cond)` is this leaf's runtime precondition check: an
unconditional `__builtin_trap()` if `cond` is false, with no `NDEBUG` guard anywhere in the file,
so it survives `-DNDEBUG` (every shipped build defines it) where a plain `assert()` would compile
out. The condition it guards is a memory-safety precondition, not a recoverable runtime one — a
`Canvas` built over a span shorter than its geometry, or a `PadGrid`/tween call given mismatched
dimensions, would otherwise read or write past caller memory on the first real call. `gfx` cannot
report through the app's `FREEZE_WITH_ERROR` path (it is a leaf and may not reach `display`), so a
leaf precondition can only stop, not report — an unconditional trap is the correct trade: a halt
at the violating frame's PC and backtrace, rather than a heap smash discovered later. Used by
`Canvas`'s constructor (storage long enough for the geometry), `gfx::grid::Rect::split_rows`/
`split_cols`'s `at` bound, `composite`'s dimension match, and every one of the tween functions
below (row/column bounds, `zoom`'s magnitude ceiling). Never used for a condition a caller can
legitimately produce at runtime — clipping, an out-of-range value arriving from the model, a full
queue — those stay return values.

## `gfx::oled::Canvas`: the clip rect

`Canvas::set_clip(Rect)`/`clear_clip()`/`clip() -> optional<Rect>` (`gfx/oled/canvas.h`) bound
*every* drawing routine to a rectangle, pixel for pixel: a glyph, an icon or a rounded invert that
straddles the boundary draws only the part that is inside. The clip is resolved once, into a
private `ClipBox` of four inclusive ints intersected with the canvas, so no drawing loop reads a
`std::optional`, and with no clip set (the default) every routine behaves exactly as it did before
this existed — the clip simply starts out as the whole canvas. The byte-at-a-time blitters
(`draw_graphic_multi_line`, and everything that routes through it: `draw_char`, the string family,
icons) clip vertically with one precomputed bit mask per page rather than a per-pixel test.
`ui::oled::ClipScope` (`src/ui/README.md`) is the frame-layer type built on top of this that every
`OledFrame` drawing method opens for its own region.

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

## `gfx::grid` tweens

`gfx/grid/tween.{h,cpp}`: pure `from -> to` frame transitions over `PadGrid`; the caller owns
timing and calls one of these once per tick with its own progress parameter. `fade`,
`scroll_horizontal`/`scroll_vertical` and `grey_out` ported first; `smear_scroll_horizontal`/
`smear_scroll_vertical`, `zoom`, `explode` and `expand_collapse` (toolkit-fixes task-7) round out
the set legacy's `hid/led/pad_leds.cpp` animates:

| function | ports | notes |
|---|---|---|
| `fade` | `PadLEDs::renderFade` | blend `from`→`to` at `t` (0..65535) |
| `scroll_horizontal`/`scroll_vertical` | `horizontal::renderScroll`/`vertical::renderScroll` | whole-square shift; vertical moves the sidebar with the rows, horizontal does not |
| `grey_out` | `RGB::greyOut` applied per pad | sidebar untouched |
| `smear_scroll_horizontal`/`_vertical` | no legacy software routine exists — legacy hands the smooth part to the PIC in hardware, which the host sim and any second display have nothing for | ported from `deluge-grid-toolkit`'s `animations/smear_scroll.rs`, with its `blend_static` argument order swapped: the SDK's own code is a whole pixel out of place at a zero offset (verified — an integral `offset_px_16` is byte-identical to `scroll_horizontal`/`_vertical`, the legacy whole-square behaviour, only after the swap) |
| `zoom` | `renderZoom` + `renderZoomWithProgress`/`renderZoomedSquare` | `ZoomParams` incl. `kMaxZoomMagnitude = 5` (`DELUGE_GFX_CONTRACT`-enforced: the shift arithmetic in `renderZoom`'s port is UB past it) |
| `explode` | `renderExplodeAnimation` | `ExplodeParams` incl. `OccupancyMask from_occupancy` |
| `expand_collapse` | the shared core of `renderInstrumentClipCollapseAnimation` | `ExpandCollapseParams` incl. `row_to_by_row` (per-row destinations, for `renderNoteRowExpandOrCollapse`'s per-note-row targets) and `from_occupancy`/`to_occupancy` |

`OccupancyMask` (a `std::span<const uint32_t>` in `ui::PadMask`'s column-bitset layout) stands in
for legacy's `occupancyMaskStore`/`occupancyMask` where "source pad is non-black" is not the same
question as "was this pad marked occupied" — `View::renderMacros` paints an armed clip's macro pad
black on its flash-off frame and marks it occupied anyway, and that reaches both `explode` and
`expand_collapse`'s callers. An empty span keeps the non-black default (every existing caller and
golden is unaffected); a non-empty one must be exactly `cols()` long (`DELUGE_GFX_CONTRACT`).

Five legacy quirks are reproduced rather than fixed in `zoom`/`explode`/`expand_collapse` — see
`src/ui/README.md`'s "Legacy quirks reproduced on purpose" section, which lists them alongside the
ones from the widget layer.

Deferred to later phases (tracked in the UI redesign spec): `RGB` into the `deluge::gfx::grid`
namespace; the raw `uint8_t[]` graphics still on `OLED`.
