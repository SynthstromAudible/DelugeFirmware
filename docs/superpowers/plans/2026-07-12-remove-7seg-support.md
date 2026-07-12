# Remove 7SEG Display Support — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Drop 7-segment display hardware support from `next`, leaving a minimal "tombstone" that tells a 7SEG user to upgrade to OLED, and collapse the two-display abstraction into a single concrete OLED display.

**Architecture:** 7SEG is currently runtime-dispatched — one binary boots on either panel and picks a `Display` subclass from a boot probe. We remove the emulated-display feature first (which is what makes the display type a boot-time constant), then halt early on 7SEG hardware with a static message, then delete the `SevenSegment` subclass and let the compiler enumerate every call site that must be fixed.

**Tech Stack:** C++20, CMake (`GLOB_RECURSE` sources), CppUTest unit tests, `dbt` build wrapper, Renesas RZ/A1 target.

**Spec:** `docs/superpowers/specs/2026-07-12-remove-7seg-support-design.md`

## Global Constraints

- **Build command:** `./dbt build Debug` — this is the primary verification gate. Debug enables beta asserts. Run it at the end of every task.
- **Test command:** `./dbt test` — the CppUTest host suite. Must stay green.
- **The build IS the test.** This is a deletion/refactor plan on embedded firmware. There is no meaningful failing-test-first cycle for "delete a no-op virtual"; the compiler is the oracle. Where a step *can* be guarded by a test, it is. Do not invent tests that assert deleted code is deleted.
- **Declaration-first removal is mandatory.** For every 7SEG-only virtual, delete the declaration from `Display` *before* touching any call site. The resulting build errors are the work list. Never grep-and-delete call sites first — you will miss the unguarded ones.
- **Sources are `GLOB_RECURSE`** (`src/deluge/CMakeLists.txt:8`). Deleting a `.cpp` needs no CMake edit. The one exception is the l10n language list (Task 4).
- **Never change the OLED-visible string at a `shortLong` popup site.** Collapsing picks `shortLong[1]` (the long form) — the string an OLED user already sees today. If a site looks like it wants `[0]`, stop and ask.
- **Commit after every task.** Conventional commit prefixes (`feat:`, `refactor:`, `chore:`), matching repo style.

---

## File Structure

**Created:**
- `src/deluge/hid/display/seven_segment_tombstone.{cpp,h}` — ~60 lines. Segment table + one "show 4 chars and halt" function. Not a display driver; not a `Display` subclass.

**Deleted:**
- `src/deluge/hid/display/seven_segment.{cpp,h}` (~840 lines)
- `src/deluge/hid/display/numeric_layer/` (5 classes, ~490 lines)
- `src/deluge/gui/l10n/g_seven_segment.cpp`, `src/deluge/gui/l10n/seven_segment.json` (~1150 lines)
- `src/deluge/gui/menu_item/runtime_feature/emulated_display.{cpp,h}` (~50 lines)

**Heavily modified:**
- `src/deluge/hid/display/display.{cpp,h}` — the abstraction collapse
- `src/deluge/deluge.cpp` — boot flow + tombstone halt
- `src/deluge/hid/hid_sysex.{cpp,h}` — 7SEG sysex protocol removal
- `src/deluge/hid/buttons.cpp`, `src/deluge/hid/display/oled.{cpp,h}`
- `src/deluge/model/settings/runtime_feature_settings.{cpp,h}`
- ~42 files carrying `have7SEG()` / `haveOLED()` branches
- `tests/unit/mocks/display_mock.h`, `tests/32bit_unit_tests/mocks/mock_display.cpp`

---

## Task 1: Remove the emulated-display runtime feature

**Why first:** While this feature exists, `display->have7SEG()` can be true on OLED hardware. No `have7SEG()` branch is safely collapsible until it's gone. This task is what makes the display type a boot-time constant.

**Files:**
- Delete: `src/deluge/gui/menu_item/runtime_feature/emulated_display.cpp`, `emulated_display.h`
- Modify: `src/deluge/gui/menu_item/runtime_feature/settings.cpp:20,43,68`
- Modify: `src/deluge/model/settings/runtime_feature_settings.h:41,58`
- Modify: `src/deluge/model/settings/runtime_feature_settings.cpp:75-96,148-151`
- Modify: `src/deluge/deluge.cpp:714-717`
- Modify: `src/deluge/hid/buttons.cpp:109-120`
- Modify: `src/deluge/hid/hid_sysex.cpp:71-75`
- Modify: `src/deluge/hid/display/display.cpp:89-104` (`swapDisplayType`), `display.h:126`
- Modify: `src/deluge/hid/display/oled.cpp:768`, `oled.h:95` (`renderEmulated7Seg`)
- Modify: `src/deluge/hid/display/seven_segment.cpp:662-667` (drop the emulation branch)
- Modify: `src/deluge/gui/l10n/english.json`, `seven_segment.json`, `strings.h:559` (drop `STRING_FOR_COMMUNITY_FEATURE_EMULATED_DISPLAY`)

