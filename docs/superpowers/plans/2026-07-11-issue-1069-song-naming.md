# Issue #1069 — Remove the Slot Model from Song Naming: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make song naming display-agnostic so that a 7-seg Deluge produces `SONG185 → SONG185A → SONG185B` (as OLED already does) instead of the corrupt `185_186`, and delete the slot data-model that caused it.

**Architecture:** One invariant drives everything: `enteredText` and `FileItem::displayName` always hold the real on-card name (`SONG185`) on both displays; the `SONG`/`nnn` split becomes a *rendering* concern only. The name-derivation logic is lifted out of the 270-line `goto` tangle in `Browser::arrivedInNewFolder()` into a pure, unit-tested helper behind a tiny `FileListView` seam.

**Tech Stack:** C++23, CppUTest (`tests/unit`), CMake. Firmware built with `./dbt build Debug`.

**Spec:** `docs/superpowers/specs/2026-07-11-issue-1069-song-naming-design.md`

## Global Constraints

- **Card compatibility is non-negotiable.** 7-seg must still write `SONG185.XML` for song 185. The change is that `enteredText` becomes `"SONG185"` instead of `"185"` — the bytes on disk are identical. A card written by this firmware must still load on vanilla.
- **Names must never depend on the display.** This is the whole point of the change. Any code that produces a *filename* differently on 7-seg vs OLED is a bug. (Rendering may differ; naming may not.)
- **The `"SONG"` literal at `browser.cpp:691` stays a literal.** Presets (`SYNT`/`KIT`) keep the numeric-suffix path. Do not generalise the letter suffix to presets — that would change current OLED preset behaviour.
- **Zero-padding is out of scope.** `getUnusedSlot()` keeps its existing dynamic digit count for both displays.
- Build the firmware with `./dbt build Debug` (enables beta asserts). Unit tests: `cmake --build build --target UnitTests && ./build/tests/unit/UnitTests`.

## Decision needing confirmation before Task 2

Today the numeric-suffix delimiter is display-dependent: `browser.cpp:791` is
`display->haveOLED() ? " :" : "_:"`, so a renamed song becomes `MYTRACK 2` on OLED but
`MYTRACK_2` on 7-seg. That is display-dependent *naming*, which violates the Global
Constraint above. **This plan unifies it to a space (`" "`) on both displays**, matching
current OLED behaviour. Consequence: on 7-seg, renamed songs now increment as
`MYTRACK 2` rather than `MYTRACK_2`. Existing `MYTRACK_2` files on a card are still
found and still increment as `MYTRACK_3`, because an existing `_<number>` suffix is
detected and reused — only the delimiter chosen for a *brand new* suffix changes.

---

## File Structure

| File | Responsibility |
|---|---|
| `src/deluge/util/name_compare.h` / `.cpp` | **New.** The pure name-comparison block lifted out of `functions.cpp` (`strcmpspecial`, `getComparativeNoteNumberFromChars`, `shouldInterpretNoteNames`, `octaveStartsFromA`). No display/storage/audio deps, so unit-testable. |
| `src/deluge/gui/ui/browser/default_name.h` / `.cpp` | **New.** Pure name derivation: given a current name, a file prefix, and a `FileListView`, return the default name for the next variation. The `FileListView` seam is the only thing it knows about the file list. |
| `src/deluge/gui/ui/browser/browser.cpp` | Adapts `fileItems` to `FileListView`; loses the 7-seg `displayName` prefix-strip, the inline name-derivation, and `getUnusedSlot()`'s 7-seg branch. Gains render-time prefix shortening. |
| `src/deluge/gui/ui/browser/slot_browser.cpp` | Loses `getCurrentFilenameWithoutExtension()` and `convertToPrefixFormatIfPossible()`. |
| `src/deluge/storage/file_item.cpp` / `.h` | Loses `getDisplayNameWithoutExtension()`. |
| `src/deluge/model/song/song.h` / `.cpp` | Loses the dead `slot` / `subSlot` fields. |
| `tests/unit/name_compare_tests.cpp` | **New.** Locks in comparator ordering before it is relied upon. |
| `tests/unit/default_name_tests.cpp` | **New.** The regression tests for #1069. |
| `tests/unit/CMakeLists.txt` | Registers the two new test files and the two new sources. |

---

## Task 1: Lift the name comparator into a testable leaf module

`strcmpspecial()` is the ordering used by the browser's file list, and the name-derivation
helper in Task 2 depends on it. It currently lives in `functions.cpp`, which pulls in fatfs,
display, sound and arpeggiator headers and cannot be compiled into the unit-test target.
Lines 1651-1863 of that file are a contiguous block of pure char/string logic with none of
those dependencies. Move it out.

**Files:**
- Create: `src/deluge/util/name_compare.h`
- Create: `src/deluge/util/name_compare.cpp`
- Modify: `src/deluge/util/functions.cpp` (delete lines 1651-1863; add `#include "util/name_compare.h"`)
- Modify: `src/deluge/util/functions.h:330-331` (remove the two `extern` decls; include `name_compare.h` instead)
- Create: `tests/unit/name_compare_tests.cpp`
- Modify: `tests/unit/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces: `int32_t strcmpspecial(char const* first, char const* second)`, `extern bool shouldInterpretNoteNames`, `extern bool octaveStartsFromA`, `ComparativeNoteNumber getComparativeNoteNumberFromChars(char const*, char, bool)` — all now declared in `util/name_compare.h`. Task 2 consumes `strcmpspecial`.

- [ ] **Step 1: Read the block being moved**

Read `src/deluge/util/functions.cpp` lines 1645-1865. Confirm the block is exactly:
`getComparativeNoteNumberFromChars()` (starts ~1651), the definitions of
`shouldInterpretNoteNames` and `octaveStartsFromA` (~1712-1714), and `strcmpspecial()`
(1718 to its closing brace). Confirm `ComparativeNoteNumber` is declared in `functions.h`
and note its definition — it moves too.

- [ ] **Step 2: Create the header**

`src/deluge/util/name_compare.h`:

```cpp
/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <cstdint>

