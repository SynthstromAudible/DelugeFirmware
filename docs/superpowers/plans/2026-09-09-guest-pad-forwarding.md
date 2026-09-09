# Guest Pad Forwarding Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the ported menu legacy's contract for a grid pad pressed while it is open — forward the pads it does not claim to the legacy root view, and close on an unclaimed pad.

**Architecture:** One new `Scene` virtual, `grid_pad_claim()`, declares how much of the grid a scene consumes. `ShellHostUI::padAction` reads it and either feeds the shell (as today) or hands the pad to a new `gui/ui/` translation unit that ports `SoundEditor::padAction`'s root-identity switch and its `exitCompletely()` tail. The switch's decision is separated from its performance by a settable dispatch, so a spec can watch the routing without executing legacy view code.

**Tech Stack:** C++20, CMake, CppSpec (`deluge_scenes_spec`, `deluge_spec`), the level-3 A/B harness (`tests/ui-scripts/app/`), Python checkers under `scripts/`.

**Spec:** `docs/superpowers/specs/2026-09-09-guest-pad-forwarding-design.md`

**Branch:** `ui/pads-1`, cut from `ui/wiring-3` at `23aae2ef2`. **Branch in place — no worktrees.**

## Global Constraints

- **Never edit `src/bsp/rza1`.** Retired dead source.
- **Never commit `src/bsp/rust/Cargo.lock`.**
- **`git add` named paths only.** Never `git add -A` / `git add .` — an untracked `release-blockers.md` sits in the working tree and a previous plan had to be rewritten to get it back out.
- **snake_case** for anything under `src/shell` and `src/scenes`; **camelCase** for anything under `src/deluge` (the façade over legacy).
- **No new UI/UX.** Every behaviour in this plan is a port of a named legacy statement. A comment saying "legacy has none" or "spec design, not a port" is a defect to report, not a decision to make.
- **Legacy bug fixes are Kate's call.** Do not change `src/deluge/gui/ui/sound_editor.cpp`'s own behaviour. Porting a legacy predicate into new port-side code, with a guard legacy lacks, is not a legacy fix — writing the guard into legacy's copy would be.
- **`./dbt build Debug` is never evidence.** The device link fails on a known pre-existing `ld.lld: … Invalid record` from a Rust/LLVM skew. Do not run it, do not cite it.
- **Verification gates, run for every task:**
  ```bash
  cmake --build build-sim --target deluge_host deluge_scenes_spec -j8 && ctest --test-dir build-sim
  ctest --test-dir build/tests -C Debug
  python3 scripts/check_layer_rules.py && python3 scripts/check_layer_rules.py --audit-symbols
  python3 scripts/check_platform_boundary.py && python3 scripts/check_gfx_layer.py
  python3 scripts/check_citations.py --strict && python3 scripts/check_citation_shift.py
  bash scripts/menu_dump_check.sh
  ```
  The bare `cmake --build build-sim` (no `--target`) fails on the pre-existing dead `deluge_host_boundary` target; always name the targets.
- **Every citation you write must be opened and read.** `check_citations.py` does not scan `docs/`, and a line number that resolves is not a line number that says what you claimed. This stack has recorded seven distinct citation failure modes; the cheapest defence is `sed -n '<n>p' <file>` on every one you write.

## Ruling carried into this plan: the two note-editor arms are not written

Spec §2's behaviour 2 lists six forwarding paths. **Four are implemented here; the two note-editor arms (`sound_editor.cpp:1484`, `:1493`) are not.** They are keyed on `SoundEditor::inNoteEditor()` / `inNoteRowEditor()`, which read `menuItemNavigationRecord[0]` — a zero-initialised member no guest path ever writes (spec §3.4). Writing those arms would mean reading exactly the stale legacy state §3.4 forbids, to reach a menu that does not exist: `MenuContext::Root` has no note-editor value, and sites 12 and 13 are §10.2 item 2's remaining work.

The honest port is that note-editor behaviour is keyed on the **claim value**, not on the root: when a note-editor `MenuScene` exists it will answer `GridPadClaim::None` and the switch will need a main-grid/sidebar split for it. Task 6 records that in the tree. Do not write the arms; do not write a `TODO` in their place.

---

### Task 1: `GridPadClaim` — the declaration

**Files:**
- Modify: `src/shell/scene.h` (composition group; `input_transparent()` ends at `:423`)
- Modify: `src/scenes/menu/menu_scene.h` (the override block at `:473-482`)
- Test: `sim/scenes_spec/menu_grid_claim_spec.cpp` (create)

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `enum class deluge::ui::shell::GridPadClaim : uint8_t { All, ShortcutsOnly, None };`
  - `virtual GridPadClaim deluge::ui::shell::Scene::grid_pad_claim() const;` — default `All`.
  - `deluge::scenes::menu::MenuScene::grid_pad_claim()` — `None` on `MenuContext::Root::Settings`, `ShortcutsOnly` otherwise.

- [ ] **Step 1: Write the failing spec**

Create `sim/scenes_spec/menu_grid_claim_spec.cpp`. Follow the file-header style of the specs beside it (`sim/scenes_spec/browser_choice_spec.cpp` is the closest model): a `/// @file` block saying what the file pins and what it deliberately cannot see.

```cpp
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/registry.h"
#include "shell/scene.h"
#include "support/song_fixture.h"
#include <spec_helper.h>

using deluge::ui::shell::GridPadClaim;

describe menu_grid_claim("MenuScene::grid_pad_claim -- SoundEditor::padAction's gate at "
                         "`gui/ui/sound_editor.cpp:1467`, declared", $ {
	it("answers ShortcutsOnly on a sound root", _ {
		// `:1467`'s three predicates are all false on a sound root, so `:1468`'s
		// `potentialShortcutPadAction` runs and claims -- see the spec's §3.1.
		scenes_spec::SongFixture fx;
		auto& scene = deluge::scenes::menu_scene();
		deluge::scenes::menu::MenuContext ctx = fx.sound_context();
		scene.open(deluge::scenes::menu::nodes::sound_root(), ctx);
		expect(scene.grid_pad_claim()).to_equal(GridPadClaim::ShortcutsOnly);
	});

	it("answers None on a settings root", _ {
		// `SoundEditor::inSettingsMenu()` (`:1912-1914`) is one of `:1467`'s three predicates, so
		// legacy skips the shortcut arm entirely here and every pad falls through.
		scenes_spec::SongFixture fx;
		auto& scene = deluge::scenes::menu_scene();
		deluge::scenes::menu::MenuContext ctx = fx.sound_context();
		ctx.root = deluge::scenes::menu::MenuContext::Root::Settings;
		scene.open(deluge::scenes::menu::nodes::settings_root(), ctx);
		expect(scene.grid_pad_claim()).to_equal(GridPadClaim::None);
	});

	it("answers All for a scene that does not override it", _ {
		// The default is the safe direction (spec §3): a scene that forgets to answer forwards
		// nothing, rather than forwarding something it wanted.
		expect(deluge::scenes::browser::browser_scene().grid_pad_claim())
		    .to_equal(GridPadClaim::All);
	});
});
```

