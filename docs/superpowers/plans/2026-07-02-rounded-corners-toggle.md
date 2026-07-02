# Rounded Corners Toggle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Community Features toggle "Rounded Corners" (default On) that, when Off, renders all OLED boxes with square corners.

**Architecture:** A new `RuntimeFeatureSettings` on/off setting, surfaced in the Community Features menu via a reusable OLED-only menu-item subclass (hidden on 7SEG). The actual behavior is two guards in the centralized `Canvas` rounded-drawing methods that fall back to their square equivalents when the toggle is Off.

**Tech Stack:** C++ (Deluge firmware), CMake via `./dbt`, existing `RuntimeFeatureSettings` framework, l10n string tables.

## Global Constraints

- Default behavior must be unchanged: the "Rounded Corners" setting defaults to `On`.
- Follow the existing `ShowBatteryLevel` runtime-feature toggle pattern exactly.
- Setting XML name: `"roundedCorners"` (persisted to `SETTINGS/CommunityFeatures.XML`).
- English display name: `"Rounded Corners"`.
- No 7-segment abbreviation: the menu item is hidden on 7SEG (via `isRelevant()`).
- Menu display order: item appears immediately below "Horizontal Menus".
- Verification is build + visual (no unit-test harness exists for canvas rendering).
- Build command for every task: `./dbt build` (must complete with no errors).

---

### Task 1: Declare and register the `RoundedCorners` runtime-feature setting

Creates the setting so it exists, persists, and defaults to On. Not yet reachable in the menu and has no rendering effect — those come in Tasks 2 and 3.

**Files:**
- Modify: `src/deluge/gui/l10n/strings.h:569`
- Modify: `src/deluge/gui/l10n/g_english.cpp` (after the `SHOW_BATTERY_LEVEL` entry)
- Modify: `src/deluge/gui/l10n/english.json` (after the `SHOW_BATTERY_LEVEL` entry)
- Modify: `src/deluge/model/settings/runtime_feature_settings.h:68`
- Modify: `src/deluge/model/settings/runtime_feature_settings.cpp:203`

**Interfaces:**
- Produces: `RuntimeFeatureSettingType::RoundedCorners` (enum value) and
  `STRING_FOR_COMMUNITY_FEATURE_ROUNDED_CORNERS` (l10n string), both consumed by Task 2.
- Consumes: nothing.

- [ ] **Step 1: Add the l10n string enum**

In `src/deluge/gui/l10n/strings.h`, add the new enumerator directly after
`STRING_FOR_COMMUNITY_FEATURE_SHOW_BATTERY_LEVEL,` (line 569):

```cpp
	STRING_FOR_COMMUNITY_FEATURE_SHOW_BATTERY_LEVEL,
	STRING_FOR_COMMUNITY_FEATURE_ROUNDED_CORNERS,
```

- [ ] **Step 2: Add the English string mapping (compiled table)**

In `src/deluge/gui/l10n/g_english.cpp`, add directly after the
`{STRING_FOR_COMMUNITY_FEATURE_SHOW_BATTERY_LEVEL, "Show Battery Level"},` line:

```cpp
        {STRING_FOR_COMMUNITY_FEATURE_SHOW_BATTERY_LEVEL, "Show Battery Level"},
        {STRING_FOR_COMMUNITY_FEATURE_ROUNDED_CORNERS, "Rounded Corners"},
```

- [ ] **Step 3: Add the English string mapping (JSON source)**

In `src/deluge/gui/l10n/english.json`, add directly after the
`"STRING_FOR_COMMUNITY_FEATURE_SHOW_BATTERY_LEVEL": "Show Battery Level",` line:

```json
    "STRING_FOR_COMMUNITY_FEATURE_SHOW_BATTERY_LEVEL": "Show Battery Level",
    "STRING_FOR_COMMUNITY_FEATURE_ROUNDED_CORNERS": "Rounded Corners",
```

- [ ] **Step 4: Add the enum value**

In `src/deluge/model/settings/runtime_feature_settings.h`, add `RoundedCorners,`
immediately before `MaxElement // Keep as boundary` (line 69):

```cpp
	ShowBatteryLevel,
	RoundedCorners,
	MaxElement // Keep as boundary
```

- [ ] **Step 5: Register the setting in `init()`**

In `src/deluge/model/settings/runtime_feature_settings.cpp`, add the registration
directly after the `ShowBatteryLevel` block (which ends at line 202), still inside
`init()`:

```cpp
	// Show Battery Level
	SetupOnOffSetting(settings[RuntimeFeatureSettingType::ShowBatteryLevel],
	                  STRING_FOR_COMMUNITY_FEATURE_SHOW_BATTERY_LEVEL, "showBatteryLevel",
	                  RuntimeFeatureStateToggle::On);

	// Rounded Corners
	SetupOnOffSetting(settings[RuntimeFeatureSettingType::RoundedCorners],
	                  STRING_FOR_COMMUNITY_FEATURE_ROUNDED_CORNERS, "roundedCorners",
	                  RuntimeFeatureStateToggle::On);
```

- [ ] **Step 6: Build**

Run: `./dbt build`
Expected: build completes with no errors. (The new setting compiles and is registered;
it is not yet shown in any menu.)

- [ ] **Step 7: Commit**

```bash
git add src/deluge/gui/l10n/strings.h src/deluge/gui/l10n/g_english.cpp \
        src/deluge/gui/l10n/english.json \
        src/deluge/model/settings/runtime_feature_settings.h \
        src/deluge/model/settings/runtime_feature_settings.cpp
git commit -m "feat: add RoundedCorners runtime feature setting"
```

---

### Task 2: Surface the toggle in the Community Features menu (OLED-only)

Adds the menu item below "Horizontal Menus" using a reusable OLED-only toggle class, and
retrofits "Horizontal Menus" to the same class so it too is hidden on 7SEG.

**Files:**
- Modify: `src/deluge/gui/menu_item/runtime_feature/settings.cpp:17-23` (add include)
- Modify: `src/deluge/gui/menu_item/runtime_feature/settings.cpp:50` (change class of `menuHorizontalMenus`)
- Modify: `src/deluge/gui/menu_item/runtime_feature/settings.cpp:53` (add `OledOnlySettingToggle` definition + `menuRoundedCorners` instance)
- Modify: `src/deluge/gui/menu_item/runtime_feature/settings.cpp:75` (insert into `subMenuEntries`)

**Interfaces:**
- Consumes: `RuntimeFeatureSettingType::RoundedCorners` (Task 1), `SettingToggle`
  (existing), `display->haveOLED()` from `hid/display/display.h` (existing global
  `extern deluge::hid::Display* display;`).
- Produces: menu entry `menuRoundedCorners`; the shared `OledOnlySettingToggle` class.

- [ ] **Step 1: Add the display include**

In `src/deluge/gui/menu_item/runtime_feature/settings.cpp`, add the include alongside the
existing ones (after `#include "shift_is_sticky.h"`, line 22):

```cpp
#include "setting.h"
#include "shift_is_sticky.h"
#include "hid/display/display.h"
#include <array>
```

- [ ] **Step 2: Define the reusable OLED-only toggle class**

In `src/deluge/gui/menu_item/runtime_feature/settings.cpp`, inside the
`namespace deluge::gui::menu_item::runtime_feature {` block, add this class definition
just before the "Generic menu item instances" declarations (before line 30):

```cpp
namespace deluge::gui::menu_item::runtime_feature {

// A SettingToggle that is only relevant (and thus only shown) when an OLED display is
// active. Used for features that have no effect on the 7-segment display.
class OledOnlySettingToggle final : public SettingToggle {
public:
	using SettingToggle::SettingToggle;
	bool isRelevant(ModControllableAudio*, int32_t) override { return display->haveOLED(); }
};

// Generic menu item instances
```

- [ ] **Step 3: Switch Horizontal Menus to the OLED-only class**

In `src/deluge/gui/menu_item/runtime_feature/settings.cpp`, change the declaration at
line 50 from `SettingToggle` to `OledOnlySettingToggle`:

```cpp
OledOnlySettingToggle menuHorizontalMenus(RuntimeFeatureSettingType::HorizontalMenus);
```

- [ ] **Step 4: Declare the Rounded Corners menu item**

In `src/deluge/gui/menu_item/runtime_feature/settings.cpp`, add directly after the
`menuShowBatteryLevel` declaration (line 52):

```cpp
SettingToggle menuShowBatteryLevel(RuntimeFeatureSettingType::ShowBatteryLevel);
OledOnlySettingToggle menuRoundedCorners(RuntimeFeatureSettingType::RoundedCorners);
```

- [ ] **Step 5: Insert into the menu order below Horizontal Menus**

In `src/deluge/gui/menu_item/runtime_feature/settings.cpp`, insert `&menuRoundedCorners`
into the `subMenuEntries` array immediately after `&menuHorizontalMenus,` (line 75):

```cpp
    &menuHorizontalMenus,
    &menuRoundedCorners,
    &menuTrimFromStartOfAudioClip,
```