struct ComparativeNoteNumber {
	int32_t noteNumber;
	int32_t stringLength;
};

// You must set octaveStartsFromA if setting shouldInterpretNoteNames to true.
extern bool shouldInterpretNoteNames;
extern bool octaveStartsFromA;

ComparativeNoteNumber getComparativeNoteNumberFromChars(char const* string, char noteChar, bool octaveStartsFromA);

/// Orders filenames the way the browser lists them: like strcmp, except that runs of digits
/// compare as numbers rather than character-by-character, so "SONG2" sorts before "SONG10".
int32_t strcmpspecial(char const* first, char const* second);
```

If `ComparativeNoteNumber` is already defined in `functions.h`, delete it from there and
have `functions.h` `#include "util/name_compare.h"` so existing users keep compiling.

- [ ] **Step 3: Create the implementation**

`src/deluge/util/name_compare.cpp`: the same GPL header as above, then:

```cpp
#include "util/name_compare.h"
#include <cstring>
```

followed by the three definitions **moved verbatim** from `functions.cpp:1651-1863`.
Do not rewrite them — this is a pure move, and any behaviour change here would silently
reorder every browser list.

- [ ] **Step 4: Remove the block from functions.cpp**

Delete lines 1651-1863 from `src/deluge/util/functions.cpp` and add
`#include "util/name_compare.h"` to its include block.

- [ ] **Step 5: Register the new source and test with CMake**

In `tests/unit/CMakeLists.txt`, add to the `file(GLOB_RECURSE deluge_SOURCES ...)` list:

```cmake
        # For name comparison / default naming tests
        ../../src/deluge/util/name_compare.cpp
```

and add `name_compare_tests.cpp` to the `add_executable(UnitTests ...)` list.

- [ ] **Step 6: Write the characterisation test**

`tests/unit/name_compare_tests.cpp`:

```cpp
#include "CppUTest/TestHarness.h"
#include "util/name_compare.h"

TEST_GROUP(NameCompareTests){void setup() override{shouldInterpretNoteNames = false;
octaveStartsFromA = false;
}
}
;

TEST(NameCompareTests, digitRunsCompareAsNumbers) {
	// The whole point of "special": plain strcmp would put SONG10 before SONG2.
	CHECK(strcmpspecial("SONG2.XML", "SONG10.XML") < 0);
	CHECK(strcmpspecial("SONG10.XML", "SONG2.XML") > 0);
	CHECK_EQUAL(0, strcmpspecial("SONG185.XML", "SONG185.XML"));
}

TEST(NameCompareTests, letterSuffixSortsAfterBareNumber) {
	// Relied on by the subslot probe: SONG185A must sort after SONG185.
	CHECK(strcmpspecial("SONG185.XML", "SONG185A.XML") < 0);
	CHECK(strcmpspecial("SONG185A.XML", "SONG185B.XML") < 0);
}

TEST(NameCompareTests, prefixIsOrderPreserving) {
	// The invariant this whole change rests on: adding the constant "SONG" prefix to every
	// entry does not reorder them relative to each other.
	CHECK(strcmpspecial("1.XML", "2.XML") < 0);
	CHECK(strcmpspecial("SONG1.XML", "SONG2.XML") < 0);
	CHECK(strcmpspecial("2.XML", "10.XML") < 0);
	CHECK(strcmpspecial("SONG2.XML", "SONG10.XML") < 0);
}
```

- [ ] **Step 7: Run the tests**

```bash
cmake --build build --target UnitTests && ./build/tests/unit/UnitTests -g NameCompareTests -v
```

Expected: PASS. If `prefixIsOrderPreserving` fails, **stop** — the spec's "sort order is
preserved" risk has materialised and the design needs revisiting before going further.

- [ ] **Step 8: Verify the firmware still builds**

```bash
./dbt build Debug
```

Expected: builds clean. This task is a pure move; no behaviour may change.

- [ ] **Step 9: Commit**

```bash
git add src/deluge/util/name_compare.h src/deluge/util/name_compare.cpp \
        src/deluge/util/functions.cpp src/deluge/util/functions.h \
        tests/unit/name_compare_tests.cpp tests/unit/CMakeLists.txt
git commit -m "refactor: lift name comparison out of functions.cpp into a testable leaf module"
```

---

## Task 2: Pure default-name helper, with the #1069 regression tests

This is where the bug is actually fixed — but nothing is wired up yet, so the firmware's
behaviour does not change. The helper is written correctly from the start (operating on
real names throughout), and the tests lock in both the OLED behaviour we are preserving
and the 7-seg behaviour we are restoring.

**Files:**
- Create: `src/deluge/gui/ui/browser/default_name.h`
- Create: `src/deluge/gui/ui/browser/default_name.cpp`
- Create: `tests/unit/default_name_tests.cpp`
- Modify: `tests/unit/CMakeLists.txt`