**Interfaces:**
- Consumes: nothing.
- Produces: after this task, `display->haveOLED() == deluge::hid::display::have_oled_screen` always. `swapDisplayType()` and `RuntimeFeatureSettingType::EmulatedDisplay` no longer exist.

- [ ] **Step 1: Delete the menu item files**

```bash
git rm src/deluge/gui/menu_item/runtime_feature/emulated_display.cpp \
       src/deluge/gui/menu_item/runtime_feature/emulated_display.h
```

- [ ] **Step 2: Unregister the menu item**

In `src/deluge/gui/menu_item/runtime_feature/settings.cpp`, remove all three references: the `#include "emulated_display.h"` (line 20), the `EmulatedDisplay menuEmulatedDisplay{};` definition (line 43), and the `&menuEmulatedDisplay,` entry in the menu array (line 68).

- [ ] **Step 3: Remove the runtime setting**

In `src/deluge/model/settings/runtime_feature_settings.h`, delete the enum:

```cpp
enum RuntimeFeatureStateEmulatedDisplay : uint32_t { Hardware = 0, Toggle = 1, OnBoot = 2 };
```

and the `EmulatedDisplay,` entry from `RuntimeFeatureSettingType` (line 58).

In `src/deluge/model/settings/runtime_feature_settings.cpp`, delete the whole `SetupEmulatedDisplaySetting()` function (lines ~75-96) and its call site (lines ~148-151).

Settings are serialised by XML name, not enum index (`readSettingsFromFile()` matches on `xmlName`), so removing this entry does not corrupt existing `CommunityFeatures.XML` files — the stale `emulatedDisplay` tag is simply not matched and is ignored.

- [ ] **Step 4: Remove the boot-time swap**

In `src/deluge/deluge.cpp`, delete:

```cpp
	if (runtimeFeatureSettings.get(RuntimeFeatureSettingType::EmulatedDisplay)
	    == RuntimeFeatureStateEmulatedDisplay::OnBoot) {
		deluge::hid::display::swapDisplayType();
	}
```

- [ ] **Step 5: Remove the button shortcut**

In `src/deluge/hid/buttons.cpp`, the `AFFECT_ENTIRE` handler currently reads:

```cpp
	if (b == AFFECT_ENTIRE) {
		if (on) {
			display->cancelPopup();
		}
		if (on && isShiftButtonPressed() && isButtonPressed(LEARN)) {
			if (runtimeFeatureSettings.get(RuntimeFeatureSettingType::EmulatedDisplay)
			    != RuntimeFeatureStateEmulatedDisplay::Hardware) {
				deluge::hid::display::swapDisplayType();
				goto dealtWith;
			}
		}
	}
```

Reduce it to:

```cpp
	if (b == AFFECT_ENTIRE) {
		if (on) {
			display->cancelPopup();
		}
	}
```

Note this frees the Shift+Learn+AffectEntire chord. Do not repurpose it here.

- [ ] **Step 6: Remove the sysex SWAP command**

In `src/deluge/hid/hid_sysex.cpp`, delete the SWAP branch:

```cpp
	// else if (data[4] == 4) { // SWAP
	else if (data[2] == 4) { // SWAP
		deluge::hid::display::swapDisplayType();
		oledDeltaForce = true;
	}
```

This removes command `4` from the display sysex protocol. External display-mirroring tools that send it will stop getting a response; that is intended and must be called out in the PR description.

- [ ] **Step 7: Remove `swapDisplayType` and the 7SEG emulation renderer**

In `src/deluge/hid/display/display.cpp`, delete the entire `swapDisplayType()` function (lines ~89-104) and its declaration in `display.h:126`. Remove the now-unused `#include "gui/ui/ui.h"` if nothing else in the file needs it.

In `src/deluge/hid/display/oled.cpp`, delete `OLED::renderEmulated7Seg()` (line 768) and its declaration in `oled.h:95`.

In `src/deluge/hid/display/seven_segment.cpp`, `SevenSegment::render()` currently branches on `have_oled_screen` to decide whether to emulate. Collapse it to the hardware path:

```cpp
	deluge_display_write_seven_segment(segments.data(), segments.size());
	HIDSysex::sendDisplayIfChanged();
```

- [ ] **Step 8: Remove the l10n string**