**Adapt the fixture calls to what `sim/scenes_spec/support/song_fixture.h` actually offers** — read it first, and copy the setup idiom from `menu_actions_spec.cpp`, which already opens a real `MenuScene` on a real root. The three assertions above are the contract; how you reach an opened scene is the neighbouring specs' business, not this plan's invention.

- [ ] **Step 2: Run it and watch it fail**

```bash
cmake --build build-sim --target deluge_scenes_spec -j8
```
Expected: compile error, `'grid_pad_claim' is not a member of 'deluge::ui::shell::Scene'`.

- [ ] **Step 3: Declare the enum and the virtual**

In `src/shell/scene.h`, immediately after `input_transparent()` (ends `:423`) and before `greyout_mask()`, inside `namespace deluge::ui::shell`:

```cpp
	/// @brief How much of the grid this scene consumes while it is the input top.
	///
	/// The three values are legacy's three behaviours, not a design: `SoundEditor::padAction`
	/// (`gui/ui/sound_editor.cpp:1460`) tries its shortcut arm only when the menu is not a
	/// settings or note-editor menu (`:1467`), and forwards everything it did not claim to the
	/// root view by identity (`:1474-1517`).
	enum class GridPadClaim : uint8_t {
		/// The scene owns the grid; nothing is forwarded. Every root scene, every browser.
		All,
		/// The scene owns only the shortcut gesture -- a pressed main-grid pad with shift held.
		/// Everything else forwards. The port of `:1467` passing.
		ShortcutsOnly,
		/// The scene owns no grid pad. The port of `:1467` failing.
		None,
	};

	/// @brief Which grid pads this scene consumes.
	///
	/// The rest are forwarded to the legacy root view by `gui/ui/shell_pad_forward.cpp`, as
	/// `SoundEditor::padAction` forwards them. **`All` is the default on purpose**: every scene
	/// that existed before this virtual keeps exactly today's behaviour, and a scene that forgets
	/// to answer forwards nothing rather than forwarding something it meant to keep.
	///
	/// @note The exact predicate `ShortcutsOnly` stands for -- pressed, main grid, shift held --
	///       is applied by the reader, not here: it needs `kDisplayWidth` and the button state,
	///       neither of which this layer may read. See `ShellHostUI::claimsGridPad`.
	virtual GridPadClaim grid_pad_claim() const { return GridPadClaim::All; }
```

- [ ] **Step 4: Override it in `MenuScene`**

In `src/scenes/menu/menu_scene.h`, in the override block beside `dims_grid()` (`:477-479`):

```cpp
	/// @brief `SoundEditor::padAction`'s gate at `gui/ui/sound_editor.cpp:1467`, as a declaration.
	///
	/// `inSettingsMenu()` (`:1912-1914`) is one of that gate's three predicates, so on a settings
	/// root legacy never reaches `potentialShortcutPadAction` and every pad -- shift or not --
	/// falls through to the forwarding switch and the tail. The other two predicates name roots
	/// this port does not have yet (`MenuContext::Root` has no note-editor value; sites 12 and 13
	/// are migration §10.2 item 2's remaining work), so `None`'s second producer is future work.
	deluge::ui::shell::GridPadClaim grid_pad_claim() const override {
		return ctx_.root == MenuContext::Root::Settings ? deluge::ui::shell::GridPadClaim::None
		                                                : deluge::ui::shell::GridPadClaim::ShortcutsOnly;
	}
```

- [ ] **Step 5: Run the spec**

```bash
cmake --build build-sim --target deluge_host deluge_scenes_spec -j8 && ctest --test-dir build-sim
```
Expected: PASS, including the new `menu_grid_claim_spec` row (`create_specs_driver` registers it by stem automatically — no CMake change).

- [ ] **Step 6: Run every checker**

The full gate block from Global Constraints. All green.

- [ ] **Step 7: Commit**

```bash
git add src/shell/scene.h src/scenes/menu/menu_scene.h sim/scenes_spec/menu_grid_claim_spec.cpp
git commit -m "feat(scene): GridPadClaim -- SoundEditor::padAction's gate, declared"
```

---

### Task 2: the forwarding switch — four arms and the tail

**Files:**
- Create: `src/deluge/gui/ui/shell_pad_forward.h`, `src/deluge/gui/ui/shell_pad_forward.cpp`
- Modify: `scripts/check_layer_rules.py` (`LEGACY_PORT_SEAMS`, `:301-320`)
- Test: `sim/scenes_spec/pad_forward_spec.cpp` (create)

**Interfaces:**
- Consumes: nothing from Task 1 (this task decides; Task 4 asks).
- Produces:
  - `enum class deluge::gui::ui::PadForwardTarget : uint8_t { None, Keyboard, AuditionColumn, Performance, Exit };` — Task 3 adds `Automation`.
  - `using PadForwardPerform = void (*)(PadForwardTarget, int32_t, int32_t, int32_t);`
  - `PadForwardTarget deluge::gui::ui::forwardGridPad(int32_t x, int32_t y, int32_t velocity);`
  - `void deluge::gui::ui::setPadForwardPerform(PadForwardPerform perform);`

- [ ] **Step 1: Write the failing spec**

Create `sim/scenes_spec/pad_forward_spec.cpp`. It installs a recording `perform` so the decision is assertable without running a legacy view handler, then drives `forwardGridPad` with the root view set. Read `sim/scenes_spec/browser_choice_spec.cpp:648-780` first — it is the same shape (`setSampleClaimDispatch` installed for exactly this reason) and shows how a spec in this binary sets legacy globals.