**Interfaces:**
- Consumes: `strcmpspecial()` from `util/name_compare.h` (Task 1).
- Produces:
  - `class deluge::gui::browser::FileListView` with one pure virtual:
    `virtual bool contains(char const* nameWithExtension) const = 0;`
  - `std::string deluge::gui::browser::nextDefaultName(std::string_view currentName, std::string_view slotPrefix, FileListView const& files);`

  Task 4 implements `FileListView` over `Browser::fileItems` and calls `nextDefaultName`.

- [ ] **Step 1: Write the header**

`src/deluge/gui/ui/browser/default_name.h` (GPL header as in Task 1, then):

```cpp
#pragma once

#include <string>
#include <string_view>

namespace deluge::gui::browser {

/// Read-only view over the browser's file list, so name derivation can be tested without
/// a card, a display, or the browser's static state. Implemented over Browser::fileItems in
/// the firmware and over a vector in tests.
class FileListView {
public:
	virtual ~FileListView() = default;
	/// True if the list holds this exact name. The name includes its extension, matching
	/// FileItem::displayName (the CStringArray sort key).
	virtual bool contains(char const* nameWithExtension) const = 0;
};

/// The delimiter used when giving a name its first numeric suffix ("MYTRACK" -> "MYTRACK 2").
/// Deliberately NOT display-dependent: a name must never depend on which Deluge saved it.
inline constexpr char kNumericSuffixDelimiter = ' ';

/// Derives the default name for the next variation of `currentName`.
///
/// `currentName` and the result are real on-card names: no extension, and no display-specific
/// mangling (always "SONG185", never "185").
///
/// `slotPrefix` is the prefix that earns letter-suffix treatment - "SONG" for songs, and empty
/// for everything else. Presets deliberately do NOT get letter suffixes: that would change
/// current OLED preset behaviour. Pass "" and they take the numeric path.
///
/// Two forms:
///   <slotPrefix><digits>[letter]  -> next unused letter:  SONG185 -> SONG185A -> SONG185B
///   anything else                 -> next unused number:  MYTRACK -> "MYTRACK 2"
///                                                         TRACK_1 -> TRACK_2  (existing
///                                                                   delimiter is reused)
///
/// Returns `currentName` unchanged when no free variation exists (letters exhausted past Z).
std::string nextDefaultName(std::string_view currentName, std::string_view slotPrefix, FileListView const& files);

} // namespace deluge::gui::browser
```

- [ ] **Step 2: Write the failing tests**

`tests/unit/default_name_tests.cpp`:

```cpp
#include "CppUTest/TestHarness.h"
#include "gui/ui/browser/default_name.h"
#include "util/name_compare.h"
#include <string>
#include <vector>

using deluge::gui::browser::FileListView;
using deluge::gui::browser::nextDefaultName;

namespace {
/// Stands in for Browser::fileItems.
class FakeFileList : public FileListView {
public:
	explicit FakeFileList(std::vector<std::string> names) : names_{std::move(names)} {}
	bool contains(char const* nameWithExtension) const override {
		for (auto const& n : names_) {
			if (strcmpspecial(n.c_str(), nameWithExtension) == 0) {
				return true;
			}
		}
		return false;
	}

private:
	std::vector<std::string> names_;
};

/// A card holding SONG001.XML .. SONGnnn.XML, as a vanilla card looks.
FakeFileList cardWithSongsUpTo(int32_t highest) {
	std::vector<std::string> names;
	for (int32_t i = 1; i <= highest; i++) {
		char buf[32];
		snprintf(buf, sizeof buf, "SONG%03d.XML", i);
		names.emplace_back(buf);
	}
	return FakeFileList{std::move(names)};
}
} // namespace

TEST_GROUP(DefaultNameTests){void setup() override{shouldInterpretNoteNames = false;
octaveStartsFromA = false;
}
}
;

// --- Issue #1069: the actual regression -------------------------------------------------

TEST(DefaultNameTests, issue1069_defaultNamedSongGetsALetterSuffix) {
	auto files = cardWithSongsUpTo(185);
	CHECK_EQUAL(std::string{"SONG185A"}, nextDefaultName("SONG185", "SONG", files));
}

TEST(DefaultNameTests, issue1069_neverEmitsTheSlotNumberAsASuffix) {
	// The bug produced "185_186": the slot number leaking in through a wrong string offset.
	auto files = cardWithSongsUpTo(185);
	std::string result = nextDefaultName("SONG185", "SONG", files);
	CHECK(result.find("186") == std::string::npos);
	CHECK(result.find('_') == std::string::npos);
}

TEST(DefaultNameTests, letterSuffixesAdvance) {
	FakeFileList files{{"SONG185.XML", "SONG185A.XML", "SONG185B.XML"}};
	CHECK_EQUAL(std::string{"SONG185C"}, nextDefaultName("SONG185", "SONG", files));
	CHECK_EQUAL(std::string{"SONG185C"}, nextDefaultName("SONG185B", "SONG", files));
}

TEST(DefaultNameTests, letterSuffixFillsTheFirstGap) {
	FakeFileList files{{"SONG185.XML", "SONG185A.XML", "SONG185C.XML"}};
	CHECK_EQUAL(std::string{"SONG185B"}, nextDefaultName("SONG185", "SONG", files));
}

TEST(DefaultNameTests, letterSuffixExhaustedReturnsInputUnchanged) {
	std::vector<std::string> names{"SONG185.XML"};
	for (char c = 'A'; c <= 'Z'; c++) {
		names.push_back(std::string{"SONG185"} + c + ".XML");
	}
	FakeFileList files{std::move(names)};
	CHECK_EQUAL(std::string{"SONG185"}, nextDefaultName("SONG185", "SONG", files));
}

// --- Behaviour that must be preserved (this is what OLED does today) ---------------------

TEST(DefaultNameTests, renamedSongGetsANumericSuffix) {
	FakeFileList files{{"MYTRACK.XML"}};
	CHECK_EQUAL(std::string{"MYTRACK 2"}, nextDefaultName("MYTRACK", "SONG", files));
}

TEST(DefaultNameTests, numericSuffixAdvances) {
	FakeFileList files{{"MYTRACK.XML", "MYTRACK 2.XML", "MYTRACK 3.XML"}};
	CHECK_EQUAL(std::string{"MYTRACK 4"}, nextDefaultName("MYTRACK", "SONG", files));
}

TEST(DefaultNameTests, existingUnderscoreDelimiterIsReused) {
	// A name that already carries an "_<number>" suffix keeps the underscore.
	FakeFileList files{{"TRACK_1.XML"}};
	CHECK_EQUAL(std::string{"TRACK_2"}, nextDefaultName("TRACK_1", "SONG", files));
}

TEST(DefaultNameTests, nonNumericNameSharingThePrefixIsNotTreatedAsASlot) {
	// "SONGIDEA" starts with SONG but has no digits - it is just a name.
	FakeFileList files{{"SONGIDEA.XML"}};
	CHECK_EQUAL(std::string{"SONGIDEA 2"}, nextDefaultName("SONGIDEA", "SONG", files));
}

TEST(DefaultNameTests, presetsKeepTheNumericSuffix) {
	// Only songs get letter suffixes. Presets pass an empty slotPrefix, so SYNT005 takes the
	// numeric path - preserving current OLED preset behaviour. See Global Constraints.
	FakeFileList files{{"SYNT005.XML"}};
	CHECK_EQUAL(std::string{"SYNT005 2"}, nextDefaultName("SYNT005", "", files));
}
```