Delete `STRING_FOR_COMMUNITY_FEATURE_EMULATED_DISPLAY` from `src/deluge/gui/l10n/strings.h:559`, from `english.json`, and from `seven_segment.json`. The generated `g_*.cpp` files are build artifacts — do not hand-edit them; they regenerate.

- [ ] **Step 9: Build**

```bash
./dbt build Debug
```

Expected: clean build. If anything still references `swapDisplayType`, `EmulatedDisplay`, or `renderEmulated7Seg`, the linker or compiler will say so — fix and rebuild.

- [ ] **Step 10: Run tests**

```bash
./dbt test
```

Expected: PASS.

- [ ] **Step 11: Commit**

```bash
git add -A
git commit -m "refactor: remove emulated-display runtime feature

The 7SEG emulation toggle meant display->have7SEG() could be true on OLED
hardware, which blocks constant-folding the display-type branches. Removing
it makes the display type a boot-time constant.

Removes sysex display command 4 (SWAP) and the Shift+Learn+AffectEntire chord."
```

---

## Task 2: Add the tombstone and halt 7SEG boot

**Why now:** With the display type now a boot constant, we can formally drop 7SEG hardware. After this task, `have_oled` is guaranteed true past the halt point, which is what licenses every collapse in Tasks 5-8.

**Files:**
- Create: `src/deluge/hid/display/seven_segment_tombstone.h`, `seven_segment_tombstone.cpp`
- Modify: `src/deluge/deluge.cpp:626-658`

**Interfaces:**
- Consumes: `deluge_display_write_seven_segment(const uint8_t* digits, uint8_t count)` and `deluge_control_flush()` from `libdeluge/display.h` — these are BSP primitives in `src/bsp/rza1/control_surface.cpp` and are **out of scope** (the same PIC drives the pads and LEDs).
- Produces: `void deluge::hid::display::tombstoneAndHalt()` — writes `OLED` to the 7-segment panel and never returns.

- [ ] **Step 1: Write the tombstone header**

Create `src/deluge/hid/display/seven_segment_tombstone.h`:

```cpp
#pragma once

namespace deluge::hid::display {

/// Displays a static "OLED" message on the 7-segment panel and halts forever.
///
/// This firmware does not support 7-segment hardware. This exists solely so a user who
/// flashes it onto a 7SEG unit sees why it stopped, rather than concluding they bricked
/// the device. It is not a display driver — no layers, popups, blinking, scrolling, or
/// localization. Do not grow it.
[[noreturn]] void tombstoneAndHalt();

} // namespace deluge::hid::display
```

- [ ] **Step 2: Write the tombstone implementation**

Create `src/deluge/hid/display/seven_segment_tombstone.cpp`. The segment encoding is lifted from the uppercase block of the old `letterSegments` table in `seven_segment.cpp:62`; the halt loop mirrors the old `SevenSegment::freezeWithError()` (`seven_segment.cpp:698`), minus the resume path — there is nothing to resume into.

```cpp
#include "hid/display/seven_segment_tombstone.h"
#include "board_layout.hpp" // kNumericDisplayLength == 4
#include <array>
#include <cstdint>

#include "libdeluge/control_surface.h" // deluge_control_flush
#include "libdeluge/display.h"         // deluge_display_write_seven_segment

namespace deluge::hid::display {

// Segment bits, matching the old SevenSegment driver:
//
//  -1-
// |   |
// 6   2
// |   |
//  -7-
// |   |
// 5   3
// |   |
//  -4-  .0
//
// 'O', 'L', 'E', 'D' — the only message this firmware can show on a 7-segment panel.
constexpr std::array<uint8_t, kNumericDisplayLength> kUpgradeMessage = {
    0x1D, // O
    0x0E, // L
    0x4F, // E
    0x3D, // D
};

[[noreturn]] void tombstoneAndHalt() {
	deluge_display_write_seven_segment(kUpgradeMessage.data(), kUpgradeMessage.size());

	// The PIC latches the segment data on flush, so we must keep flushing: a single
	// pre-halt flush can be dropped if the PIC is not yet ready to accept it.
	while (true) {
		deluge_control_flush();
	}
}

} // namespace deluge::hid::display
```

These names are verified against the current tree: `kNumericDisplayLength = 4` is defined in `src/board_layout.hpp:34`, `deluge_control_flush()` in `include/libdeluge/control_surface.h:90`, and `deluge_display_write_seven_segment()` in `include/libdeluge/display.h`. `seven_segment.cpp` is still present at this point in the plan if you need to cross-check.

- [ ] **Step 3: Wire the halt into boot**

In `src/deluge/deluge.cpp`, `deluge_boot()` currently reads:

```cpp
static void deluge_boot(const DelugeBoard* board) {
	(void)board;
	bool have_oled = deluge_board_probe_oled();

	// Give the control surface its startup configuration.
	if (have_oled) {
		deluge_control_enable_oled();
	}

	deluge_control_init();

	PadLEDs::setRefreshTime(23);

	deluge_control_flush();
	...
	if (have_oled) {
		deluge_display_init(); // Set up OLED now
		display = new deluge::hid::display::OLED;
	}
	else {
		display = new deluge::hid::display::SevenSegment;
	}
	// remember the physical display type
	deluge::hid::display::have_oled_screen = have_oled;
```

Change it to halt right after the control surface is up — the PIC drives the segments, so the tombstone cannot run before `deluge_control_init()`, but it must run before audio, SD, song load, and task registration:

```cpp
static void deluge_boot(const DelugeBoard* board) {
	(void)board;

	if (!deluge_board_probe_oled()) {
		// This firmware does not support 7-segment hardware. Tell the user and stop,
		// before we bring up audio, SD, or the task manager.
		deluge_control_init();
		deluge::hid::display::tombstoneAndHalt();
	}

	// Give the control surface its startup configuration.
	deluge_control_enable_oled();

	deluge_control_init();

	PadLEDs::setRefreshTime(23);

	deluge_control_flush();
	...
	deluge_display_init(); // Set up OLED now
	display = new deluge::hid::display::OLED;
```

Delete the `have_oled` local and the `have_oled_screen` assignment. Add `#include "hid/display/seven_segment_tombstone.h"`.

Leave `deluge::hid::display::have_oled_screen` itself in place for now — Task 5 removes it and its readers together.

- [ ] **Step 4: Build**

```bash
./dbt build Debug
```

Expected: clean build. `SevenSegment` is still compiled at this point (it is still referenced by `hid_sysex.cpp` and `display.cpp`); that is fine and expected — it becomes unreachable, not uncompiled. Task 3 deletes it.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: drop 7SEG hardware support, halt with upgrade message

7-segment units are no longer supported. Boot now halts early on 7SEG
hardware showing 'OLED', so a user who flashes this build sees why it
stopped instead of concluding the unit is bricked.

BREAKING: this firmware will not run on 7-segment Deluge units."
```

---

## Task 3: Delete the SevenSegment driver, numeric layers, and 7SEG sysex

**Why now:** This is the deliberate demolition. The build will break loudly and comprehensively — that is the intent, and the errors are the work list for Tasks 5-8. Do **not** try to get the build green inside this task.

**Files:**
- Delete: `src/deluge/hid/display/seven_segment.cpp`, `seven_segment.h`
- Delete: `src/deluge/hid/display/numeric_layer/` (whole directory)
- Modify: `src/deluge/hid/hid_sysex.cpp:31,65-69,93-97,135-146`, `hid_sysex.h:6,10`

**Interfaces:**
- Consumes: Task 2's guarantee that `SevenSegment` is unreachable at runtime.
- Produces: a broken build. Every remaining reference to `SevenSegment`, `NumericLayer`, or the 7SEG virtuals is now a compiler error.

- [ ] **Step 1: Delete the driver and the numeric layer stack**

```bash
git rm src/deluge/hid/display/seven_segment.cpp \
       src/deluge/hid/display/seven_segment.h
git rm -r src/deluge/hid/display/numeric_layer/
```

- [ ] **Step 2: Remove the 7SEG sysex protocol**

In `src/deluge/hid/hid_sysex.h`, delete both declarations:

```cpp
void request7SegDisplay(MIDICable& cable, uint8_t* data, int32_t len);
void send7SegData(MIDICable& cable);
```

In `src/deluge/hid/hid_sysex.cpp`, delete `request7SegDisplay()` (line ~135) and `send7SegData()` (line ~141) entirely, plus:
- the dispatch to `request7SegDisplay(cable, data, len);` (line ~31)
- the `if (force && display->have7SEG()) { send7SegData(cable); }` block (lines ~67-69)
- the `if (display->have7SEG()) { send7SegData(*midiDisplayCable); }` block (lines ~95-96)

`send7SegData` is the only consumer of `Display::getLast()`, which Task 6 removes.

This drops the 7SEG display-mirroring sysex request from the protocol. Same caveat as Task 1 Step 6: external mirroring tools lose this capability, intentionally. It goes in the PR description.

- [ ] **Step 3: Confirm the build breaks, and capture the work list**

```bash
./dbt build Debug 2>&1 | tee /tmp/7seg-fallout.txt
```

Expected: FAIL, with many errors. This file is your checklist for Tasks 5-8. Do not fix anything yet.

- [ ] **Step 4: Commit the demolition**

The build is red at this commit. That is deliberate and it is why this is *one PR*, not one-commit-per-merge. Note it in the message so a future bisector is not confused:

```bash
git add -A
git commit -m "refactor: delete SevenSegment driver, numeric layers, and 7SEG sysex