```cpp
describe pad_forward("gui::ui::forwardGridPad -- SoundEditor::padAction's root switch", $ {
	// The recording perform. `forwardGridPad` returns its decision too; the recorder exists so an
	// example can also assert the coordinates the target would have been handed.
	static PadForwardTarget seen;
	static int32_t seen_x, seen_y, seen_v;

	before_each(_ {
		seen = PadForwardTarget::None;
		deluge::gui::ui::setPadForwardPerform([](PadForwardTarget t, int32_t x, int32_t y, int32_t v) {
			seen = t; seen_x = x; seen_y = y; seen_v = v;
		});
	});
	after_each(_ { deluge::gui::ui::setPadForwardPerform(nullptr); });

	it("sends every pad to the keyboard screen when that is the root", _ {
		// `sound_editor.cpp:1474-1477`: the arm has no coordinate test at all.
		<open keyboardScreen as the root UI>
		expect(deluge::gui::ui::forwardGridPad(3, 4, 64)).to_equal(PadForwardTarget::Keyboard);
		expect(seen_x).to_equal(3);
	});

	it("sends the audition column, and only that column, to the clip view", _ {
		// `:1500-1502`: `x == kDisplayWidth + 1`, nothing else.
		<open instrumentClipView as the root UI, currentUIMode = UI_MODE_NONE>
		expect(deluge::gui::ui::forwardGridPad(kDisplayWidth + 1, 4, 64))
		    .to_equal(PadForwardTarget::AuditionColumn);
	});

	it("lets a main-grid pad on a clip root fall through to the tail", _ {
		// `:1500`'s `else if` not taken -> the comment `// fall through below` at `:1504` -> the
		// tail at `:1520-1545`. This is the shipped divergence of spec §2.1.
		<open instrumentClipView as the root UI, currentUIMode = UI_MODE_NONE>
		expect(deluge::gui::ui::forwardGridPad(3, 4, 64)).to_equal(PadForwardTarget::Exit);
	});

	it("sends everything to the performance view when that is the root", _ {
		// `:1515-1517`, which returns performanceView's own result and never reaches the tail.
		<open performanceView as the root UI>
		expect(deluge::gui::ui::forwardGridPad(3, 4, 64)).to_equal(PadForwardTarget::Performance);
	});

	it("does not exit on a release", _ {
		// `:1520`'s `&& on`. Load-bearing: a swallowed release leaves legacy's audition state
		// latched after a forwarded audition-column press.
		<open sessionView as the root UI, currentUIMode = UI_MODE_NONE>
		expect(deluge::gui::ui::forwardGridPad(3, 4, 0)).to_equal(PadForwardTarget::None);
	});

	it("does not exit while a UI mode is active", _ {
		// `:1520`'s `currentUIMode == UI_MODE_NONE`.
		<open sessionView as the root UI>
		currentUIMode = UI_MODE_AUDITIONING;
		expect(deluge::gui::ui::forwardGridPad(3, 4, 64)).to_equal(PadForwardTarget::None);
		currentUIMode = UI_MODE_NONE;
	});

	it("really removes the ported menu when the tail fires", _ {
		// The one example that runs the REAL perform: `closePortedMenu()` names no legacy view, so
		// it is safe here, and this is what makes behaviour 3 covered outside the A/B.
		deluge::gui::ui::setPadForwardPerform(nullptr);
		<open instrumentClipView as the root UI; push a real MenuScene on the guest stack>
		(void)deluge::gui::ui::forwardGridPad(3, 4, 64);
		expect(shellHostUI.shell().stack().depth()).to_equal(0u);
	});
});
```

Replace every `<...>` with the real setup. `openUI(&instrumentClipView)` and friends are how the root is set; `menu_actions_spec.cpp` and `browser_choice_spec.cpp` between them show the whole rig, including how a `MenuScene` gets onto `shellHostUI.shell().stack()`. **If a root view cannot be opened in this binary without booting the app, say so in your report and pin what you can** — an example that cannot run is worth more as a reported obstacle than as a deleted assertion.

- [ ] **Step 2: Run it and watch it fail**

```bash
cmake --build build-sim --target deluge_scenes_spec -j8
```
Expected: `fatal error: 'gui/ui/shell_pad_forward.h' file not found`.

- [ ] **Step 3: Write the header**

`src/deluge/gui/ui/shell_pad_forward.h`. Give it a `/// @file` block in the register of `shell_choice_dispatch.h`'s — what the seam is, which legacy function it ports, and what deliberately stays on the other side of the wall.

```cpp
#pragma once

#include <cstdint>

namespace deluge::gui::ui {

/// @brief Where a pad the ported menu did not claim was sent.
///
/// One value per arm of `SoundEditor::padAction`'s root switch (`gui/ui/sound_editor.cpp:1474-1517`)
/// plus its tail (`:1520-1545`). Returned so a caller -- and a spec -- can see the decision the
/// switch made without inspecting what it did.
enum class PadForwardTarget : uint8_t {
	/// No arm matched and the tail's conditions did not hold. Legacy returns `DEALT_WITH` here
	/// too (`:1548`): the pad is swallowed, which is what an open menu should do with it.
	None,
	/// `:1475` -- `keyboardScreen.padAction`, every coordinate.
	Keyboard,
	/// `:1500-1502` -- `instrumentClipView.padAction`, the audition column only.
	AuditionColumn,
	/// `:1516` -- `performanceView.padAction`, every coordinate, never reaching the tail.
	Performance,
	/// `:1545` -- `exitCompletely()`, ported as `closePortedMenu()`.
	Exit,
};

/// @brief What performs a decided forward. See `setPadForwardPerform`.
using PadForwardPerform = void (*)(PadForwardTarget target, int32_t x, int32_t y, int32_t velocity);

/// @brief Route one grid pad the ported menu did not claim, exactly as `SoundEditor::padAction` does.
/// @param x,y The pad, in `MatrixDriver` coordinates.
/// @param velocity Legacy's `on`: non-zero is a press.
/// @return Which arm matched -- already performed when this returns.
PadForwardTarget forwardGridPad(int32_t x, int32_t y, int32_t velocity);

/// @brief Replace what performs a decided forward; `nullptr` restores the real one.
///
/// F3's `setSampleClaimDispatch` (`gui/ui/shell_choice_dispatch.h:206`) for the same reason it
/// exists there: the decision is the part worth pinning, and performing it means calling into
/// `keyboardScreen` / `instrumentClipView` / `performanceView`, whose handlers need model state a
/// spec should not have to build. `deluge_scenes_spec` links `deluge_app`, so those symbols
/// resolve -- this is about what running them costs, not about what links.
void setPadForwardPerform(PadForwardPerform perform);

} // namespace deluge::gui::ui
```

- [ ] **Step 4: Write the implementation**

`src/deluge/gui/ui/shell_pad_forward.cpp`:

```cpp
#include "gui/ui/shell_pad_forward.h"

#include "definitions_cxx.hpp"
#include "gui/ui/shell_action_dispatch.h" // closePortedMenu()
#include "gui/ui/ui.h"                    // getRootUI(), currentUIMode
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_view.h"

namespace deluge::gui::ui {
namespace {

/// The real performer. Replaced by `setPadForwardPerform`; see its note.
void performForward(PadForwardTarget target, int32_t x, int32_t y, int32_t velocity) {
	switch (target) {
	case PadForwardTarget::Keyboard:
		keyboardScreen.padAction(x, y, velocity); // `sound_editor.cpp:1475`
		break;
	case PadForwardTarget::AuditionColumn:
		instrumentClipView.padAction(x, y, velocity); // `:1501`
		break;
	case PadForwardTarget::Performance:
		(void)performanceView.padAction(x, y, velocity); // `:1516`
		break;
	case PadForwardTarget::Exit:
		closePortedMenu(); // `:1545`'s `exitCompletely()`; see that function's docblock
		break;
	case PadForwardTarget::None:
		break;
	}
}

PadForwardPerform perform = &performForward;

} // namespace

PadForwardTarget forwardGridPad(int32_t x, int32_t y, int32_t velocity) {
	const UI* root = getRootUI();

	PadForwardTarget target = PadForwardTarget::None;
	if (root == &keyboardScreen) {
		target = PadForwardTarget::Keyboard; // `:1474-1477`
	}
	else if (root == &instrumentClipView && x == kDisplayWidth + 1) {
		// `:1499-1502`. The two note-editor arms above it (`:1484`, `:1493`) are deliberately
		// absent -- see the plan's ruling and `src/scenes/README.md`: they are keyed on
		// `inNoteEditor()`, which reads a `SoundEditor` member no guest path writes.
		target = PadForwardTarget::AuditionColumn;
	}
	else if (root == &performanceView) {
		target = PadForwardTarget::Performance; // `:1515-1517`
	}
	else if (currentUIMode == UI_MODE_NONE && velocity != 0) {
		// `:1520-1545`, minus the firmware-version diagnostics at `:1521-1543` -- Kate's deliberate
		// gap, recorded with its coordinates in `src/scenes/README.md`.
		target = PadForwardTarget::Exit;
	}

	perform(target, x, y, velocity);
	return target;
}

void setPadForwardPerform(PadForwardPerform p) {
	perform = (p != nullptr) ? p : &performForward;
}

} // namespace deluge::gui::ui
```

**Two things to get right, and both are cited above rather than invented:**
1. `getRootUI()`'s type — check `gui/ui/ui.h:145-150` and use whatever it actually returns (`RootUI*`), not `UI*`, if that is what it is. Compare pointers the way `sound_editor.cpp:1474` compares them.
2. `kDisplayWidth` lives in `src/definitions_cxx.hpp` (or is re-exported by it); confirm the include that `sound_editor.cpp` itself uses and use the same one.

The `.cpp` needs no CMake change if `src/deluge/**/*.cpp` is globbed; **check** `sim/CMakeLists.txt` / the app's source list and add the file explicitly if the build lists sources by name.

- [ ] **Step 5: Add the `LEGACY_PORT_SEAMS` entry**

In `scripts/check_layer_rules.py`, inside `LEGACY_PORT_SEAMS` (`:301-320`), after the `shell_choice_dispatch.cpp` entry, with an argued comment in the register of the ones already there:

```python
    # This plan: the pad half of the same seam. It names `keyboardScreen`, `instrumentClipView`
    # and `performanceView` -- the legacy views `SoundEditor::padAction` forwards to
    # (`gui/ui/sound_editor.cpp:1474-1517`) -- and, from Task 3, `deluge::scenes::menu_scene()`
    # for the automation predicate the port had to answer itself (spec §3.4). Its HEADER
    # (`shell_pad_forward.h`) is deliberately NOT listed: it names no `scenes/` or `shell/` type,
    # taking plain `int32_t` coordinates, so this seam is one translation unit wide -- exactly the
    # line `shell_choice_dispatch.h` draws above.
    "src/deluge/gui/ui/shell_pad_forward.cpp",
```

- [ ] **Step 6: Run the spec and the checkers**

```bash
cmake --build build-sim --target deluge_host deluge_scenes_spec -j8 && ctest --test-dir build-sim
python3 scripts/check_layer_rules.py && python3 scripts/check_layer_rules.py --audit-symbols
```
Expected: PASS. If the layer checker fails, the seam entry is wrong — do not widen the rule's pattern to make it pass.

- [ ] **Step 7: Run the whole gate block, then commit**

```bash
git add src/deluge/gui/ui/shell_pad_forward.h src/deluge/gui/ui/shell_pad_forward.cpp \
        scripts/check_layer_rules.py sim/scenes_spec/pad_forward_spec.cpp
git commit -m "feat(guest): forward unclaimed grid pads to the legacy root view"
```

---

### Task 3: the automation arm, with its question ported

**Files:**
- Modify: `src/deluge/gui/ui/shell_pad_forward.h` (one enum value), `src/deluge/gui/ui/shell_pad_forward.cpp`
- Test: `sim/scenes_spec/pad_forward_spec.cpp` (extend)

**Interfaces:**
- Consumes: Task 2's `PadForwardTarget`, `forwardGridPad`, `setPadForwardPerform`; Task 1's `MenuScene`.
- Produces: `PadForwardTarget::Automation`.

**Read spec §3.4 before starting.** The short version: `SoundEditor::handleAutomationViewPadAction` (`sound_editor.cpp:1551`) calls `isEditingAutomationViewParam()` (`:1564`), whose first statement dereferences `getCurrentMenuItem()` — `menuItemNavigationRecord[navigationDepth]` (`:1908-1910`), a zero-initialised member (`sound_editor.h:111`) that **no guest path writes**. Calling it from here is a null dereference on any boot where no legacy menu was ever opened. The question gets answered from the port's own menu instead.

- [ ] **Step 1: Write the failing examples**

Append to `sim/scenes_spec/pad_forward_spec.cpp`:

```cpp
	it("sends a sidebar pad to automation view regardless of the param", _ {
		// `:1553-1557`: `(x >= kDisplayWidth) || editingParamInAutomationView` -- the sidebar half
		// needs no param agreement at all.
		<open automationView as the root UI; open a MenuScene on a node with no param binding>
		expect(deluge::gui::ui::forwardGridPad(kDisplayWidth, 2, 64))
		    .to_equal(PadForwardTarget::Automation);
	});

	it("sends a main-grid pad to automation view when the menu shows the same param", _ {
		<open automationView as the root UI, onArrangerView = false>
		<open a MenuScene focused on a node whose bind.param matches clip->lastSelectedParam*>
		expect(deluge::gui::ui::forwardGridPad(3, 2, 64)).to_equal(PadForwardTarget::Automation);
	});

	it("falls through to the tail when the menu shows a different param", _ {
		// `handleAutomationViewPadAction` returns NOT_DEALT_WITH (`:1561`), and `:1508-1511` then
		// does not return -- so legacy reaches the tail.
		<open automationView as the root UI>
		<open a MenuScene focused on a node whose param does NOT match>
		expect(deluge::gui::ui::forwardGridPad(3, 2, 64)).to_equal(PadForwardTarget::Exit);
	});

	it("does not dereference a null clip", _ {
		// The guard migration §10.5 banked for legacy's copy (`:1582-1586` dereferences
		// `getCurrentClip()` unguarded). This is the port's own copy, so writing the guard here is
		// not the banked fix -- legacy's copy is untouched.
		<open automationView as the root UI with no current clip, onArrangerView = false>
		<open a MenuScene focused on a param node>
		expect(deluge::gui::ui::forwardGridPad(3, 2, 64)).to_equal(PadForwardTarget::Exit);
	});
```

- [ ] **Step 2: Run and watch them fail**

```bash
cmake --build build-sim --target deluge_scenes_spec -j8 && ctest --test-dir build-sim -R pad_forward
```
Expected: the four new examples fail (they get `Exit` or `None` where `Automation` is expected).

- [ ] **Step 3: Add the enum value**

In `shell_pad_forward.h`, after `Performance`:

```cpp
	/// `:1508` -- `SoundEditor::handleAutomationViewPadAction`'s work, done here: see
	/// `menuShowsAutomationParam` in the `.cpp` for why it is not a call into that function.
	Automation,
```

- [ ] **Step 4: Port the predicate and add the arm**

In `shell_pad_forward.cpp`, in the anonymous namespace:

```cpp
/// @brief `SoundEditor::isEditingAutomationViewParam` (`gui/ui/sound_editor.cpp:1564-1590`), asked
///        of the ported menu instead of of `SoundEditor`.
///
/// **Not a call into legacy's copy, and this is the whole reason this function exists.** That copy
/// opens with `getCurrentMenuItem()->getParamKind()`, and `getCurrentMenuItem()` is
/// `menuItemNavigationRecord[navigationDepth]` (`:1908-1910`) -- a zero-initialised member
/// (`gui/ui/sound_editor.h:111`) written only by `SoundEditor::setup`/`navigate`, neither of which
/// runs under the guest. `adoptResolvedContext` deliberately does not write it and says so
/// (`sound_editor.h:216`). So legacy's copy dereferences null here on any boot that never opened a
/// legacy menu. F3 task 4 met the same shape from the other side and supplied the state; the state
/// cannot be supplied here, because under the toggle the open menu genuinely is `MenuScene`.
///
/// The `clip != nullptr` guard is legacy's missing one (`:1582-1586`). Writing it in this copy is
/// not the fix migration §10.5 banked -- that fix edits `sound_editor.cpp`, and this does not.
bool menuShowsAutomationParam() {
	const auto* focused = deluge::scenes::menu_scene().focused();
	if (focused == nullptr) {
		return false;
	}
	// `:1576`'s test, in this layer's representation. Legacy tests two things -- `kind != NONE`
	// and `paramID != kNoParamID` -- but `kNoParamID` is `0xFFFFFFFF`
	// (`modulation/params/param.h:304`) and `ParamRef::index` is a `uint8_t` whose unset value is
	// 255 (`scenes/menu/node.h:132`), so that second comparison cannot be transcribed: it would be
	// false for every possible index. `ParamRef::valid()` (`:133`) is the whole of the test here --
	// an unbound `Binding` carries `Kind::NONE`.
	const deluge::scenes::menu::ParamRef& param = focused->bind.param;
	if (!param.valid()) {
		return false;
	}
	const auto kind = param.kind;
	const int32_t paramID = param.index;

	if (automationView.onArrangerView) {           // `:1578-1580`
		return currentSong != nullptr && kind == currentSong->lastSelectedParamKind
		       && paramID == currentSong->lastSelectedParamID;
	}
	const Clip* clip = getCurrentClip();           // `:1582`
	return clip != nullptr && kind == clip->lastSelectedParamKind
	       && paramID == clip->lastSelectedParamID; // `:1585-1586`
}
```

and in `forwardGridPad`, between the `instrumentClipView` arm and the `performanceView` arm — **legacy's order, `:1507-1517`**:

```cpp
	else if (root == &automationView) {
		// `:1507-1512`, whose body is `handleAutomationViewPadAction` (`:1551-1562`): the sidebar
		// half needs no param agreement, the main-grid half needs the menu to be showing the very
		// param automation view is editing. `NOT_DEALT_WITH` from that helper does not return in
		// legacy either -- it falls to the tail below.
		if (x >= kDisplayWidth || menuShowsAutomationParam()) {
			target = PadForwardTarget::Automation;
		}
	}
```

Note the shape: the `else if` chain must still fall to the tail when the automation arm declines, exactly as `:1508-1511` does. Getting that wrong turns a decline into a swallowed pad.

Add `performForward`'s arm:

```cpp
	case PadForwardTarget::Automation:
		automationView.padAction(x, y, velocity); // `:1558`
		break;
```

and the two includes (`gui/views/automation_view.h`, `scenes/menu/menu_scene.h`, `scenes/registry.h`).

- [ ] **Step 5: Run the spec**

```bash
cmake --build build-sim --target deluge_host deluge_scenes_spec -j8 && ctest --test-dir build-sim
```
Expected: PASS, all of `pad_forward_spec`.

- [ ] **Step 6: Whole gate block, then commit**

```bash
git add src/deluge/gui/ui/shell_pad_forward.h src/deluge/gui/ui/shell_pad_forward.cpp \
        sim/scenes_spec/pad_forward_spec.cpp
git commit -m "feat(guest): the automation arm, with its param question ported off SoundEditor"
```

---

### Task 4: `ShellHostUI::padAction` asks, then forwards or feeds

**Files:**
- Modify: `src/deluge/gui/ui/shell_host_ui.h` (one private helper), `src/deluge/gui/ui/shell_host_ui.cpp:747`
- Modify: `tests/spec/gui/support/fake_legacy_ui.h`, `tests/spec/gui/support/fake_legacy_ui.cpp`
- Test: `tests/spec/gui/shell_host_ui_spec.cpp` (extend)

**Interfaces:**
- Consumes: `Scene::grid_pad_claim()` and `GridPadClaim` (Task 1); `forwardGridPad` (Tasks 2-3).
- Produces: nothing later tasks call.

- [ ] **Step 1: Write the failing host-suite examples**

