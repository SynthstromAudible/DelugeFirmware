# Guest pad forwarding — design

**Date:** 2026-09-09
**Status:** design approved (Kate), not yet planned
**Subject:** porting `SoundEditor::padAction`'s contract to `ShellHostUI`, so a ported menu
behaves like legacy's when a pad is pressed under it.

---

## 1. Why this exists

`ShellHostUI::padAction` (`src/deluge/gui/ui/shell_host_ui.cpp:747`) feeds every pad to the shell and
returns `DEALT_WITH` unconditionally. Legacy's equivalent, `SoundEditor::padAction`
(`src/deluge/gui/ui/sound_editor.cpp:1460`), does four things, and the port does one of them.

This was found while scoping the note-editor menus (§10.2 item 2, sites 12 and 13), which cannot work
without it: legacy's note editor is a **grid-interactive menu**, and its own comment at
`sound_editor.cpp:1482` says *"allow user to interact with main pads"*.

**Two of the gaps are shipped, reachable today, and were recorded nowhere.** They passed F1's five
task reviews and three whole-branch reviews because every one of those was scoped to a diff, and
because the level-3 A/B script never presses a grid pad while a menu is open (§6.3).

## 2. What legacy's contract is

`SoundEditor::padAction` (`sound_editor.cpp:1460`), in order:

| # | behaviour | legacy | ported today |
|---|---|---|---|
| 1 | **Shortcut pads.** Tried first, and **skipped entirely** in settings, note editor and note-row editor | `:1468` `potentialShortcutPadAction(x, y, on)` | **yes** — `src/scenes/menu/menu_scene.cpp:1631`, shift-gated |
| 2 | **Six root-forwarding paths**, by `getRootUI()` identity | see below | **no** |
| 3 | **Exit on any unclaimed pad**, when `currentUIMode == UI_MODE_NONE && on` | `:1545` `exitCompletely()` | **no** |
| 4 | **Firmware-version diagnostics**, `y == 7` on `&firmwareVersionMenu` | `:1522-1546`, `x == 15` SRAM, `x == 14` voices, `x == 13` PIC version | **no** |

The six forwarding paths (behaviour 2):

| root | forwards to | line |
|---|---|---|
| `keyboardScreen` | `keyboardScreen.padAction` | `:1475` |
| `instrumentClipView`, note editor | `instrumentClipView.handleNoteEditorEditPadAction` | `:1484` |
| …its sidebar arm | `exitCompletely()` | `:1488` |
| `instrumentClipView`, note-row editor | `instrumentClipView.handleNoteRowEditorPadAction` | `:1493` |
| …when unhandled | `exitCompletely()` | `:1495` |
| `instrumentClipView`, audition column only (`x == kDisplayWidth + 1`) | `instrumentClipView.padAction` | `:1500-1502` |
| `automationView` | `SoundEditor::handleAutomationViewPadAction` (`:1551`) | `:1508` |
| `performanceView` | `performanceView.padAction` | `:1516` |

**Of the six, exactly one is recorded.** `shell_host_ui.cpp:939-940` notes of the keyboard screen that
forwarding is *"what this guest does **not** do."* That file names `automationView`,
`performanceView`, `instrumentClipView` and `audition` **zero times**.

### 2.1 The two shipped divergences

**Behaviour 3 is the more serious, and it is reachable on all eight gated entry points.** Open the
Song menu over the session view and press any grid pad: legacy falls past every forwarding arm
(`sessionView` matches none) to the tail at `:1545` and **closes the menu**. Under the guest the pad
reaches `MenuScene`, which ignores anything without shift (`menu_scene.cpp:1631`), and the menu stays
open. Dismissing a menu with a pad — a hand already on the grid — is an ordinary gesture, not an edge
case.

**Kate's ruling, 2026-09-09: *"legacy is correct, we need to close on pad press."*** The target is
legacy's behaviour, and the divergence is a defect to fix rather than a behaviour to preserve. This
settles the second open question below: behaviour 3 ships in this plan, not a separate earlier one.

**Behaviour 2's performance-view path is reachable today too.** Site 4 is the performance-view entry
and is gated and shipping. Legacy's comment on that arm reads *"Allow using performance view pads
while in the sound editor menu"*; the guest swallows them.