Build is intentionally RED at this commit; the following commits fix the
fallout. Bisecting into this commit alone will not build."
```

---

## Task 4: Delete the 7SEG localization table

**Files:**
- Delete: `src/deluge/gui/l10n/seven_segment.json`, `g_seven_segment.cpp`
- Modify: `src/deluge/gui/l10n/CMakeLists.txt:4`
- Modify: `src/deluge/gui/l10n/language.h:60`
- Modify: `src/deluge/hid/display/oled.h:46-48`
- Modify: `src/deluge/gui/l10n/generate.py` (only if it special-cases 7SEG)

**Interfaces:**
- Produces: `l10n::chosenLanguage` is unconditionally English. `deluge::l10n::built_in::seven_segment` no longer exists.

- [ ] **Step 1: Delete the table and its source**

```bash
git rm src/deluge/gui/l10n/seven_segment.json src/deluge/gui/l10n/g_seven_segment.cpp
```

`g_seven_segment.cpp` is generated, but it is checked in, so it must be removed from git as well as from the build.

- [ ] **Step 2: Drop it from the build**

In `src/deluge/gui/l10n/CMakeLists.txt`, change:

```cmake
set(LANGUAGES english seven_segment)
```

to:

```cmake
set(LANGUAGES english)
```

- [ ] **Step 3: Remove the Language declaration**

In `src/deluge/gui/l10n/language.h`, delete:

```cpp
extern deluge::l10n::Language seven_segment;
```

- [ ] **Step 4: Simplify the OLED language selection**

`src/deluge/hid/display/oled.h:46-48` currently guards against inheriting the 7SEG language:

```cpp
		if (l10n::chosenLanguage == nullptr || l10n::chosenLanguage == &l10n::built_in::seven_segment) {
			l10n::chosenLanguage = &l10n::built_in::english;
		}
```

There is now only one language, so this reduces to:

```cpp
		l10n::chosenLanguage = &l10n::built_in::english;
```

- [ ] **Step 5: Check the generator**

```bash
grep -n "seven\|7seg" src/deluge/gui/l10n/generate.py
```

If it has no 7SEG-specific branch, leave it alone — it is parameterised by the `LANGUAGES` list and needs no change. If it does, remove that branch.

- [ ] **Step 6: Commit**

The build is still red (Tasks 5-8 fix that). Do not build here; you would only re-read the same fallout.

```bash
git add -A
git commit -m "refactor: delete 7SEG localization table

Build still RED; see previous commit."
```

---

## Task 5: Collapse the display-type branches

**Files:**
- Modify: ~42 files containing `have7SEG()` (68 sites) or `haveOLED()` (277 sites)
- Modify: `src/deluge/hid/display/display.cpp:87` (`have_oled_screen`)
- Modify: `src/deluge/deluge.cpp:317,566,585`, `src/deluge/gui/ui_timer_manager.cpp:224`, `src/deluge/model/settings/runtime_feature_settings.cpp:82`

**Interfaces:**
- Consumes: Task 2's guarantee that the display is always OLED past boot.
- Produces: no `have7SEG()` / `haveOLED()` / `have_oled_screen` call sites remain. The methods themselves still exist (Task 8 removes them).

- [ ] **Step 1: Enumerate the sites**

```bash
grep -rn "have7SEG()\|haveOLED()\|have_oled_screen" src/deluge/ | tee /tmp/7seg-branches.txt
wc -l /tmp/7seg-branches.txt
```

Expected: roughly 345 lines.

- [ ] **Step 2: Collapse, file by file**

Work through the list. The mechanical rules:

| Pattern | Becomes |
|---|---|
| `if (display->haveOLED()) { A }` | `A` (unwrapped) |
| `if (display->have7SEG()) { A }` | deleted |
| `if (display->haveOLED()) { A } else { B }` | `A` (unwrapped), `B` deleted |
| `if (display->have7SEG()) { A } else { B }` | `B` (unwrapped), `A` deleted |
| `display->haveOLED() ? A : B` | `A` |
| `display->have7SEG() ? A : B` | `B` |
| `have_oled_screen` (as a condition) | `true` — unwrap the same way |

Two things to watch for, which are the reason this is a per-file review and not a `sed`:

1. **`else` pairs where the 7SEG arm set up state the OLED arm assumed.** If deleting the 7SEG arm removes an assignment that the code below the `if` reads, you have found a real dependency — do not paper over it. Stop and reason about it.
2. **Early returns inside a 7SEG arm.** `if (display->have7SEG()) { return; }` is a control-flow change, not a no-op deletion. Deleting it correctly means the following code now runs — which is what we want, but check that it is safe to reach.

Do **not** delete `display->` calls that are not display-type predicates in this task. `setText` and friends are Task 6.

- [ ] **Step 3: Remove `have_oled_screen`**

Once no readers remain, delete the definition in `src/deluge/hid/display/display.cpp:87` and the `extern` in `display.h:128`.

- [ ] **Step 4: Verify no sites remain**

```bash
grep -rn "have7SEG()\|haveOLED()\|have_oled_screen" src/deluge/
```

Expected: **no output** (the definitions in `display.h` remain but are not call sites; if the grep still shows `display.h`, that is fine — Task 8 removes them).

- [ ] **Step 5: Build**

```bash
./dbt build Debug
```

Expected: still FAIL, but only on the 7SEG-only virtuals (`setText`, `setScrollingText`, etc.) and `NumericLayer`. If you see errors about `have7SEG` or `have_oled_screen`, you missed a site.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "refactor: collapse display-type branches to their OLED arm

Build still RED; the 7SEG-only Display virtuals remain."
```