`deluge_spec` never builds the legacy view closure, so `forwardGridPad` is faked here exactly as `dispatchShellActions` and `dispatchShellChoices` are (`tests/spec/gui/support/fake_legacy_ui.cpp:313-327`). The questions that live on **this** side of the seam are: was the claim consulted, did a claimed pad reach the shell, did an unclaimed pad reach the forward instead, and does the tick still get armed.

In `tests/spec/gui/shell_host_ui_spec.cpp`, in the group that covers `padAction`:

```cpp
	it("feeds a pad the scene claims, and does not forward it", _ {
		// `GridPadClaim::All` is the default, so this is every scene that existed before this seam.
		rig.scene.claim = GridPadClaim::All;
		shellHostUI.padAction(3, 4, 64);
		expect(rig.scene.pads_seen).to_equal(1);
		expect(fakelegacyui::pad_forwards).to_equal(0);
	});

	it("forwards a pad the scene does not claim, and does not feed it", _ {
		rig.scene.claim = GridPadClaim::None;
		shellHostUI.padAction(3, 4, 64);
		expect(rig.scene.pads_seen).to_equal(0);
		expect(fakelegacyui::pad_forwards).to_equal(1);
		expect(fakelegacyui::last_pad_forward).to_equal(fakelegacyui::PadCall{3, 4, 64});
	});

	it("under ShortcutsOnly feeds a shift-held main-grid press and forwards everything else", _ {
		// Spec §3.1: `potentialShortcutPadAction`'s gate is `on && x < kDisplayWidth && shift`
		// (`gui/ui/sound_editor.cpp:1169-1186`), and past that gate it claims unconditionally
		// (`:1431`).
		rig.scene.claim = GridPadClaim::ShortcutsOnly;
		rig.hold_shift(true);
		shellHostUI.padAction(3, 4, 64);
		expect(rig.scene.pads_seen).to_equal(1);

		shellHostUI.padAction(kDisplayWidth + 1, 4, 64); // sidebar: never a shortcut
		shellHostUI.padAction(3, 4, 0);                  // release: never a shortcut
		rig.hold_shift(false);
		shellHostUI.padAction(3, 4, 64);                 // no shift
		expect(fakelegacyui::pad_forwards).to_equal(3);
	});

	it("asks the input top, not the top", _ {
		// A popup is `input_transparent()`, so it is not the scene whose grid this is. Asking
		// `top()` would let a popup's default `All` swallow a pad the menu beneath would forward.
		rig.scene.claim = GridPadClaim::None;
		rig.push_popup();
		shellHostUI.padAction(3, 4, 64);
		expect(fakelegacyui::pad_forwards).to_equal(1);
	});

	it("still arms the tick after a forward", _ {
		// Load-bearing, not housekeeping: the Exit arm empties the shell stack, and
		// `ShellHostUI::timerCallback` is what then closes the guest (`shell_host_ui.cpp:553-556`,
		// F1 task 11's fix). With no tick armed the guest would sit open over an empty stack.
		rig.scene.claim = GridPadClaim::None;
		shellHostUI.padAction(3, 4, 64);
		expect(rig.timer_armed()).to_be_true();
	});
```

Adapt `rig.*` to the fixture this file already uses — read the existing `padAction` examples and reuse their rig verbs rather than inventing new ones. Add `claim` to the spec's fake scene, defaulting to `All`.

- [ ] **Step 2: Add the fake**

In `tests/spec/gui/support/fake_legacy_ui.h`, beside `choice_dispatches` (`:142-150`), with a docblock in the same register — say why it is faked (the real one names `keyboardScreen`, `instrumentClipView`, `performanceView`, `automationView` and `deluge::scenes::menu_scene()`, none of which this suite builds) and what that leaves assertable here:

```cpp
/// @brief How many times `deluge::gui::ui::forwardGridPad()` was called.
extern int pad_forwards;
/// @brief The arguments of the last one.
struct PadCall { int32_t x, y, velocity; };
extern PadCall last_pad_forward;
```

and in `fake_legacy_ui.cpp`, beside `dispatchShellChoices` (`:325-327`):

```cpp
PadForwardTarget forwardGridPad(int32_t x, int32_t y, int32_t velocity) {
	fakelegacyui::pad_forwards++;
	fakelegacyui::last_pad_forward = fakelegacyui::PadCall{x, y, velocity};
	return PadForwardTarget::None;
}
```

Reset both in `reset()`, beside the existing counters.

- [ ] **Step 3: Run and watch them fail**

```bash
cmake --build build/tests --target <the spec target> -j8 && ctest --test-dir build/tests -C Debug -R shell_host_ui
```
Expected: the new examples fail — every pad is fed, `pad_forwards` stays 0.

- [ ] **Step 4: Add the claim helper**

In `src/deluge/gui/ui/shell_host_ui.h`, private:

```cpp
	/// @brief Does the scene that would receive this pad claim it? Spec §3.1's predicate.
	///
	/// `input_top()`, not `top()`: a passive overlay (popup, console, working animation) is
	/// `input_transparent()` and is not the scene whose grid this is (`shell/scene.h:423`). Asking
	/// `top()` would let such an overlay's default `All` swallow a pad the menu beneath it would
	/// have forwarded.
	bool claimsGridPad(int32_t x, int32_t y, int32_t velocity);
```

In the `.cpp`, beside `padAction`:

```cpp
bool ShellHostUI::claimsGridPad(int32_t x, int32_t y, int32_t velocity) {
	switch (shell_.stack().input_top().grid_pad_claim()) {
	case deluge::ui::shell::GridPadClaim::All:
		return true;
	case deluge::ui::shell::GridPadClaim::None:
		return false;
	case deluge::ui::shell::GridPadClaim::ShortcutsOnly:
		// `SoundEditor::potentialShortcutPadAction`'s gate (`gui/ui/sound_editor.cpp:1169-1186`),
		// minus its auditioning half: that is `isUIModeActive(UI_MODE_AUDITIONING)`, which
		// `MenuScene` already drops with the reason recorded (`scenes/menu/menu_scene.cpp:1627-1630`).
		// Past that gate legacy claims unconditionally, even where no shortcut table has an entry
		// (`:1431`) -- and so does `MenuScene`, which returns `Result::unhandled()` and lets the pad
		// die (`menu_scene.cpp:1664`). The two agree; see spec §3.1.
		return velocity != 0 && x < kDisplayWidth && Buttons::isShiftButtonPressed();
	}
	return true; // `-Werror=switch` makes this unreachable; it is here for the enum's future.
}
```

- [ ] **Step 5: Wire `padAction`**

