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