---

## Task 6: Remove the 7SEG-only Display virtuals, declaration-first

**This is the highest-risk task in the plan.** These virtuals have **empty default bodies** in `Display` (`display.h:97-112`), so an unguarded call to one of them on OLED hardware *already does nothing today*. That is what makes deleting the call sites behavior-preserving — and it is also why they look like live code. Do not trust your eyes; make the compiler enumerate them.

**Files:**
- Modify: `src/deluge/hid/display/display.h:76,97-112` (the declarations)
- Modify: every file the compiler names
- Modify: `tests/unit/mocks/display_mock.h`, `tests/32bit_unit_tests/mocks/mock_display.cpp`

**Interfaces:**
- Consumes: Task 5's collapse (so any surviving call is genuinely unguarded, not 7SEG-only).
- Produces: `Display` has no 7SEG-only methods.

The virtuals, with current call-site counts:

| Virtual | Sites | Base-class body |
|---|---|---|
| `setText` | 73 | empty |
| `setScrollingText` | 35 | `return nullptr;` |
| `setTextAsNumber` | 12 | empty |
| `getEncodedPosFromLeft` | 4 | `return 0;` |
| `setTextAsSlot` | 3 | empty |
| `setTextWithMultipleDots` | 1 | empty |
| `getLast` | 1 | `return {0};` |
| `setNextTransitionDirection` | — | empty |

- [ ] **Step 1: Delete ONE virtual's declaration**

Start with the smallest. In `src/deluge/hid/display/display.h`, delete `getLast()`. Do not touch any call site yet.

- [ ] **Step 2: Let the compiler find the callers**

```bash
./dbt build Debug 2>&1 | grep -E "error|getLast"
```

Every error is a call site. For each, classify it:

- **Inside a (now-removed) 7SEG guard** → should not exist; Task 5 removed those. If you find one, Task 5 missed a site.
- **Unguarded** → it was already a no-op on OLED. Deleting the call is behavior-preserving. Delete it.
- **Its return value feeds real logic** → STOP. This is the case the plan's central assumption does not cover. Do not delete it; work out what the OLED behavior should be, and raise it in the PR.

`getLast()`'s only caller was `send7SegData()`, already deleted in Task 3, so expect zero errors here. That is the point of starting with it: it proves the loop.

- [ ] **Step 3: Repeat for each remaining virtual, one at a time**

In this order (ascending by blast radius): `setTextWithMultipleDots`, `setTextAsSlot`, `getEncodedPosFromLeft`, `setTextAsNumber`, `setNextTransitionDirection`, `setScrollingText`, `setText`.

For each: delete the declaration → build → fix every named site → build clean → move to the next. **Do not batch them.** The whole value of this task is that the error list stays small and attributable.

`setScrollingText` returns `NumericLayerScrollingText*`, a type deleted in Task 3. Any caller holding that pointer must lose the variable too, not just the call.

- [ ] **Step 4: Fix the test mocks**

`tests/unit/mocks/display_mock.h` is a standalone stub:

```cpp
class Display {
public:
	bool have7SEG() { return false; }
};
```

It has no other members. Its `have7SEG()` is dead once Task 5 lands, but check whether anything still calls it:

```bash
grep -rn "have7SEG\|display->" tests/unit/
```

If nothing does, delete the method, leaving an empty `Display` stub (or delete the mock entirely if nothing includes it).