`src/deluge/gui/ui/shell_host_ui.cpp:747`. Keep every existing comment — the range-guard docblock in particular is argued and cited. The body becomes:

```cpp
	if (x >= 0 && y >= 0 && x <= UINT8_MAX && y <= UINT8_MAX) {
		if (claimsGridPad(x, y, velocity)) {
			feed(DelugeInputEvent{DELUGE_EVENT_PAD, static_cast<uint8_t>(x), static_cast<uint8_t>(y),
			                      static_cast<int16_t>(velocity)});
		}
		else {
			// The pad is the legacy root view's, as it is in `SoundEditor::padAction`
			// (`gui/ui/sound_editor.cpp:1474-1545`).
			//
			// **The guest may not be current when this returns**, and nothing below may assume it
			// is: `performanceView.padAction` opens the legacy sound editor on a shift press
			// (`gui/views/performance_view.cpp:1038`) and the tail's `closePortedMenu()` empties
			// this guest's own stack. `armTickSoon` is safe on both counts -- `rearmTimer` refuses
			// when this guest is not the current UI (`:620`, F2's final fix) -- and it is *required*
			// on the second: the tick is what notices the empty stack and closes the guest
			// (`:553-556`).
			(void)forwardGridPad(x, y, velocity);
		}
	}
```

Add `#include "gui/ui/shell_pad_forward.h"`, `#include "hid/buttons.h"` and whatever gives `kDisplayWidth`, in the file's existing alphabetical include order.

- [ ] **Step 6: Run both suites**

```bash
cmake --build build-sim --target deluge_host deluge_scenes_spec -j8 && ctest --test-dir build-sim
ctest --test-dir build/tests -C Debug
```
Expected: PASS. **`ui_app_scripts_menu_ab` is expected to still pass here** — the existing script never presses a pad with a menu open, which is the whole of spec §7.2. Task 5 is what changes that.

- [ ] **Step 7: Whole gate block, then commit**

```bash
git add src/deluge/gui/ui/shell_host_ui.h src/deluge/gui/ui/shell_host_ui.cpp \
        tests/spec/gui/support/fake_legacy_ui.h tests/spec/gui/support/fake_legacy_ui.cpp \
        tests/spec/gui/shell_host_ui_spec.cpp
git commit -m "feat(guest): ask the scene before feeding a grid pad"
```

---

### Task 5: the level-3 A/B — the gesture the script never made

**Files:**
- Modify: `tests/ui-scripts/app/menu.txt`
- Modify: `tests/golden/ui/app/menu/`, `tests/golden/ui/app/menu_shell/` (recorded, not hand-written)

**Interfaces:**
- Consumes: everything from Tasks 1-4.
- Produces: nothing.

**Append; do not insert.** The script's later timestamps and every existing snapshot are pinned byte-for-byte by two `_diff` rows. Adding gestures in the middle re-records sixteen snapshots per leg for no reason and buries the new evidence in the churn. The last snapshot is `claim-exited` at `t=16500`, with `InstrumentClipView` as the root — which is the root that gives both the audition-column path and the tail.

- [ ] **Step 1: Add the gestures**

At the end of `tests/ui-scripts/app/menu.txt`, before `quit`, and move `quit` to `20000`:

```
# **The gesture this script never made, and why it is here.** Everything above opens menus and
# turns encoders; the only main-grid presses (`pad 0 7`, `pad 0 6`) happen with no menu open, to
# make a clip. So `SoundEditor::padAction`'s forwarding switch and its `exitCompletely()` tail
# (`gui/ui/sound_editor.cpp:1474-1545`) were never once exercised by the acceptance test, and the
# guest's divergence from both shipped through five task reviews and three whole-branch reviews.
# See the pad-forwarding spec §7.2.
#
# Root here is `InstrumentClipView` (the `claim-exited` snapshot above), which is the one root that
# reaches two arms: the audition column at `x == kDisplayWidth + 1` (`:1500-1502`) and the tail.
17000  button SelectEnc down
17020  button SelectEnc up
17300  snapshot pad-menu-open

# The audition column, forwarded to `instrumentClipView.padAction` (`:1501`). The menu stays open:
# legacy's arm returns `DEALT_WITH` without reaching the tail.
17500  pad 17 4 down
17800  snapshot pad-audition-forwarded

# Released before the next press ON PURPOSE. The audition press latches `UI_MODE_AUDITIONING`, and
# the tail is gated on `currentUIMode == UI_MODE_NONE` (`:1520`) -- so with the pad still held, the
# main-grid press below would not close the menu, and this script would be pinning the wrong
# reason for the right picture.
18000  pad 17 4 up
18200  snapshot pad-audition-released

# A main-grid pad with no shift: no arm matches, so `:1520-1545` closes the menu. This is the
# behaviour Kate ruled on (spec §2.1): legacy is correct, we close on pad press.
18500  pad 3 3 down
18700  pad 3 3 up
19000  snapshot pad-menu-closed
20000  quit
```

Confirm `pad 17 4` is the audition column on this board (`kDisplayWidth + 1`) by reading the existing `auditioning` snapshot's own comment at `:250` — it presses exactly that pad for exactly that reason.

- [ ] **Step 2: Record both legs' goldens, then verify without the flag**

```bash
cmake --build build-sim --target deluge_host -j8
GFX_GOLDEN_UPDATE=1 ctest --test-dir build-sim -R ui_app_scripts
ctest --test-dir build-sim -R ui_app_scripts
```
Expected: the second run PASSES every row, `ui_app_scripts_menu_ab` included. **Read the four new snapshots in both golden trees before believing them** (`scripts/pbm2png.py` for the panels): a golden recorded wrong the first time passes forever afterwards. In particular `pad-menu-closed` must show the clip view with no menu on **both** legs, and `pad-audition-forwarded` must show the menu still up.

- [ ] **Step 3: Run it twice more and diff, for determinism**

```bash
ctest --test-dir build-sim -R ui_app_scripts && ctest --test-dir build-sim -R ui_app_scripts
```
Expected: PASS both times. Run-to-run wobble is a bug in the change, not noise.

- [ ] **Step 4: The mutation — prove it still fails for the right reason**

The spec's §7 asks for the inverse of the usual mutation: this comparison goes red *before* the change, so what has to be demonstrated is that it still would. Temporarily revert the one decision in `ShellHostUI::padAction` — replace `if (claimsGridPad(x, y, velocity))` with `if (true)` — rebuild, and run:

```bash
cmake --build build-sim --target deluge_host -j8
ctest --test-dir build-sim -R ui_app_scripts_menu_ab
```