## 3. The seam

**One new `Scene` virtual**, joining the family at `src/shell/scene.h` — `is_grid_overlay()` (`:385`),
`occludes_grid()` (`:387`), `input_transparent()` (`:423`):

```cpp
enum class GridPadClaim : uint8_t { All, ShortcutsOnly, None };

/// Which grid pads this scene consumes. The rest are forwarded to the legacy root view,
/// as `SoundEditor::padAction` (`gui/ui/sound_editor.cpp:1460`) forwards them.
virtual GridPadClaim grid_pad_claim() const { return GridPadClaim::All; }
```

Three answers because legacy has exactly three behaviours:

| answer | who | legacy's rule |
|---|---|---|
| `All` | root scenes, `BrowserScene`, **default** | the scene owns the grid |
| `ShortcutsOnly` | `MenuScene` on a settings or ordinary root | `:1468` runs, the rest forwards |
| `None` | `MenuScene` on a note-editor root | `:1468` skipped; main grid forwards, sidebar exits |

**Defaulting to `All` is the safe direction**: every existing scene keeps today's behaviour, only
`MenuScene` answers differently, and a scene that forgets to answer forwards nothing rather than
forwarding something it wanted.

**The split follows the architecture already in place.** The *declaration* lives in `src/scenes` — a
scene knows what it is. The *forwarding* lives in `gui/ui/`, beside `shell_action_dispatch.cpp` and
`shell_choice_dispatch.cpp`, because performing it names `instrumentClipView`, `performanceView`,
`automationView` and `keyboardScreen`, which `src/scenes` may not. Same shape, same
`LEGACY_PORT_SEAMS` entry.

`ShellHostUI::padAction` gains one decision before it feeds: ask the current scene, then forward or
feed. Synchronous, one call, no duplicated routing logic.

### 3.1 Two approaches rejected, and why

**Forward-on-drop (deferred).** Feed as today; forward whatever `Router` dropped after `tick()`
routes. Reuses existing machinery (`router.h:198`'s handled bool, `:192`'s `dropped_events()`), but
the forward lands a tick after the press, and press/release pairing across that boundary is where
held-pad gestures break. F3's slicer finding is the same shape: input read a tick later is a
different moment than the one that produced it.

**Pre-route claim query (synchronous).** Ask the shell whether the scene *would* claim this pad.
Matches legacy's ordering, but implements routing's decision a second time — the "two answers to one
question" shape that produced F2's mirror problems.