`tests/32bit_unit_tests/mocks/mock_display.cpp` derives from the real `deluge::hid::Display` and constructs with `DisplayType::SEVENSEG`. It must lose every override of a deleted virtual. Its constructor is fixed in Task 8, when `DisplayType` goes.

- [ ] **Step 5: Build and test**

```bash
./dbt build Debug && ./dbt test
```

Expected: **the build finally goes green here.** This is the first green build since Task 2. Tests PASS.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "refactor: remove 7SEG-only Display virtuals and their dead call sites

These virtuals had empty default bodies, so unguarded calls to them were
already no-ops on OLED hardware. Removed declaration-first so the compiler
enumerated every call site rather than relying on grep."
```

---

## Task 7: Audit the `shortLong` popups

**The compiler cannot help with this task.** Both arms typecheck. A mistake here silently changes what the user reads on screen. This is the only task in the plan with genuine behavioral content.

**Files:**
- Modify: `src/deluge/hid/display/display.h:65-69`
- Modify: every `displayPopup(shortLong)` call site

**Interfaces:**
- Produces: the `char const*[2]` popup overload no longer exists; all callers pass a single `char const*`.

The overload today:

```cpp
	virtual void displayPopup(char const* shortLong[2], int8_t numFlashes = 3, bool alignRight = false,
	                          uint8_t drawDot = 255, int32_t blinkSpeed = 1, PopupType type = PopupType::GENERAL) {
		displayPopup(have7SEG() ? shortLong[0] : shortLong[1], numFlashes, alignRight, drawDot, blinkSpeed, type);
	}
```

`shortLong[0]` is the 4-character abbreviation; `shortLong[1]` is the full string an OLED user sees today. **Collapsing always keeps `[1]`.**

- [ ] **Step 1: Find every caller**

The array is usually a local, so grep for the overload's use rather than a literal:

```bash
grep -rn "displayPopup" src/deluge/ | grep -vE "displayPopup\((\"|l10n::|[a-z]+Str|[0-9])" | tee /tmp/shortlong.txt
```

Then, for each candidate, confirm it is passing a two-element array by reading the surrounding declaration. Be thorough — a missed site becomes a build error in Step 3 anyway, but a *misclassified* one does not.

- [ ] **Step 2: Rewrite each site to pass the long form**

For each caller, replace the array with the long string directly. Concretely, a site like:

```cpp
	char const* shortLong[2] = {"SIDE", "Sidechain"};
	display->displayPopup(shortLong);
```

becomes:

```cpp
	display->displayPopup("Sidechain");
```

Delete the now-unused array. **If a site's `[0]` and `[1]` are not an abbreviation/full-string pair — if they differ in meaning rather than length — stop and raise it rather than guessing.**

- [ ] **Step 3: Delete the overload**

Remove the `char const* shortLong[2]` overload from `display.h`. The build now catches any site you missed in Step 1.

```bash
./dbt build Debug
```

Expected: clean. Any error is a `shortLong` site you missed — fix it per Step 2.

- [ ] **Step 4: Verify the strings against `next`**

For each site you changed, confirm the string now shown matches what `git show next:<file>` had in `shortLong[1]`:

```bash
git diff next --stat
```

Read the diff of every changed popup line. This is a human check and it is the point of the task.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "refactor: collapse shortLong popups to the full string

Every site keeps shortLong[1], the string OLED users already saw. The
4-character abbreviations are gone with the 7SEG display."
```

---

## Task 8: Flatten the Display abstraction

Pure refactor. No behavior change. If this task changes what any user sees, it is wrong.

**Files:**
- Modify: `src/deluge/hid/display/display.h`, `display.cpp`
- Modify: `src/deluge/hid/display/oled.h`, `oled.cpp`
- Modify: `tests/32bit_unit_tests/mocks/mock_display.cpp`

**Interfaces:**
- Produces: `deluge::hid::Display` is a single concrete class. `DisplayType`, `have7SEG()`, `haveOLED()` are gone. The global `display` pointer keeps its name and type.

- [ ] **Step 1: Remove the display-type machinery**

In `src/deluge/hid/display/display.h`, delete:

```cpp
enum struct DisplayType { OLED, SEVENSEG };
```

and:

```cpp
	bool haveOLED() { return displayType == DisplayType::OLED; }
	bool have7SEG() { return displayType == DisplayType::SEVENSEG; }

private:
	DisplayType displayType;
```

and the constructor `Display(DisplayType displayType) : displayType(displayType) {}`.

- [ ] **Step 2: Fix the OLED constructor and the mock**

`OLED`'s constructor passes `DisplayType::OLED` to the base — drop that argument. Same for `MockDisplay` in `tests/32bit_unit_tests/mocks/mock_display.cpp`, which currently passes `DisplayType::SEVENSEG`.