Add `default_name_tests.cpp` to `add_executable(UnitTests ...)` and
`../../src/deluge/gui/ui/browser/default_name.cpp` to the `deluge_SOURCES` glob in
`tests/unit/CMakeLists.txt`.

- [ ] **Step 3: Run the tests to verify they fail**

```bash
cmake --build build --target UnitTests
```

Expected: FAIL to link/compile — `nextDefaultName` is not defined.

- [ ] **Step 4: Implement the helper**

`src/deluge/gui/ui/browser/default_name.cpp` (GPL header, then):

```cpp
#include "gui/ui/browser/default_name.h"
#include <cctype>
#include <cstdio>

namespace deluge::gui::browser {

namespace {

constexpr int32_t kMaxSlotDigits = 3;
constexpr int32_t kMaxNumericSuffix = 9999;

bool exists(FileListView const& files, std::string const& nameWithoutExtension) {
	std::string probe = nameWithoutExtension + ".XML";
	return files.contains(probe.c_str());
}

/// If `name` is "<slotPrefix><1-3 digits>" optionally followed by a single letter, returns the
/// name without that trailing letter (i.e. the stem to which suffixes get appended) and reports
/// the letter via `letter` (0 when absent). Returns empty when `name` is not of that form, and
/// always returns empty for an empty `slotPrefix` (presets - see the header).
std::string slotStem(std::string_view name, std::string_view slotPrefix, char* letter) {
	*letter = 0;
	if (slotPrefix.empty() || name.size() <= slotPrefix.size()) {
		return {};
	}
	for (size_t i = 0; i < slotPrefix.size(); i++) {
		if (std::toupper(static_cast<unsigned char>(name[i])) != std::toupper(static_cast<unsigned char>(slotPrefix[i]))) {
			return {};
		}
	}

	size_t pos = slotPrefix.size();
	size_t digitsStart = pos;
	while (pos < name.size() && pos - digitsStart < kMaxSlotDigits
	       && std::isdigit(static_cast<unsigned char>(name[pos]))) {
		pos++;
	}
	if (pos == digitsStart) {
		return {}; // No digits: just a name that happens to start with "SONG".
	}

	std::string stem{name.substr(0, pos)};

	if (pos == name.size()) {
		return stem; // "SONG185"
	}
	if (pos + 1 == name.size() && std::isalpha(static_cast<unsigned char>(name[pos]))) {
		*letter = static_cast<char>(std::toupper(static_cast<unsigned char>(name[pos])));
		return stem; // "SONG185A"
	}
	return {}; // Trailing junk: not a slot-form name.
}

/// Splits a numeric-suffixed name: "MYTRACK 3" -> base "MYTRACK", delimiter ' '.
/// Leaves `delimiter` as kNumericSuffixDelimiter and returns the whole name when there is no
/// existing suffix to reuse.
std::string numericStem(std::string_view name, char* delimiter) {
	*delimiter = kNumericSuffixDelimiter;

	for (char candidate : {'_', ' '}) {
		size_t at = name.rfind(candidate);
		if (at == std::string_view::npos || at + 1 >= name.size()) {
			continue;
		}
		bool allDigits = true;
		for (size_t i = at + 1; i < name.size(); i++) {
			if (!std::isdigit(static_cast<unsigned char>(name[i]))) {
				allDigits = false;
				break;
			}
		}
		if (allDigits) {
			*delimiter = candidate;
			return std::string{name.substr(0, at)};
		}
	}

	return std::string{name};
}

} // namespace

std::string nextDefaultName(std::string_view currentName, std::string_view slotPrefix, FileListView const& files) {
	char letter = 0;
	std::string stem = slotStem(currentName, slotPrefix, &letter);

	// Slot-form name ("SONG185" / "SONG185A"): advance the letter suffix.
	if (!stem.empty()) {
		char from = (letter == 0) ? 'A' : static_cast<char>(letter + 1);
		for (char c = from; c <= 'Z'; c++) {
			std::string candidate = stem + c;
			if (!exists(files, candidate)) {
				return candidate;
			}
		}
		return std::string{currentName}; // Letters exhausted.
	}

	// Anything else: advance the numeric suffix.
	char delimiter = kNumericSuffixDelimiter;
	std::string base = numericStem(currentName, &delimiter);
	for (int32_t n = 2; n <= kMaxNumericSuffix; n++) {
		std::string candidate = base + delimiter + std::to_string(n);
		if (!exists(files, candidate)) {
			return candidate;
		}
	}
	return std::string{currentName};
}

} // namespace deluge::gui::browser
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build --target UnitTests && ./build/tests/unit/UnitTests -g DefaultNameTests -v
```