- [ ] **Step 6: Build**

Run: `./dbt build`
Expected: build completes with no errors.

- [ ] **Step 7: Visual verification**

Launch the OLED emulator/host-sim. Navigate: Settings → Community Features.
Expected:
- "Rounded Corners" appears directly below "Horizontal Menus", defaulting to On.
- In 7SEG mode (or emulated 7SEG), neither "Rounded Corners" nor "Horizontal Menus"
  appears in the list.

- [ ] **Step 8: Commit**

```bash
git add src/deluge/gui/menu_item/runtime_feature/settings.cpp
git commit -m "feat: add Rounded Corners menu item, gate OLED-only toggles off 7SEG"
```

---

### Task 3: Make the toggle square the rendering

Adds the two guards to the centralized rounded-drawing methods so that when the setting
is Off, boxes render square everywhere.

**Files:**
- Modify: `src/deluge/hid/display/oled_canvas/canvas.cpp:18-27` (add include)
- Modify: `src/deluge/hid/display/oled_canvas/canvas.cpp:236` (`drawRectangleRounded` guard)
- Modify: `src/deluge/hid/display/oled_canvas/canvas.cpp:760` (`invertAreaRounded` guard)

**Interfaces:**
- Consumes: `runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::RoundedCorners)` from
  `model/settings/runtime_feature_settings.h` (global `extern RuntimeFeatureSettings
  runtimeFeatureSettings;`); existing `Canvas::drawRectangle` and `Canvas::invertArea`.
- Produces: final behavior; nothing consumed downstream.

- [ ] **Step 1: Add the settings include**

In `src/deluge/hid/display/oled_canvas/canvas.cpp`, add the include among the existing
ones (after `#include "gui/fonts/fonts.h"`, line 21):

```cpp
#include "gui/fonts/fonts.h"
#include "model/settings/runtime_feature_settings.h"
#include "storage/flash_storage.h"
```

- [ ] **Step 2: Guard `drawRectangleRounded`**

In `src/deluge/hid/display/oled_canvas/canvas.cpp`, at the top of
`Canvas::drawRectangleRounded` (currently line 236), add the square fallback as the first
statement:

```cpp
void Canvas::drawRectangleRounded(int32_t minX, int32_t minY, int32_t maxX, int32_t maxY, BorderRadius radius) {
	if (!runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::RoundedCorners)) {
		drawRectangle(minX, minY, maxX, maxY);
		return;
	}

	const int32_t radiusPixels = radius == SMALL ? 1 : 2;
```

- [ ] **Step 3: Guard `invertAreaRounded`**

In `src/deluge/hid/display/oled_canvas/canvas.cpp`, at the top of
`Canvas::invertAreaRounded` (currently line 760), add the square fallback as the first
statement:

```cpp
void Canvas::invertAreaRounded(int32_t xMin, int32_t width, int32_t startY, int32_t endY, BorderRadius radius) {
	if (!runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::RoundedCorners)) {
		invertArea(xMin, width, startY, endY);
		return;
	}

	const int32_t radiusPixels = radius == SMALL ? 1 : 2;
```

- [ ] **Step 4: Build**

Run: `./dbt build`
Expected: build completes with no errors.

- [ ] **Step 5: Visual verification**

Launch the OLED emulator/host-sim.
- With "Rounded Corners" = On (default): confirm popups, menu number/value boxes, menu
  selection highlights, filter container, and patch-cable strength render exactly as
  before (rounded).
- Set "Rounded Corners" = Off: confirm those same elements now render with square
  corners.
- Power-cycle the emulator: confirm the Off setting persists (written to
  `SETTINGS/CommunityFeatures.XML`).

- [ ] **Step 6: Commit**

```bash
git add src/deluge/hid/display/oled_canvas/canvas.cpp
git commit -m "feat: render square corners when Rounded Corners setting is off"
```

---

## Notes for the implementer

- The `isRelevant(ModControllableAudio*, int32_t)` signature must match the virtual in
  `menu_item.h` exactly (`ModControllableAudio` is already visible via the `Selection` →
  `MenuItem` include chain; no extra include needed for the type).
- `display->haveOLED()` returns true for emulated OLED, so the two OLED-only items stay
  visible in the emulator and hide only in genuine 7-segment mode.
- Persistence is automatic: `RuntimeFeatureSettings` serializes the whole `settings`
  array by `xmlName`, so no read/write code changes are needed.
- There is intentionally no `g_seven_segment.cpp` / `seven_segment.json` entry — the item
  is never displayed on 7SEG.
