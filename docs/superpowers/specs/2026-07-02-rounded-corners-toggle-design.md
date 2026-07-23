# Rounded Corners Toggle — Design

**Date:** 2026-07-02
**Branch:** `feat/style`
**Status:** Awaiting review

## Goal

Add a user-facing setting that disables rounded corners across the OLED UI, so the
display renders square boxes instead. Default preserves today's rounded appearance.

## Background

Rounded-corner rendering is centralized in two `Canvas` methods
(`src/deluge/hid/display/oled_canvas/canvas.cpp`):

- `drawRectangleRounded(minX, minY, maxX, maxY, radius)` — draws a 1px rectangle with
  1px or 2px rounded corners. Its square equivalent is the existing
  `drawRectangle(minX, minY, maxX, maxY)`.
- `invertAreaRounded(xMin, width, startY, endY, radius)` — inverts a rectangular area,
  then clears the corner pixels to round it. Its square equivalent is a plain
  `invertArea(xMin, width, startY, endY)` with the corner-clearing skipped.

Every rounded box in the UI routes through one of these two methods. Known consumers:
popups (`oled.cpp`), menu number/value boxes (`menu_item/number.cpp`), the filter
container (`menu_item/filter/filter_container.h`), the horizontal-menu selection
highlight (`menu_item/horizontal_menu.cpp`), and patch-cable strength
(`menu_item/patch_cable_strength.cpp`). Guarding the two methods covers all of them.

The codebase already has an established toggle mechanism: the **Community Features**
menu, backed by `RuntimeFeatureSettings`
(`src/deluge/model/settings/runtime_feature_settings.{h,cpp}`). Existing toggles such
as `HorizontalMenus` and `ShowBatteryLevel` are the template this feature follows.

## Design

### Behavior

Add a Community Features on/off toggle named **Rounded Corners**, default **On**
(current behavior). When set to **Off**, all rounded box rendering falls back to square.

Two guards, one per centralized method:

- `drawRectangleRounded(...)`: if the toggle is Off, call
  `drawRectangle(minX, minY, maxX, maxY)` and return.
- `invertAreaRounded(...)`: if the toggle is Off, call
  `invertArea(xMin, width, startY, endY)` and return (skipping the corner-clearing that
  rounds it).

The setting is read directly from the `runtimeFeatureSettings` global singleton via
`runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::RoundedCorners)`. This adds a
dependency from `hid/display` to the settings global — a global read consistent with
how the setting is consumed elsewhere, with negligible cost on the mono display.

### Files to change

Follows the `ShowBatteryLevel` toggle end to end.

1. `src/deluge/model/settings/runtime_feature_settings.h`
   - Add `RoundedCorners` to `enum RuntimeFeatureSettingType`, immediately before
     `MaxElement`.

2. `src/deluge/model/settings/runtime_feature_settings.cpp`
   - In `init()`, register it:
     `SetupOnOffSetting(settings[RuntimeFeatureSettingType::RoundedCorners],
     STRING_FOR_COMMUNITY_FEATURE_ROUNDED_CORNERS, "roundedCorners",
     RuntimeFeatureStateToggle::On);`

3. `src/deluge/gui/menu_item/runtime_feature/settings.cpp`
   - Define a reusable `SettingToggle` subclass that hides the item on 7SEG (for
     OLED-only features — the 7-segment display has no rounded-corner or horizontal-menu
     rendering, so the toggle is meaningless there):
     ```cpp
     class OledOnlySettingToggle final : public SettingToggle {
     public:
         using SettingToggle::SettingToggle;
         bool isRelevant(ModControllableAudio*, int32_t) override { return display->haveOLED(); }
     };
     OledOnlySettingToggle menuRoundedCorners(RuntimeFeatureSettingType::RoundedCorners);
     ```
     Add `#include "hid/display/display.h"` if not already available for `display->haveOLED()`.
   - Insert `&menuRoundedCorners` into the `subMenuEntries` array immediately after
     `&menuHorizontalMenus` (this array controls the menu display order, so the toggle
     appears directly below "Horizontal Menus" in the Community Features menu).
   - Also change the existing `menuHorizontalMenus` from `SettingToggle` to
     `OledOnlySettingToggle` — the horizontal-menus feature is likewise OLED-only (gated
     by `display->haveOLED()` at its render site, `horizontal_menu.cpp:597`), so its menu
     item should not appear on 7SEG either. Its enum, l10n, and default are unchanged;
     only the menu-item class changes.
   - `Submenu` already filters entries by `isRelevant()`, so returning `false` hides the
     item in 7SEG mode. `display->haveOLED()` reflects the *active* display mode, so these
     items hide whenever the unit is in 7-segment mode — real or emulated — and show
     whenever an OLED-style display is active.

4. l10n strings (mirror `STRING_FOR_COMMUNITY_FEATURE_SHOW_BATTERY_LEVEL`):
   - `src/deluge/gui/l10n/strings.h` — add `STRING_FOR_COMMUNITY_FEATURE_ROUNDED_CORNERS`.
   - `src/deluge/gui/l10n/g_english.cpp` + `english.json` — "Rounded Corners".
   - No 7-segment abbreviation needed: the item is hidden on 7SEG, so no
     `g_seven_segment.cpp` / `seven_segment.json` entry is required.

5. `src/deluge/hid/display/oled_canvas/canvas.cpp`
   - Include `model/settings/runtime_feature_settings.h`.
   - Add the two guards described above.

### Persistence

No new persistence work. The `RuntimeFeatureSettings` read/write path serializes the
full `settings` array to `SETTINGS/CommunityFeatures.XML` generically by `xmlName`, so
`"roundedCorners"` is saved and restored automatically.

## Testing

There is no unit-test harness for canvas rendering; verification is visual.

- Build and run on the emulator / host-sim.
- Toggle **Off**: confirm popups and menu number/value boxes render with square corners;
  confirm menu selection highlights, filter container, and patch-cable strength are square.
- Toggle **On** (default): confirm rendering is unchanged from current firmware.
- Confirm the setting survives a power cycle (written to CommunityFeatures.XML).
- In 7-segment mode (or emulated 7SEG), confirm the "Rounded Corners" **and**
  "Horizontal Menus" items do **not** appear in the Community Features menu; in OLED mode
  (real or emulated) confirm both do.

## Open questions / assumptions

Both are cheap to flip; defaults chosen to preserve current behavior and match the
existing opt-in style.

1. **Framing.** Menu item named **"Rounded Corners", default On** — turning it Off
   squares the UI. Alternative considered: "Square Corners", default Off. Chosen the
   former for clearest semantics (the label names what it controls, default = today).

## Out of scope

- No per-element granularity (single global toggle; scope confirmed as "all rounded
  rendering").
- No new border-radius options or circle/other-shape changes.
- No changes to the 7-segment display (it has no rounded-corner rendering).
- Beyond gating the existing "Horizontal Menus" menu item off 7SEG (via the shared
  `OledOnlySettingToggle`), no other change to the horizontal-menus feature.
