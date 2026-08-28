# Second frontend: a new device as BSP + board profile

**Date:** 2026-08-26
**Branch target:** `next`
**Status:** design, pending review. Spec H of the UI stack redesign
(`2026-08-26-ui-stack-redesign-design.md` §8.7). Depends on spec D (shell) and the
libdeluge multi-display extension (gfx plan, Task 1).

Naming: `snake_case` functions/variables; `PascalCase` types.

## 1. The device, as far as this spec needs to know

Several 128×64 mono OLEDs (no bezel offset), the same 8-row pad grid with 16 main
columns **and a two-column sidebar**, more buttons than the Deluge, encoders. Shipped as a
**separate firmware image** (size), selected by build target, not at boot.

## 2. A frontend is three artefacts

Nothing in `deluge-ui-*`, `deluge-ui-shell` or `deluge-scenes` changes for a new device.

1. **A `libdeluge` BSP** for the board (Rust in `deluge-sdk`'s `deluge-bsp`, or C — the ABI
   is indifferent), implementing the same headers plus the multi-display extension:
   `DelugeBoard{pad_grid_width 16, pad_grid_height 8, button_rows/cols for its matrix,
   encoder_count, display_kind OLED, display_count N, displays[] = {128, 64, 0}, …}`,
   `deluge_display_blit_oled_n`, `deluge_control_*`, `deluge_encoder_take_edges`.
2. **A `BoardProfile`** in `src/scenes/profiles/<board>.cpp`:
   `button_by_matrix` (new `ButtonId` values for the extra buttons), `led_by_button`,
   `encoder_by_index` / `encoder_kind`, `oled_role_by_index` (which panel is `Main`, which
   are `Param0..3` / `Aux`), a `Style` (larger faces and row heights for 64 rows), and the
   shortcut tables (the same `Node` pointers unless the panel wants a different pad map).
3. **A firmware target** (`dbt build <board> Debug`) that links `deluge_app` against that
   BSP with `-DDELUGE_BOARD_PROFILE=<board>`, exactly as `sim/` links the host BSP. The
   profile is selected at link time; only one profile's tables are in an image.

## 3. Surface roles

The profile decides; the shell only composites. Two supported patterns, per board:

- **Param displays** (`Param0..3`, typically above encoders): scenes with a
  `ParamOledView` render focused parameters one per panel — the horizontal menu's four
  columns become four panels; `ModKnobs` shows the gold-knob targets on the panels nearest
  those knobs. Scenes without a `ParamOledView` leave the panels to `ModKnobs`' default
  (current param name, value, indicator).
- **Aux** (a second general panel): a scene may render a secondary view (arranger overview,
  clip list) or nothing; the shell shows console/notices there when the scene leaves it
  blank.

Panels with `SurfaceRole::None` are cleared once at boot and never drawn.

## 4. Buttons, encoders, LEDs

- New `ButtonId` values are appended. A scene that does not handle one returns
  `handled = false` and `GlobalControls` gets a chance. A board with fewer buttons maps
  nothing to the missing ids; no scene asks "does this board have X" — it checks
  `ctx.held.has(...)` or receives the event.
- Extra encoders get new `EncoderRole`s. `InputSnapshot` grows a field only when a role is
  meant to reach widgets (e.g. a per-panel encoder driving that panel's `columns` focus);
  otherwise scenes read the role from `UiEvent`.
- LEDs under new buttons are `led_by_button` rows; indicators beyond two extend `IndicatorId`.

## 5. Bring-up before hardware exists

1. Write the `DelugeBoard` literal and the `BoardProfile`.
2. Run the host sim with `DELUGE_HOST_DISPLAYS=128x64+0,128x64+0,…` and
   `DELUGE_HOST_PROFILE=<board>`; run every level-3 script; record the device's goldens
   as a second set under `tests/golden/ui/app-<board>/` (they differ by design: larger
   `Style`, more panels).
3. Drive it interactively through spark's panel with a faceplate SVG for the new layout
   (the simulator's button/encoder coordinate table is data).
4. Bring up the BSP on hardware; the application is unchanged.

## 6. Keeping it true

- CI builds `deluge_app` for **both profiles** against the host BSP and runs both golden
  sets, so a scene that hard-codes `128×48`, renders only to `Main`, or references a
  Deluge-specific `ButtonId` fails before it ships.
- Layer-rule row: `src/scenes/**` outside `profiles/` may not reference `kDelugeOled`,
  `kDelugeGrid`, or any symbol from `profiles/deluge.cpp`.
- Size budget per image (`./dbt sizediff`): the second image carries only its own profile
  tables and goldens; the shell/toolkit are shared code.

## 7. Open questions

1. Whether the new panels' encoders (if per-panel) should be `EncoderRole::Panel(n)` in
   `InputSnapshot` or scene-level events only — decide with the first `ParamOledView`.
2. Whether `Aux` gets a default renderer in the shell (console/notices, as drafted) or stays
   blank unless a scene draws — default renderer as drafted; revisit if it distracts.