- [ ] **Step 3: De-virtualise**

With one subclass, the remaining `virtual` methods on `Display` are dead weight. Keep the base/subclass split (`OLED : Display`) — flattening it into a single class would touch every `#include` in the tree for no behavioral gain, and the spec explicitly limits scope to removing 7SEG.

Remove `virtual` and `= 0` from the `Display` methods only where the method has exactly one implementation and no override remains. If in doubt, leave `virtual` — it costs a vtable slot, not correctness, and this is the lowest-value part of the plan.

- [ ] **Step 4: Build and test**

```bash
./dbt build Debug && ./dbt test
```

Expected: clean build, tests PASS.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "refactor: flatten Display to a single concrete implementation"
```

---

## Task 9: Tooling, docs, and the release note

**Files:**
- Modify: `dbt` (the `gdb [7seg|oled]` subcommand)
- Modify: `docs/community_features.md`
- Modify: the changelog / release notes

- [ ] **Step 1: Remove the 7SEG gdb target**

`./dbt gdb` currently takes a `{7seg,oled}` target argument. Find and remove the 7SEG option:

```bash
grep -rn "7seg" dbt scripts/ 2>/dev/null
```

Remove the choice and any 7SEG-specific linker/target config it selects. If it turns out `7seg` selects a distinct *build* target rather than just a debug symbol path, stop — that is a bigger finding than this task assumes, and it belongs in its own task.

- [ ] **Step 2: Remove the emulated-display docs**

In `docs/community_features.md`, delete the "Emulated Display" section (removed in Task 1).

- [ ] **Step 3: Write the release note**

This is a **one-way door for 7SEG users** and the note must lead with it, not bury it. Someone who auto-updates and gets a dead panel with no warning is the failure case that turns an upgrade incentive into a support fire. Draft:

> ### BREAKING: 7-segment display support removed
>
> This release does not run on Deluge units with the original 4-digit (7-segment) display. Flashing it onto a 7SEG unit will show `OLED` on the display and stop; the unit is **not** damaged and can be recovered by flashing a previous release.
>
> An official OLED upgrade is available for 7SEG units. 7SEG users who do not wish to upgrade should stay on <last supporting release>.
>
> Also removed: the Emulated Display community feature, the Shift+Learn+AffectEntire display-swap shortcut, and sysex display commands for 7SEG mirroring (`SWAP`, 7SEG data request).

Fill in `<last supporting release>` with the actual version.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "docs: remove 7SEG from tooling and document the hardware drop"
```

---

## Task 10: Hardware verification

Neither the compiler nor the test suite can cover this. **Both passes are merge gates.**

- [ ] **Step 1: OLED hardware pass (Kate's unit)**

Flash the build and exercise the flows that leaned hardest on the removed virtuals — a silently-deleted `setText` shows up here as a blank or stale screen:

- **Sample browser** — scrolling filenames (was `setScrollingText`, 35 sites)
- **Song load / save naming** — text entry and display (was `setText` + the old slot helpers)
- **The menu system** — every submenu, value editing (was `drawValue`, 114 sites)
- **Popups in all overloads** — including every string touched in Task 7
- **Error display** — trigger a `freezeWithError` path and confirm the OLED still renders it

- [ ] **Step 2: 7SEG tombstone pass (teammate's unit)**

Flash the build onto a 7SEG unit. Confirm:

- The display reads `OLED` and stays lit (it must not flicker, scroll, or blank)
- The unit does not appear to boot further — no audio, no pad animation
- **Flashing a previous release recovers the unit fully.** This is the single most important check in the plan; if a 7SEG user cannot roll back, the tombstone is a brick, not a message.

- [ ] **Step 3: Record the results in the PR**

Both passes, explicitly, with who ran them. Do not merge on "should be fine."

---

## Notes for the implementer

- **The load-bearing assumption:** the 7SEG-only virtuals have empty base-class bodies, so unguarded calls to them are already no-ops on OLED. Task 6 rests entirely on this. It is verified for the current `display.h`, but if you find a call site whose *return value* feeds real logic, that assumption does not cover it — stop and raise it rather than deleting.
- **The build is red from Task 3 through Task 6.** That is by design. Do not "fix" it early by stubbing things out; the errors are the work list.
- **Slot/naming code:** the slot model was already removed and names are display-agnostic (issue #1069), which eliminates what would otherwise be the nastiest tangle. Re-confirm during Task 6 rather than assume.
- **Out of scope:** `src/bsp/rza1/control_surface.cpp` and the PIC layer (still drives pads and LEDs), `deluge_board_probe_oled()` (still needed for the halt), and any unrelated cleanup the collapse walks past.