Expected: all PASS, including `issue1069_neverEmitsTheSlotNumberAsASuffix`.

- [ ] **Step 6: Commit**

```bash
git add src/deluge/gui/ui/browser/default_name.h src/deluge/gui/ui/browser/default_name.cpp \
        tests/unit/default_name_tests.cpp tests/unit/CMakeLists.txt
git commit -m "feat: pure default-name derivation with #1069 regression tests"
```

---

## Task 3: Make rendering and navigation tolerant of a prefixed name

Prepare the two places that consume `enteredText` for the invariant flip in Task 4, so that
Task 4 does not have to change behaviour in three places at once. After this task both
displays still behave exactly as before — the code merely stops *assuming* the prefix has
been stripped.

Note what this fixes as a side effect: `selectEncoderAction`'s two branches are currently
written for the opposite display's convention. The 7-seg branch (`browser.cpp:1054-1060`)
requires `enteredText` to start with `filePrefix`, which is impossible on 7-seg today; the
OLED branch (`browser.cpp:1003-1011`) calls `getSlot(enteredText)` on `"SONG185"`, whose
first char is `S`. **Shift+select slot jumping and horizontal-encoder digit-scrub are dead on
both displays right now.** Collapsing them into one correct path revives both.

**Files:**
- Modify: `src/deluge/gui/ui/browser/browser.cpp:1497-1545` (`displayText`)
- Modify: `src/deluge/gui/ui/browser/browser.cpp:1000-1100` (`selectEncoderAction`)

**Interfaces:**
- Consumes: `Browser::getSlot()`, `Browser::filePrefix`.
- Produces: a private helper `char const* Browser::nameAfterPrefix(char const* name)` — returns
  the position in `name` just past `filePrefix`, or `nullptr` if `name` does not carry the prefix.
  Used by Task 4's rendering and by `selectEncoderAction`.

- [ ] **Step 1: Add the helper**

Declare in `src/deluge/gui/ui/browser/browser.h`, in the `protected:` section next to `getSlot`:

```cpp
	/// Returns the character just past filePrefix within `name`, or nullptr if `name` does not
	/// start with filePrefix. Names always carry the prefix; only *rendering* strips it.
	static char const* nameAfterPrefix(char const* name);
```

Define in `browser.cpp` next to `Browser::getSlot`:

```cpp
char const* Browser::nameAfterPrefix(char const* name) {
	if (!filePrefix) {
		return nullptr;
	}
	int32_t prefixLength = strlen(filePrefix);
	if (memcasecmp(name, filePrefix, prefixLength)) {
		return nullptr;
	}
	return &name[prefixLength];
}
```

`filePrefix` is a non-static member; make `nameAfterPrefix` a non-static member too if the
compiler objects (drop `static` from both the declaration and definition).

- [ ] **Step 2: Make displayText render a prefixed name in short form**

In `browser.cpp:1517-1524`, replace:

```cpp
				if (filePrefix) {

					Slot thisSlot = getSlot(enteredText.get());
					if (thisSlot.slot >= 0) {
```

with:

```cpp
				// A name is always the full on-card name ("SONG185"). On 7SEG we render the
				// numeric part alone ("185") so it fits the four-character display.
				char const* numberPart = nameAfterPrefix(enteredText.get());
				if (numberPart) {

					Slot thisSlot = getSlot(numberPart);
					if (thisSlot.slot >= 0) {
```

Leave the `setTextAsSlot(...)` call and the scrolling-text fallback below it unchanged.

- [ ] **Step 3: Collapse the two inverted selectEncoderAction branches into one**

In `browser.cpp`, replace the whole `if (display->haveOLED()) { ... } else { ... }` block
spanning roughly lines 1003-1100 (the block that begins with the comment
`// If user is holding shift, skip past any subslots...`) with the single display-agnostic
path below. Keep the surrounding `else { ... }` and the `nonNumeric:` label intact.

