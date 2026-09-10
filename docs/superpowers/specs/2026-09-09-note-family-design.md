# The note family — design

**Date:** 2026-09-09
**Status:** design approved (Kate), not yet planned
**Subject:** porting legacy's note editor and note-row editor — the menu tree, the mode, and both
entry points — so that sites 12 and 13 of `gui/ui/shell_menu_entry.cpp`'s fourteen can be gated.

---

## 1. Why this exists, and what it actually is

Two of the six ungated legacy entry points are `InstrumentClipView::enterNoteEditor`
(`gui/views/instrument_clip_view.cpp:3590`) and `enterNoteRowEditor` (`:3758`). Both were left
ungated by F1 task 9 for one stated reason: `MenuContext::Root` has no enumerator for their root
menus, because legacy picks those roots from `currentUIMode` (`gui/ui/sound_editor.cpp:1774-1779`),
which `src/scenes` may not read.

**Both sites' own comments cite that selection at `:1757-1759` and `:1760-1762`. It is at
`:1774-1779`.** Those citations rotted after F1 and nothing caught it, because a bare prose citation
inside a comment is not something `scripts/check_citations.py` resolves. Repointing them is part of
this work.

**The note family is not a batch of menu items. It is a mode**, and that is the finding that set this
design's shape. `grep -n "inNoteEditor()\|inNoteRowEditor()" src/deluge/gui/ui/sound_editor.cpp`
returns fourteen lines across **eight distinct sites**, of which only three are the pad ones
`ui/pads-1` deferred. Legacy's note editor is a menu that the grid *and* both encoders drive, with
its own pad lights, its own greyout and its own exit.

## 2. The mode: eight sites

Every one is a routing decision, not logic.

| # | legacy | behaviour | port home |
|---|---|---|---|
| 1 | `sound_editor.cpp:274-281` (`getGreyoutColsAndRows`) | note editor greys **the sidebar only** (`*cols = 0x03`, `*rows = 0`); note-row editor greys **nothing** (`doGreyout = false`) | `MenuScene::greyout_mask()` |
| 2 | `:617-623` (`buttonAction`) | forwards wholesale to `instrumentClipView.handleNoteEditorButtonAction` / `handleNoteRowEditorButtonAction` | `ShellHostUI::buttonAction` |
| 3 | `:540` (`buttonAction`, AFFECT_ENTIRE) | the arm is **disabled** in both note modes (`&& !inNoteEditor() && !inNoteRowEditor()`) | `ShellHostUI::buttonAction` |
| 4 | `:749-755` (`exitCompletely`) | note editor: `exitNoteEditor()` **and** `uiNeedsRendering(this, 0xFFFFFFFF, 0)`; note-row: `exitNoteRowEditor()`, no repaint | the guest's exit path |
| 5 | `:836-838` (`updatePadLightsFor`) | early return — no source blinks, no `SHORTCUT_BLINK` timer | `MenuScene` |
| 6 | `:1047-1052` (`horizontalEncoderAction`) | `handleNoteEditorHorizontalEncoderAction` / `handleNoteRowEditorHorizontalEncoderAction` | `ShellHostUI::horizontalEncoderAction` |
| 7 | `:1593-1598` (`verticalEncoderAction`) | `handleNoteEditorVerticalEncoderAction` / `handleNoteRowEditorVerticalEncoderAction`, both taking `inCardRoutine` | `ShellHostUI::verticalEncoderAction` |
| 8 | `:1467`, `:1481-1497` (`padAction`) | shortcut arm skipped; main grid → `handleNoteEditorEditPadAction`, sidebar → `exitCompletely()`; note-row → `handleNoteRowEditorPadAction`, **unhandled → `exitCompletely()`** | `gui/ui/shell_pad_forward.cpp`'s two deferred arms |

Every hook already exists on `ShellHostUI` — `padAction` (`shell_host_ui.cpp:778`), `buttonAction`
(`:846`), `horizontalEncoderAction` (`:932`), `verticalEncoderAction` (`:946`), `exitUI` (`:971`).

### 2.1 Three properties of this table that a reader should not have to re-derive

**Site 1 closes a carried gap rather than adding one.** `MenuScene::greyout_mask()` today drops the
note-editor arms with a recorded reason: they "branch on the root UI's type or on a `currentUIMode`,
neither of which `src/scenes` may read". With `Root::NoteEditor` in `MenuContext` that stops being
true — the arms key on the menu root, which the scene owns. The carry row is struck, not re-explained.

