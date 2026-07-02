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
   - Declare `SettingToggle menuRoundedCorners(RuntimeFeatureSettingType::RoundedCorners);`
   - Insert `&menuRoundedCorners` into the `subMenuEntries` array immediately after
     `&menuHorizontalMenus` (this array controls the menu display order, so the toggle
     appears directly below "Horizontal Menus" in the Community Features menu).

4. l10n strings (mirror `STRING_FOR_COMMUNITY_FEATURE_SHOW_BATTERY_LEVEL`):
   - `src/deluge/gui/l10n/strings.h` — add `STRING_FOR_COMMUNITY_FEATURE_ROUNDED_CORNERS`.
   - `src/deluge/gui/l10n/g_english.cpp` + `english.json` — "Rounded Corners".
   - `src/deluge/gui/l10n/g_seven_segment.cpp` + `seven_segment.json` — 4-char abbreviation "CORN".

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

## Open questions / assumptions

Both are cheap to flip; defaults chosen to preserve current behavior and match the
existing opt-in style.

1. **Framing.** Menu item named **"Rounded Corners", default On** — turning it Off
   squares the UI. Alternative considered: "Square Corners", default Off. Chosen the
   former for clearest semantics (the label names what it controls, default = today).
2. **7-segment abbreviation.** "CORN" (4 chars). Alternative: "ROUN".

## Out of scope

- No per-element granularity (single global toggle; scope confirmed as "all rounded
  rendering").
- No new border-radius options or circle/other-shape changes.
- No changes to the 7-segment display (it has no rounded-corner rendering).