```cpp
		// If user is holding shift, skip past any subslots. And the user may have chosen one
		// digit to "edit" (7SEG only - numberEditPos is -1 otherwise).
		int32_t numberEditPosNow = numberEditPos;
		if (Buttons::isShiftButtonPressed() && numberEditPosNow == -1) {
			numberEditPosNow = 0;
		}

		if (numberEditPosNow != -1) {
			char const* numberPart = nameAfterPrefix(enteredText.get());
			if (!numberPart) {
				goto nonNumeric;
			}
			Slot thisSlot = getSlot(numberPart);
			if (thisSlot.slot < 0) {
				goto nonNumeric;
			}
			thisSlot.subSlot = -1;

			switch (numberEditPosNow) {
			case 0:
				thisSlot.slot += offset;
				break;
			case 1:
				thisSlot.slot = (thisSlot.slot / 10 + offset) * 10;
				break;
			case 2:
				thisSlot.slot = (thisSlot.slot / 100 + offset) * 100;
				break;
			default:
				__builtin_unreachable();
			}

			int32_t filePrefixLength = strlen(filePrefix);
			char searchString[16];
			memcpy(searchString, filePrefix, filePrefixLength);
			char* searchStringNumbersStart = searchString + filePrefixLength;
			intToString(thisSlot.slot, searchStringNumbersStart, 1);
			if (offset < 0) {
				char* pos = strchr(searchStringNumbersStart, 0);
				*pos = 'A';
				pos++;
				*pos = 0;
			}
			newFileIndex = fileItems.search(searchString);
			if (offset < 0) {
				newFileIndex--;
			}
		}
		else {
			newFileIndex = fileIndexSelected + offset;
		}
```

- [ ] **Step 4: Build the firmware**

```bash
./dbt build Debug
```

Expected: builds clean.

- [ ] **Step 5: Commit**

```bash
git add src/deluge/gui/ui/browser/browser.cpp src/deluge/gui/ui/browser/browser.h
git commit -m "refactor: make browser rendering and navigation prefix-tolerant (#1069)"
```

---

## Task 4: Flip the invariant

The behavioural change. `enteredText` and `displayName` now always hold the real on-card
name on both displays, and the name derivation moves to the Task 2 helper. This task is
inherently atomic — the pieces below cannot land separately without leaving the tree broken.