**`GridPadClaim` is preferred because legacy's own decision is by identity, not by inspection.**
`SoundEditor::padAction` asks *which menu am I* (`:1467`'s `!inSettingsMenu() && !inNoteEditor() &&
!inNoteRowEditor()`), and the port already knows. The declaration is a port of that question; the
other two reconstruct it.

### 3.2 One clause deliberately split across the wall

Behaviour 3's precondition is `currentUIMode == UI_MODE_NONE`, which `src/scenes` may not read. So
the scene declares the **claim** and the `gui/ui/` site decides the **exit**. One legacy statement,
two sides of the wall, deliberately — recorded here so a later reader does not "tidy" the exit into
the scene.

## 4. Re-entrancy

**Forwarding calls a legacy `padAction` from inside the guest's own `padAction`, and some targets
close UIs** — the note-editor sidebar arm calls `exitCompletely()` at `:1488`, the note-row arm at
`:1495`, and `performanceView.padAction` is a full handler that can open things.

So **the guest may not be current when forwarding returns**, and nothing may touch guest state
afterwards without re-checking. This is F2's final fix applied to a second site: `rearmTimer` skips
when `getCurrentUI() != this`, because three `MenuAction` arms end in `openUI(...)`. It is also the
shape of F3 task 2's Critical, where a claim reached `closeUI` on a browser that was never opened and
read `uiNavigationHierarchy[size_t(-1)]`.

Both were found by review rather than design. This spec states the rule in advance instead.

## 5. Toggle-off

**Reachability, and it is the strong form.** The forwarding site is inside `ShellHostUI::padAction`,
which runs only while the guest is current, which happens only through `openShellMenu`'s single
`openUI(&shellHostUI)` behind `shellMenuEnabled()`. Nothing new runs with the toggle off. The
`GridPadClaim` virtual is a declaration read by nothing else.

This is F2 task 6's argument, not F2 task 5's — the forwarding **has real side effects**, so it is
unreachable rather than harmless-if-reached.

## 6. Scope

### 6.1 In scope

Behaviours 1-3: the `GridPadClaim` declaration, the six-way forwarding switch, and the exit on an
unclaimed pad.

Behaviour 3 is **not** split into an earlier plan (§2.1, Kate 2026-09-09). It lands with the
forwarding, because its tail is what the forwarding switch falls through to: implementing the exit
first would mean writing the fall-through twice.

### 6.2 Out of scope, recorded

- **Behaviour 4, the diagnostics** (Kate's call, 2026-09-09: *"deliberate gap, we can replicate
  later"*). Recorded with its three coordinates and what each prints, so replicating it is a lookup
  rather than a rediscovery.
- **`GridPadClaim::None` ships with no producer.** The note-editor roots do not exist — sites 12 and
  13 are §10.2 item 2's remaining work. The arm is correct and carries nothing, in the shape F2 used
  for its inert MIDI transports.

### 6.3 What must be recorded in the tree, not only in a plan

This stack's recurring failure is findings that live only in a task report; F2 task 9 had to promote
one into the tree for exactly that reason. All four land in `src/scenes/README.md`'s carry table:

1. The five unrecorded forwarding paths, named individually.
2. **Behaviour 3, shipped and reachable on all eight gated sites** — also in migration §10's defect
   list, because unlike the others a player meets it immediately.
3. Behaviour 4 as a deliberate gap, with coordinates.
4. `GridPadClaim::None`'s missing producer.

## 7. Testing

**Fixture (`sim/scenes_spec`).** `grid_pad_claim()` is pure scene state: `MenuScene` answers
`ShortcutsOnly` on a settings root and `None` on a note-editor root once one exists; root scenes
answer `All`. Pinned against the roots, not against a constant.

**Host suite (`deluge_spec`).** Cannot hold this — its source list omits the legacy view closures
deliberately, proven three times across F1-F3. The forwarding site gets a stub, as
`dispatchShellActions` and `dispatchShellChoices` both did.

**Level-3 A/B — and this is the first seam where it is the natural oracle.** Add one gesture: open a
menu, press a main-grid pad, snapshot.

**The comparison should go red before the change and green after.** Today the OFF leg closes the menu
and shows the root view; the ON leg keeps the menu open — two different panels. Every previous A/B
extension in this stack had to be *proved capable* of failing, because it passed on arrival. This one
demonstrates the defect on arrival.

That inverts the usual mutation: not *can it fail*, but **does it still fail for the right reason** —
revert the forwarding and confirm the A/B reddens on the panel showing the menu, not on an incidental
`.state` line.

### 7.1 The coverage number, stated in advance

The script boots into `InstrumentClipView` and reaches the audio-clip and song views, so it can drive
**the audition-column path and behaviour 3**. `keyboardScreen`, `automationView` and `performanceView`
need navigation the script does not do.

**So the A/B will cover two of six forwarding paths plus behaviour 3.** F2 said *1 of 7 arms* and F3
said *4 of 5 steps*; both were better for saying so.

### 7.2 Why the divergence shipped

`tests/ui-scripts/app/menu.txt` opens a menu at `t=400`, snapshots it, turns encoders, and exits at
`t=1300`. Its only main-grid presses — `pad 0 7`, `pad 0 6` — occur from `t=3100` to **make a clip**,
with the pad held *while* SelectEnc opens a menu. **No pad is ever pressed while a menu is already
open.**

F1 task 9's lesson was that an acceptance test whose script presses nothing proves nothing. This is
that lesson one level in: the script presses the wrong things. Worth carrying into the next A/B
extension as a question — *what gesture would a player make that this script never makes?*

## 8. Open questions this spec does not answer

- **The other five roots' coverage.** Two of six paths is what the A/B can reach today; whether the
  script should be extended to reach `performanceView` (site 4, gated and shipping) is a plan-level
  decision.