Expected: **FAIL**, and the failure must name `pad-menu-closed` as an OLED panel that differs between the legs — the OFF leg back in the clip view, the ON leg still showing the menu. Record the checker's exact output in your task report. If it fails on a `.state` line instead, or on a different snapshot, the coverage is not what this task claims and you must say so.

Then restore the line, rebuild, and confirm green again:

```bash
git checkout -- src/deluge/gui/ui/shell_host_ui.cpp
cmake --build build-sim --target deluge_host -j8
ctest --test-dir build-sim -R ui_app_scripts
```

**`git checkout --` on a file with uncommitted work has silently reverted a task's own edits three times in this stack.** `shell_host_ui.cpp` was committed in Task 4, so this is safe — but check `git status` first and confirm the file is clean apart from your mutation.

- [ ] **Step 5: Whole gate block, then commit**

```bash
git add tests/ui-scripts/app/menu.txt tests/golden/ui/app
git commit -m "test(level-3): press a grid pad with a menu open, on both legs"
```

---

### Task 6: record what shipped, in the tree

**Files:**
- Modify: `src/scenes/README.md` (the carry table)
- Modify: `docs/superpowers/specs/2026-08-26-ui-migration-design.md` (§10's defect list, §10.2 item 2)

**Interfaces:**
- Consumes: what Tasks 1-5 actually did — read the diffs, do not copy this plan's intentions.
- Produces: nothing.

**Why this is its own task.** This stack's recurring failure is a finding that lives only in a task report; F2 task 9 had to promote one into the tree for exactly that reason, and the whole reason the divergence in this plan existed is that five of behaviour 2's paths were recorded nowhere. Every row below needs a citation you have opened.

- [ ] **Step 1: Add the carry rows**

In `src/scenes/README.md`'s carry table, one row each:

1. **The two note-editor forwarding arms are not ported** (`sound_editor.cpp:1484`, `:1493`), with the plan's ruling: they are keyed on `inNoteEditor()`/`inNoteRowEditor()`, which read `menuItemNavigationRecord[0]` — never written under the guest — and the roots they serve do not exist. They arrive with `GridPadClaim::None`'s second producer, sites 12 and 13.
2. **Behaviour 4, the firmware-version diagnostics** (`:1521-1543`): Kate's deliberate gap, 2026-09-09. Record all three coordinates and what each prints — `x == 15` SRAM, `x == 14` active voices, `x == 13` PIC firmware version, all at `y == 7` on `&firmwareVersionMenu` — so replicating it is a lookup, not a rediscovery.
3. **The automation interpolation pads** (`:1175-1177`): legacy exempts `x == 0, y == 6|7` from the shortcut arm under `automationView`; `MenuScene` claims them because it cannot see the root view's type. A divergence this plan records rather than closes.
4. **The auditioning half of the shortcut gate** (`:1171-1172`): already dropped by `MenuScene` with its reason; note that `GridPadClaim::ShortcutsOnly` inherits that drop rather than reintroducing the question.

- [ ] **Step 2: Add the §10 defect entry**

In the migration spec's §10 defect list, record **behaviour 3 as a shipped, player-reachable divergence now fixed**, with what it was: with the toggle on, a grid pad pressed under a ported menu did nothing, where legacy closes the menu. Reachable on all eight gated entry sites. Name the fix (this branch) and the reason it survived review (spec §7.2: the acceptance script never made the gesture).

- [ ] **Step 3: Update §10.2 item 2's reachability note**

Item 2 tracks the entry points. Add what this plan measured: `model/clip/instrument_clip_minder.cpp:408` is inherited by three root views and called by all three (`keyboard_screen.cpp:585`, `automation_view.cpp:1209`, `instrument_clip_view.cpp:867`), so the eight gated sites reach **five** distinct legacy roots, not eight views' worth of one.

- [ ] **Step 4: Verify every citation you just wrote**

```bash
for c in <every file:line you cited>; do printf '%-52s %s\n' "$c" "$(sed -n "${c##*:}p" "${c%:*}" | cut -c1-90)"; done
python3 scripts/check_citations.py --strict && python3 scripts/check_citation_shift.py
```
`check_citations.py` does not scan `docs/`, so the loop above is the only thing checking those. A citation that resolves to the wrong line is this stack's most-repeated defect; a citation that resolves to the right line under a sentence it does not support is the second.

- [ ] **Step 5: Whole gate block, then commit**

```bash
git add src/scenes/README.md docs/superpowers/specs/2026-08-26-ui-migration-design.md
git commit -m "docs: what the pad-forwarding port carries, defers and fixed"
```

---

## Self-review

Run against the spec after the plan is written, before Task 1 is dispatched.

**Spec coverage.** §3's seam → Task 1. §3.1's predicate → Task 4 step 4. §3.3's split clause and `closePortedMenu()` → Tasks 2 and 4. §3.4's automation port → Task 3. §4's re-entrancy → Task 4 step 5's comment and the `armTickSoon` example. §5's toggle-off argument → no task: the forwarding site is inside `ShellHostUI::padAction`, which runs only while the guest is current; nothing to gate, and a gate would be the bug. §6.1's behaviours 1-3 → Tasks 1-4, **minus the two note-editor arms**, ruled out above and recorded in Task 6. §6.2's out-of-scope items → Task 6. §6.3's four recording items → Task 6, which carries five. §7's three test levels → Tasks 1-3 (fixture and routing), Task 4 (host suite), Task 5 (A/B). §7.1's coverage number → Task 5 reaches the audition column and behaviour 3: **two of the five implemented paths, plus behaviour 3**, as §7.1 predicted.

**One spec requirement this plan narrows**, stated so nobody has to find it: §6.1 says "the six-way forwarding switch"; this plan implements four arms and the tail. The ruling and its reasoning are at the top, and Task 6 records it in the tree.

**Type consistency.** `GridPadClaim` is `deluge::ui::shell`'s throughout (Tasks 1, 4). `PadForwardTarget`, `PadForwardPerform`, `forwardGridPad`, `setPadForwardPerform` are `deluge::gui::ui`'s throughout (Tasks 2, 3, 4). `grid_pad_claim()` is snake_case (it is `src/shell`/`src/scenes`); `claimsGridPad`, `forwardGridPad`, `setPadForwardPerform` are camelCase (they are `src/deluge`). Task 2 declares five enumerators and Task 3 adds a sixth, `Automation`, which Task 2's spec does not name.

**Placeholders.** The `<...>` markers in Tasks 2 and 3's spec bodies are deliberate and are the only ones: they mark the rig setup that must be copied from `menu_actions_spec.cpp` / `browser_choice_spec.cpp` rather than invented, and each is accompanied by the assertion it must make possible. Every code block that ships is complete.