**Site 8's two arms are not symmetrical.** The note-editor arm splits by *coordinate* (`x <
kDisplayWidth` vs the sidebar); the note-row arm splits by the handler's own `bool` return. This is
the shape `ui/pads-1` got wrong on its first pass at the automation arm — a decline flattened into a
swallow — and it is the single likeliest defect in this plan.

**Site 3 is the only subtraction in the set.** It is a gate that must *stop* firing, so its test has
to prove an absence, and an absence passes vacuously whenever the gesture never reaches the gate.
See §6.

## 3. Where the mode lives

**Two new `MenuContext::Root` enumerators, `NoteEditor` and `NoteRowEditor`** (`scenes/menu/context.h:68-80`),
supplied by the caller.

This is the faithful home rather than a convenient one: legacy's `noteEditorRootMenu`
(`gui/ui/menus.cpp:1880`) and `noteRowEditorRootMenu` (`:1927`) genuinely *are* root menus, chosen in
the same `switch` that chooses the other six (`sound_editor.cpp:1774-1779`). The entry-point table's
own words are that `Root` "deliberately has no enumerator for it" — an enumerator withheld, not a
wrong home.

Two approaches were rejected. **A separate `note_mode` field on `MenuContext`** splits one legacy
concept across two fields and makes "which root is open" answerable two ways. **A `NoteEditorScene`**
— what spec E originally implied — was already ruled out by the scoping spike: the mode question is
answerable by the caller exactly as sites 1-8 answer theirs, and the thing on screen is a menu, not a
view.

### 3.1 How the caller says it

`openShellMenu` (`util/shell_menu_entry.h:178`) already takes six positional arguments, four of them
UI-fact bools. It gains **a `NoteMode { None, NoteEditor, NoteRowEditor }` enum**, not a seventh
bool: the two modes are mutually exclusive, and two bools can encode a state legacy cannot reach.

## 4. The menu tree

**29 node definitions: four root containers and 25 leaves.**

| container | children |
|---|---|
| `note_editor_root` (`menus.cpp:1880`, a `HorizontalMenu`) | velocity, probability, iterance, fill |
| `note_custom_iterance_root` (`:1859`, a `Submenu`) | divisor + eight step toggles |
| `note_row_editor_root` (`:1927`, a `HorizontalMenu`) | sequence direction (**already ported**), probability, iterance, fill |
| `note_row_custom_iterance_root` (`:1907`, a `Submenu`) | divisor + eight step toggles |

The two custom-iterance families are **20 of the 29** and are structurally identical twins, reached
by `IterancePreset`'s select-chain (`gui/menu_item/note/iterance_preset.h:64`,
`note_row/iterance_preset.h:77`) — the mechanism E2 built. They are the bulk of the count and the
cheapest part of the work.

Both roots are `HorizontalMenu`s. The port renders horizontal pages already (`render_horizontal`,
`Column`, the `columns` widget), so they need no new machinery; they inherit the recorded
`HorizontalMenu::renderingStyle()` carry (§5). None of the five items blocked by
`allowToBeginSessionFromHorizontalMenu` is a note item, so that gap does not bite here.

### 4.1 The leaves are not param bindings, and this is the design's second finding

`note::Velocity::writeCurrentValue()` is **empty** (`gui/menu_item/note/velocity.h:59`). The item
reads `instrumentClipView.gridSquareInfo[y][x].averageVelocity` (`:44-52`) and writes by calling
`instrumentClipView.adjustVelocity(offset)` (`:54-57`). Every note leaf has that shape: a `get`/`set`
over **view-owned** state, not over a `ParamManager` — and `gui/views/instrument_clip_view.h` is
exactly what `scenes-includes-outside` forbids `src/scenes` to name.

Measured rather than estimated — `grep -rho "instrumentClipView\.[a-zA-Z_]*"` over
`gui/menu_item/note/` and `gui/menu_item/note_row/` returns **16 distinct members**:
`getLeftMostNotePressed` (8 uses), `lastAuditionedYDisplay`, `getFillString`, `gridSquareInfo`,
`lastSelectedNoteXDisplay`, `lastSelectedNoteYDisplay`, `createNoteRowForYDisplay`,
`setNoteRowIteranceWithFinalValue`, `adjustNoteIteranceWithFinalValue`,
`setNoteRowProbabilityWithOffset`, `setNoteRowIteranceWithOffset`, `setNoteRowFillWithOffset`,
`adjustVelocity`, `adjustNoteProbabilityWithOffset`, `adjustNoteIteranceWithOffset`,
`adjustNoteFillWithOffset`.

### 4.2 The note bridge

A vtable of function pointers **declared in `src/scenes`** and **installed from `gui/ui`** at wire
time, carrying those sixteen. The nodes stay data, the legacy names stay on the legacy side, and the
bindings are ordinary `get`/`set`.

This is not a new idea: `wireShellGuestServices` (`gui/ui/shell_guest_services.cpp`) already installs
exactly this kind of hook set for the guest's other legacy facts, and F3's `setSampleClaimDispatch`
and `ui/pads-1`'s `setPadForwardPerform` are the same seam at single-function scale.

**It must be synchronous, and that rules out the obvious alternative.** F2's `MenuAction` mailbox
drains on the next tick; legacy calls `readValueAgain()` on the line *after* `adjustVelocity(offset)`
(`velocity.h:54-57`). A tick between the write and the read-back would show as a value trailing the
knob — the same reasoning that made `ui/pads-1` reject forward-on-drop.

### 4.3 Two guards, and they are not the same guard

- **`note::SelectedNote::shouldEnterSubmenu()`** (`gui/menu_item/note/selected_note.h:29-39`) pops
  **"Select Note"** and refuses when `lastSelectedNoteXDisplay`/`YDisplay` are `kNoSelection` or the
  cached square is invalid.
- **`note_row::SelectedNoteRow::shouldEnterSubmenu()`** (`note_row/selected_note_row.h:34-40`) pops
  **"Select Row"** and refuses on `!isUIModeActive(UI_MODE_AUDITIONING)` — a `currentUIMode` read,
  so it resolves through the bridge, not through `MenuContext`.

Both are also `selectEncoderActionIsPermitted()` (`selected_note.h:42`,
`selected_note_row.h:43`), so with nothing selected the knob does nothing either. Two guards on every
leaf of their respective family; the port has no equivalent of either today.

## 5. Scope

**In:** the 29 nodes, the bridge, the eight mode sites, both entry gates, both guards, and the
citation repoint in §1.

**Out, and recorded:**

- **Anything in `instrument_clip_view.cpp`.** The guest calls the nine `handleNote*` handlers legacy
  calls; it does not port them. That file is E5's, and it is ~5,000 lines.
- **The `HorizontalMenu::renderingStyle()` carry.** Legacy renders a `HorizontalMenu` as a plain
  vertical list when the `HorizontalMenus` runtime feature is off
  (`gui/menu_item/horizontal_menu.cpp:596-601`); the port's horizontal pages are horizontal
  unconditionally. The gap predates this work and is already a carry row — it simply becomes visible
  on two more roots.

## 6. Testing, with the coverage stated in advance

**`scenes_spec` with a fake bridge** pins all 25 leaves' `get`/`set` and both guards, with no
`instrumentClipView` in the picture. The bulk of the assurance lives here.

**The legacy-vs-port oracle**, in E3's shape: drive legacy's own `note::*` / `note_row::*` items from
their own sentinel and compare against the ported node. This family suits that pattern better than
anything it has been pointed at so far — 25 leaves that are each a plain `get`/`set`, against E3's
browser and marker, which needed a driver built for them.

**The level-3 A/B** reaches both modes cheaply: site 12 wants a note pad held while SELECT is
pressed, site 13 an audition pad held while SELECT is pressed, and `tests/ui-scripts/app/menu.txt`
already does `pad 0 7 down` + SelectEnc and `pad 17 4 down`. **So the A/B covers entry, the first
page and exit on both modes — and covers neither encoder route nor the pad lights.**

**Site 3 is the weak one and is called out here so the plan cannot skip it.** "AFFECT_ENTIRE does
nothing in note mode" is an absence, and an absence passes vacuously if the gesture never reaches the
gate. Its test must show the same rig producing an *effect* outside note mode — otherwise it repeats
the lesson `ui/pads-1` was written to fix: an acceptance test that never makes the gesture proves
nothing about the gesture.

## 7. Open questions this spec does not answer

- **Whether the two custom-iterance families share one set of node definitions.** They are
  structurally identical and differ only in which bridge entries they call. Sharing halves 18 leaves
  to 9 but gives one node id two parents, which `SceneId::arg` and the menu dump both observe. A
  plan-level decision; the spec requires only that both trees have the shape legacy has.
- **Whether the oracle drives all 25 leaves or a sample.** E3's 26-entry-point oracle was built leaf
  by leaf; 25 more is a real cost, and the honest alternative is a stated sample with the rest
  recorded as uncovered.