**Files:**
- Modify: `src/deluge/gui/ui/browser/browser.cpp:343-377` (remove the 7-seg `displayName` strip)
- Modify: `src/deluge/gui/ui/browser/browser.cpp:686-836` (replace inline derivation with the helper)
- Modify: `src/deluge/gui/ui/browser/browser.cpp:928-971` (delete `getUnusedSlot()`'s 7-seg branch)
- Modify: `src/deluge/gui/ui/browser/slot_browser.cpp:173-214` (delete `getCurrentFilenameWithoutExtension`; rework `getCurrentFilePath`)
- Modify: `src/deluge/gui/ui/browser/slot_browser.h`

**Interfaces:**
- Consumes: `nextDefaultName()` and `FileListView` (Task 2); `nameAfterPrefix()` (Task 3).
- Produces: the invariant. Every later task depends on `enteredText` being a real name.

- [ ] **Step 1: Remove the 7-seg displayName prefix-strip**

In `browser.cpp`, replace the whole `if (display->have7SEG()) { ... } else { nonNumericFile: ... }`
block at lines 343-381 with:

```cpp
		// displayName is the CStringArray sort key and must equal the real on-card name.
		// The 7SEG short form ("185") is produced at render time, not stored here.
		thisItem->displayName = storedFilenameChars;
```

Delete the now-unused `nonNumericFile:` label and any `goto nonNumericFile;` that targeted it
within this function. The `filePrefixHere` / `filePrefixLength` locals may become unused — remove
them if the compiler warns.

- [ ] **Step 2: Replace the inline name derivation with the helper**

In `Browser::arrivedInNewFolder`, replace everything from line 689 (`// `#if 1 || !OLED` macro was here`)
through the end of the `doNormal:` block at line 835 — that is, the entire subslot block, the
commented-out dead `#else` branch, and all of `doNormal:` — with:

```cpp
		{
			BrowserFileListView view;
			// Only songs earn letter suffixes; presets pass an empty slotPrefix and take the
			// numeric path, preserving current OLED preset behaviour. See Global Constraints.
			char const* slotPrefix = (filePrefix && !memcasecmp(filePrefix, "SONG", 4)) ? filePrefix : "";
			std::string newName = deluge::gui::browser::nextDefaultName(enteredText.get(), slotPrefix, view);
			if (newName == enteredText.get()) {
				goto useFoundFile; // No free variation available; stay on the file we found.
			}
			error = enteredText.set(newName.c_str());
			if (error != Error::NONE) {
				goto gotErrorAfterAllocating;
			}
			enteredTextEditPos = enteredText.getLength();
		}
```

Add near the top of `browser.cpp`:

```cpp
#include "gui/ui/browser/default_name.h"
```

and, above `Browser::arrivedInNewFolder`, the adapter:

```cpp
namespace {
/// Adapts Browser::fileItems to the FileListView seam used by nextDefaultName().
class BrowserFileListView final : public deluge::gui::browser::FileListView {
public:
	bool contains(char const* nameWithExtension) const override {
		bool foundExact = false;
		Browser::fileItems.search(nameWithExtension, &foundExact);
		return foundExact;
	}
};
} // namespace
```

`fileItems` is a public static on `Browser`, so the adapter needs no friendship.

- [ ] **Step 3: Delete getUnusedSlot()'s 7-seg branch**

In `Browser::getUnusedSlot`, the function currently reads
`if (display->haveOLED()) { ...A... } else { ...B... }` twice — once when choosing
`filenameToStartAt` (lines 880-891) and once when deriving the name (lines 900-971).

For the first: always use the OLED form, since names now always carry the prefix:

```cpp
	char filenameToStartAt[6]; // thingName is max 4 chars.
	strcpy(filenameToStartAt, thingName);
	strcat(filenameToStartAt, ":");
	error = readFileItemsFromFolderAndMemory(currentSong, outputType, getThingName(outputType), filenameToStartAt, NULL,
	                                         false, Availability::ANY, CATALOG_SEARCH_LEFT);
```

For the second: keep the existing OLED body (lines 900-927) unconditionally and **delete the
entire 7-seg slot-scanning branch at lines 928-971**, including the `goBackOne:` and
`noMoreToLookAt:` labels and the `nextHigherSlotFound` local. Replace
`fileItem->getDisplayNameWithoutExtension(&displayName)` with
`fileItem->getFilenameWithoutExtension(&displayName)`.

- [ ] **Step 4: Delete the re-prefixing hack in SlotBrowser**

In `slot_browser.cpp`, delete `SlotBrowser::getCurrentFilenameWithoutExtension()` entirely
(lines 173-190) and its declaration in `slot_browser.h`. Then rewrite `getCurrentFilePath` so
it uses `enteredText` — which now *is* the filename:

```cpp
Error SlotBrowser::getCurrentFilePath(String* path) {
	path->set(&currentDir);

	Error error = path->concatenate("/");
	if (error != Error::NONE) {
		return error;
	}

	error = path->concatenate(&enteredText);
	if (error != Error::NONE) {
		return error;
	}

	if (writeJsonFlag) {
		error = path->concatenate(".Json");
	}
	else {
		error = path->concatenate(".XML");
	}

	return error;
}
```

If other callers of `getCurrentFilenameWithoutExtension()` exist (check with
`grep -rn getCurrentFilenameWithoutExtension src/deluge` — `save_song_ui.cpp:164` is one),
point them at `enteredText.get()` instead.

- [ ] **Step 5: Build the firmware**

```bash
./dbt build Debug
```

Expected: builds clean. Fix any fallout from removed labels/locals.

- [ ] **Step 6: Run the unit tests**

```bash
cmake --build build --target UnitTests && ./build/tests/unit/UnitTests -v
```

Expected: all PASS.

- [ ] **Step 7: Commit**

```bash
git add src/deluge/gui/ui/browser/browser.cpp src/deluge/gui/ui/browser/slot_browser.cpp \
        src/deluge/gui/ui/browser/slot_browser.h src/deluge/gui/ui/save/save_song_ui.cpp
git commit -m "fix: make song naming display-agnostic, restoring letter suffixes on 7SEG (#1069)"
```

---

## Task 5: Prefix-aware typing

With names now carrying the prefix, typing a digit would search for `"1"` and match nothing,
where before it matched the stripped `"185.XML"`. Restore it by treating the prefix as
*implicitly typed* when the user's first typed character is a digit.

This costs nothing: a song name beginning with a digit is *already* unreachable today. Typing
`1` and saving runs through `getSlot("1")` → slot 1 → writes `SONG001.XML`; even `1st idea` is
mangled (`getSlot` reads digit `1`, then takes `s` as a subslot letter → `SONG001S.XML`).

**Files:**
- Modify: `src/deluge/gui/ui/browser/browser.cpp:1216-1250` (`predictExtendedText`)

**Interfaces:**
- Consumes: `Browser::filePrefix`, `enteredText`, `enteredTextEditPos`.
- Produces: nothing new.

- [ ] **Step 1: Seed the prefix when typing starts with a digit**

The typing invariant is that `enteredText[0..enteredTextEditPos)` is exactly what the user
typed (`QwertyUI` calls `enteredText.concatenateAtPos(c, enteredTextEditPos)`, which
truncates). So the fix must put the prefix *into* `enteredText` and count it in
`enteredTextEditPos`, not merely rewrite the search key.

At the top of `Browser::predictExtendedText()`, immediately after
`arrivedAtFileByTyping = true;`, insert:

```cpp
	// Names always carry the file prefix, but on 7SEG the user only ever sees and types the
	// number ("185"). When typing begins with a digit, treat the prefix as implicitly typed.
	if (filePrefix && enteredTextEditPos > 0) {
		char const* typed = enteredText.get();
		if (typed[0] >= '0' && typed[0] <= '9') {
			int32_t prefixLength = strlen(filePrefix);
			String prefixed;
			Error prefixError = prefixed.set(filePrefix);
			if (prefixError == Error::NONE) {
				prefixError = prefixed.concatenate(&enteredText);
			}
			if (prefixError != Error::NONE) {
				display->displayError(prefixError);
				return false;
			}
			enteredText.set(&prefixed);
			enteredTextEditPos += prefixLength;
		}
	}
```

Everything downstream (the `searchString` shorten, the `memcasecmp` verification against
`fileItem->displayName`, and `setEnteredTextFromCurrentFilename()`) then works unchanged,
because `enteredText` is once again a real name.

- [ ] **Step 2: Build the firmware**

```bash
./dbt build Debug
```

Expected: builds clean.

- [ ] **Step 3: Commit**

```bash
git add src/deluge/gui/ui/browser/browser.cpp
git commit -m "fix: keep digit typing working in the browser now that names carry the prefix (#1069)"
```

---

## Task 6: Delete the dead slot model

Nothing reads these any more. Removing them is what makes the slot model actually *gone*
rather than merely bypassed.

**Files:**
- Modify: `src/deluge/storage/file_item.h:30`, `src/deluge/storage/file_item.cpp:66-88`
- Modify: `src/deluge/model/song/song.h:228-229`, `src/deluge/model/song/song.cpp:139-140`
- Modify: `src/deluge/gui/ui/browser/slot_browser.cpp:123-171`, `src/deluge/gui/ui/browser/slot_browser.h`

- [ ] **Step 1: Delete FileItem::getDisplayNameWithoutExtension**

It is now identical to `getFilenameWithoutExtension()` (since `displayName == filename`).
Remove the declaration (`file_item.h:30`) and definition (`file_item.cpp:66-88`). Repoint any
remaining callers:

```bash
grep -rn getDisplayNameWithoutExtension src/deluge
```

Expected after repointing: no hits.

- [ ] **Step 2: Delete Song::slot and Song::subSlot**

Remove the fields at `song.h:228-229` and their assignments at `song.cpp:139-140`. They are
written in the constructor and never read. Confirm:

```bash
grep -rn "currentSong->slot\|currentSong->subSlot" src/deluge
```

Expected: no hits. (Note `doesNonAudioSlotHaveActiveClipInSession(outputType, slot, subSlot)`
in `song.cpp` is about MIDI/CV *channels*, not file slots — leave it alone.)

- [ ] **Step 3: Delete convertToPrefixFormatIfPossible**

Remove `SlotBrowser::convertToPrefixFormatIfPossible()` (`slot_browser.cpp:123-171`) and its
declaration. It computes a slot and subslot and then simply clears `enteredText` — dead
behaviour under the new invariant. Remove its two call sites:
`SlotBrowser::enterKeyPress()` (`slot_browser.cpp:112-114`) and `SaveUI::timerCallback()`
(`save_ui.cpp:119`).

`SlotBrowser::enterKeyPress()` becomes empty. If nothing else overrides it, delete the
override entirely and let `Browser`'s run.

- [ ] **Step 4: Build the firmware and run the tests**

```bash
./dbt build Debug && cmake --build build --target UnitTests && ./build/tests/unit/UnitTests -v
```

Expected: builds clean, all tests PASS.

- [ ] **Step 5: Commit**

```bash
git add -A src/deluge
git commit -m "refactor: delete the now-unused slot model (#1069)"
```

---

## Task 7: Verify on hardware

The unit tests cover name *derivation*. They cannot cover rendering, the card, or the
sort-order risk. This task is manual and must not be skipped — the whole bug lived in the gap
between what the naming logic computed and what the display and card actually did.

**Files:** none.

- [ ] **Step 1: Confirm the reported bug is gone (7-seg)**

On 7-seg hardware, with a card holding `SONG001.XML`…`SONG185.XML` and the song `SONG185`
loaded: press SAVE. The display must read `185A`, and saving must produce `SONG185A.XML`.
Press SAVE again → `185B` → `SONG185B.XML`.

**Fail condition:** any underscore, any name longer than four characters, any appearance of
`186`.

- [ ] **Step 2: Confirm OLED is unchanged**

Same card on OLED: SAVE on `SONG185` must still offer `SONG185A`, then `SONG185B`. This is
current behaviour and must not regress.

- [ ] **Step 3: Confirm card compatibility both ways**

Load a vanilla-written card and check the song list renders correctly on 7-seg (numbers, not
`SONG001`). Then load a card written by this firmware in **vanilla** firmware and confirm the
songs appear and load.

- [ ] **Step 4: Check the sort-order risk flagged in the spec**

The 7-seg file list is now keyed on `SONG185.XML` rather than `185.XML`. Task 1's
`prefixIsOrderPreserving` test shows song files keep their relative order, but **folders and
non-numeric names intermix differently**. On 7-seg, open a SONGS folder containing a mix of
`SONGnnn.XML` files, arbitrarily-named songs (`MYTRACK.XML`), and subfolders. Scroll the whole
list with the select encoder and confirm the ordering is sane and nothing is unreachable.

- [ ] **Step 5: Check the revived navigation**

On 7-seg: hold SHIFT and turn the select encoder — it should jump between slot numbers,
skipping letter suffixes. Turn the horizontal encoder to pick a digit, then the select encoder
— it should step that digit by 1/10/100. Both of these are dead in the current firmware; they
should now work. Confirm they also work on OLED with SHIFT.

- [ ] **Step 6: Check typing**

On 7-seg, in the save browser, type `1` `8` `5` on the pads. The display should track
`1` → `18` → `185` and land on song 185.

- [ ] **Step 7: Check a renamed song**

Rename a song to `MYTRACK` and save twice. Expect `MYTRACK 2`, then `MYTRACK 3`, on both
displays. (Note the delimiter decision at the top of this plan: 7-seg now uses a space where it
previously used an underscore.)

- [ ] **Step 8: Update the issue**

Comment on [#1069](https://github.com/SynthstromAudible/DelugeFirmware/issues/1069) with the
root cause (the 7-seg `displayName` prefix-strip made `enteredText` miss the `SONG` guard at
`browser.cpp:691` and then miscount in `doNormal`, so `_186` was the slot number leaking
through a wrong string offset) and the fix. Note explicitly that renamed songs still get a
numeric suffix and so still scroll on 7-seg — only default-named songs regain the glanceable
four-character workflow.
