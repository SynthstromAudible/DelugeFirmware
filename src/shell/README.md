# deluge_ui_shell

Surfaces, scene stack, input lowering, services, tick, flush (spec: `docs/superpowers/specs/2026-08-26-ui-shell-design.md`,
spec D of the UI stack redesign, amended by B.1 Task 9: `render_oled`/`render_grid`/`after_render`,
per-surface `Style`, input claims, `begin_tick`).

Rules:
1. `src/shell/**` may include only `shell/**`, `ui/**`, `gfx/**`, `notify/**`, `libdeluge/**` and
   the standard library -- never `model/`, `gui/`, `hid/`, `definitions*`, `board_config.h`.
2. `snake_case` functions/members, `PascalCase` types, `k` constants; namespace
   `deluge::ui::shell`.
3. No mutable globals; a `BoardProfile` is a view over board-owned storage, not an owner.

## Board profile (`profile.h`, Task 1)

The board-independent vocabulary a `BoardProfile` is built from:

| type | purpose |
|---|---|
| `ButtonId` | Every physical button/encoder-push the shell can name (`Count` bounds a `ButtonSet` bit and `button_name()`, and is asserted `<= 64`) |
| `SurfaceRole` | Which surface a UI event/render call targets (`Main`, `Param0..3`, `Aux`, `None`) |
| `EncoderKind` | `Detented` or `Accelerating` |
| `ButtonSet` | A `uint64_t`-backed bitset of `ButtonId`s (`has`/`set`/`any`) |
| `ShortcutTable` | Pad-index -> param-id tables for a shortcut layer (`layer0`, `layer1`; spec E fills it in) |
| `TextLookup`, `ErrorLookup` | The shell's l10n bridge -- the shell has no l10n of its own |
| `BoardProfile` | The full board description: button matrix, LED map, encoder roles/kinds, OLED surface roles and per-surface `Style`, shortcuts, the l10n bridge, and the matrix dimensions |
| `button_name(ButtonId)` | For `.state` files and diagnostics |
| `FixedVector<T, N>` (`fixed_vector.h`) | A no-heap, fixed-capacity vector; `push_back` fails soft past `N` |

`BoardProfile::encoder_by_index` and `encoder_kind` are indexed by `deluge::ui::EncoderRole`
(`ui/core/input.h`) and `style_by_index`/the style fields by `deluge::ui::Style`
(`ui/core/style.h`) -- both from the finished toolkit, not redefined here.

Concrete board tables (e.g. the Deluge's own matrix/LED/encoder layout) are a later task; Task 1
is the vocabulary only.

## Surfaces (`surfaces.h`, `board_surfaces.h`)

`SurfaceSet` is the shell's only hardware seam. Everything above it renders into the surfaces and
flushes through the sink virtuals; `BoardSurfaceSet` binds those sinks to libdeluge and the specs'
`uitest::FakeSurfaceSet` binds them to recorders.

| type | purpose |
|---|---|
| `OledSurface` | One panel: `Canvas` over the pool storage, its `Geometry`, a `RepaintGate`, its `SurfaceRole`, `index`, `blit_pending`, and the profile's `Style*` for it |
| `ColumnPair` | The 16 pad colours of one main-grid column pair, in the legacy `sortLedsForCol` order |
| `ActiveTween` | A grid animation the shell's tick drives (`Fade`, `ScrollH`, `ScrollV`, `GreyOut`) |
| `GridSurface` | The `PadGrid` plus the send-side shadow (`last_sent`/`sent_once`) the legacy `sendOutMainPadColours` lacks |
| `LedSurface` | The `LedFrame` plus the LED and knob-indicator send-side shadow |
| `SurfaceSet` | The surfaces, `oled(SurfaceRole)` lookup, and the sink virtuals |
| `BoardSurfaceSet` | The libdeluge-backed `SurfaceSet`; the **only** `.cpp` here that includes `libdeluge/display.h` / `libdeluge/control_surface.h` |

Storage is a fixed pool: `kMaxOled` (4) surfaces of `kOledStorageBytes` (128x64/8) each, sized at
`build()` time from the board descriptor. No heap.

`DelugeBoard::pad_grid_width` counts **main** pad columns only (16 on the Deluge) and the
descriptor has no sidebar field, so the sidebar width comes from `BoardProfile::sidebar_cols`
(default 2). The grid is `{pad_grid_width, sidebar_cols, pad_grid_height}` and
`GridSurface::pairs` counts pairs across the *total* width -- 9 on the Deluge.
