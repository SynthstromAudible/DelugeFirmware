# The note family — design

**Date:** 2026-09-09, revised 2026-09-10
**Status:** revised after planning found the approved design could not be built as written; awaiting Kate's review of the revision
**Subject:** porting legacy's note editor and note-row editor — the menu tree, the mode, and both
entry points — together with the menu input mapping they sit on, so that sites 12 and 13 of
`gui/ui/shell_menu_entry.cpp`'s fourteen can be gated.

---

## 0. What the revision changed, and why

The first version was approved section by section on 2026-09-09. Measuring the tree to write its
plan falsified five of its statements and surfaced one defect underneath the whole feature. Kate
ruled on the largest of them (§3): **fold the fix into this work.** The rest are corrections this
revision makes on the evidence, each stated where it lands:

| # | first version said | the tree says | where |
|---|---|---|---|
| 1 | ported horizontal pages drive like legacy's | **they swap legacy's controls** — and full-page number screens do too | §3 |
| 2 | the note leaves are ordinary `get`/`set` bindings | eight of them write by **offset** and draw **bespoke** value text | §5.2-5.3 |
| 3 | site 3 (AFFECT_ENTIRE) is a subtraction | it is swallowed in the note editor but **acts** in the note-row editor on a kit | §4 |
| 4 | the bridge follows `wireShellGuestServices`' precedent | that function installs *data*; the hook precedent is `bindings::FeedbackHook` | §5.4 |
| 5 | reusing `sequence_direction` under the note-row root is free | legacy edits the **auditioned row** there, an arm the port never wrote | §5.5 |
| 6 | (not addressed) | a select press on a horizontal value column follows **that column's** chain or goes up a level; the port only raises a notification | §3.1-3.2 |
| 7 | (not addressed) — only SoundEditor's eight sites branch on the note mode | legacy's forwarded handlers and offset functions ask `getCurrentUI() == &soundEditor && inNote*Editor()` **21 more times** and dereference the null navigation record for `readValueAgain`/`goUpOneLevel`/`toggleNoteEditorParamMenu` | §4 |

And one site the first version thought needed code needs none: site 5 (§4).

## 1. Why this exists

Two of the six ungated legacy entry points are `InstrumentClipView::enterNoteEditor`
(`gui/views/instrument_clip_view.cpp:3590`) and `enterNoteRowEditor` (`:3758`). F1 task 9 left both
ungated because `MenuContext::Root` has no enumerator for their roots, which legacy chooses from
`currentUIMode` at `gui/ui/sound_editor.cpp:1774-1779`. **Both sites' comments cite that choice at
`:1757-1759` / `:1760-1762`** — stale since F1, in a comment no checker resolves. This work repoints
them.

The note family is **a mode, not a batch of menu items**: fourteen lines in `sound_editor.cpp`
branch on `inNoteEditor()`/`inNoteRowEditor()` across eight sites (§4). And every one of its menus is
a **horizontal page** — legacy's `HorizontalMenus` runtime feature defaults On
(`model/settings/runtime_feature_settings.cpp:157-159`) — which is why §3 had to come first.

## 2. Scope at a glance

1. **The menu input mapping** (§3): value editing on the select encoder everywhere, and legacy's
   instrument-button column focus and page switching on horizontal pages.
2. **Where the mode lives** (§5.1): `MenuContext::Root::NoteEditor` / `NoteRowEditor`, supplied by
   the caller through a `NoteMode` argument to `openShellMenu`.
3. **Three optional `Binding` hooks** (§5.2-5.3): `adjust`, `value_text`, `permitted`.
4. **A note bridge** (§5.4): function pointers declared in `src/scenes`, installed from `gui/ui`.
5. **29 nodes** (§5.5) and the auditioned-row arm of sequence direction.
6. **The mode's routing sites** (§4) and both **entry gates**.

## 3. The menu input mapping — folded in by Kate's ruling

### 3.1 The divergence, measured on both sides

**Legacy.** Every menu turn of the select encoder reaches `SoundEditor::selectEncoderAction`
(`:1072`), which multiplies by 5 while SHIFT is held for a non-submenu item and hands the offset to
the current item. On a full-page leaf that is `Integer::selectEncoderAction`
(`gui/menu_item/integer.cpp:25`, add then clamp), `Decimal::selectEncoderAction`
(`decimal.cpp:57`, `offset × numberEditSize`) or `IntegerRange::selectEncoderAction`
(`integer_range.cpp:33`, the selected edge). On a horizontal page it is
`HorizontalMenu::selectEncoderAction` (`horizontal_menu.cpp:309-326`), which **edits the focused
column and never moves focus**. Focus moves only by `HorizontalMenu::buttonAction` (`:70-107`):
SYNTH/KIT/MIDI/CV select column 0-3 of the visible page (`:96-97`, 140 ms debounce), and
CROSS_SCREEN/SCALE switch pages (`:85-94`). The gold knobs never touch a menu:
`SoundEditor::modEncoderAction` (`:1638`) falls through to `View::modEncoderAction`
(`gui/views/view.cpp:835`), which edits the mod-controllable's param and suppresses its popup only if
the menu is showing that same param (`:893-900`). The horizontal encoder does nothing to a menu —
`MenuItem::horizontalEncoderAction` is an empty base (`menu_item.h:84`) that neither `Submenu` nor
`HorizontalMenu` overrides — except inside `Decimal` (digit cursor, `decimal.cpp:83`) and `Range`
(edge choice, `range.cpp:35`). A select press is acted on at release by `SoundEditor::buttonAction`'s
SELECT arm (`sound_editor.cpp:385`), where `Submenu::selectButtonPress` forwards to a focused value
column (`submenu.cpp:187-194`): a returned menu is entered — which is how both CUSTOM iterance
pages are reached — and `nullptr` goes up a level. `HorizontalMenu` does not override
`selectButtonPress`, and `handleItemAction`'s one caller is the instrument-button path
(`horizontal_menu.cpp:420`).

**Port.** The input contract itself states the swap: `InputSnapshot::turn` is documented as "navigate
/ move focus" and `edit[2]` as "edit the focused value" (`ui/core/input.h:63-64`). `int_value`
reads the gold pair (`ui/oled/values.cpp:81`, the claims at `:87-88`) **under a comment that says it
ports `Integer::selectEncoderAction`** (`:86`); `decimal_value` (`:126`) and `range_value` (`:332`)
do the same and add a fine gold knob legacy has no counterpart for. `render_horizontal`
(`scenes/menu/menu_scene.cpp:763`) edits the focused column from `f.edit(0)`/`f.edit(1)`
(`:772-773`) while `columns()` walks focus on `turn()` (`ui/oled/columns.cpp:401-404`). The two
number screens claim the turn and discard it (`menu_scene.cpp`'s `render_leaf_decimal`,
`render_leaf_range`, `render_leaf_int`). SYNTH/KIT/MIDI/CV/CROSS_SCREEN/SCALE appear only in
`MenuScene::leds`' forced-off list (`:1125-1126`). There is no SHIFT acceleration anywhere. A select press on a value column raises the notification
and stays (`menu_scene.cpp:874`), under a comment attributing that to `handleItemAction`
(`menu_scene.h:218-221`) — the instrument-button path, not the select press.

**Measured, not only read:** the level-2 golden `tests/golden/ui/shell/menu_submenu_horizontal`
changes the column label band on a select turn and the value band on a gold turn. `enum_value` and
`toggle` read `turn()` (`values.cpp:248`, `:303`), so the port is not even self-consistent: an Enum
leaf edits on select, an Int leaf on gold. It is recorded nowhere.

### 3.2 What changes

- **Value widgets edit on the select encoder.** `int_value`, `decimal_value` and `range_value` read
  `f.turn()`, multiplied by 5 while `f.shift()` is held (legacy's `scaledOffset`), and stop claiming
  `edit`. The fine gold knob goes: it is a toolkit invention. `values.h`'s `Range::step` note and
  `input.h:63-64` are rewritten to say what the encoders do.
- **Horizontal pages edit on select and focus by button.** `render_horizontal` edits the focused
  column from `turn()` — raw offset, as a submenu receives it — and does nothing when that column is
  a submenu (`horizontal_menu.cpp:315-318`). `columns()` stops walking focus on the turn. New:
  SYNTH/KIT/MIDI/CV focus the column under that button on the visible page, and pressing the button
  of the column already focused acts as a select press on it (`handleItemAction`, `:663-678`);
  CROSS_SCREEN/SCALE switch page with wrap, keeping the position on the page (`switchVisiblePage`,
  `:345-366`). A button focus onto a range-dependent column runs the existing `MustSelectRange`
  check, as `handleInstrumentButtonPress` does.
- **A select press on a value column follows that column.** Its `select_chain` target is entered;
  with none, the page goes up a level. The notification moves to where legacy raises it: the
  already-focused instrument-button press, which runs the column's own press side effect (a
  toggle flips), discards any chain target, and notifies (`handleItemAction`, `:663-678`).
- **LEDs.** SYNTH/KIT/MIDI/CV light the columns the focused item covers, with legacy's single-item
  exception (`updateSelectedMenuItemLED`, `:546-572`); SCALE and CROSS_SCREEN light while the page
  count exceeds one (`renderOLED`, `:118-119`). Vertical pages keep today's off state.
- **Gold turns fall through to `services::ModKnobs`**, which already ports legacy's popup rule
  (`scenes/services/mod_knobs.cpp:342`). One correction to it: legacy's same-param test forwards
  through a horizontal page to the focused value column (`Submenu::shouldForwardButtons`,
  `gui/menu_item/submenu.cpp:181-185`), so `popup_suppressed` must compare against the focused
  column, not the page node.

### 3.3 What this deliberately does not change, and why each is carried

- **Chained horizontal menus** — SHIFT + CROSS_SCREEN/SCALE (`switchHorizontalMenu`, `:368-398`),
  and the `HorizontalMenuGroup` composites that exist only inside those chains
  (`gui/ui/menus.cpp:2270-2303`, the groups at `:646-648`, `:715-721`, `:876-880`, `:1001`, `:1068`,
  `:1748`). The port has no chain data at all (`menu_scene.cpp`'s `horizontal_page_listing` says so),
  so this is a feature, not an arm. **It does not touch the note family:** no note item is in any
  chain or group, and both note button handlers swallow CROSS_SCREEN/SCALE (§4).
- **Menu-side acceleration** (`HorizontalMenu::calcNextKnobSpeed`, `horizontal_menu.cpp:630`) —
  already a carry row; unchanged.
- **Automation retargeting on a column focus change** (`handlePotentialParamMenuChange`,
  `sound_editor.cpp:676-708`) — it calls the new column's `Automation::buttonAction`, whose arms are
  automation-view swaps only (`gui/menu_item/automation/automation.cpp:71-135`). Automation coupling
  keyed on the root view, like the interpolation-pad row `ui/pads-1` carried.
- **`currentUIMode` gating of select editing** (`selectEncoderUIModes`, `sound_editor.cpp:1069`) —
  the menu has no UI-mode state to read.

### 3.4 The cost, stated

Every level-2 menu script that turns select or gold on a value screen or horizontal page
re-records: `menu_submenu_horizontal`, `menu_decimal`, `menu_range`, `menu_enum`, `menu_toggle`,
`menu_sync_level`, `menu_chain`, `menu_shortcut`, `menu_midi_device`, `menu_gold_turn`,
`param_popup_64`, plus the level-3 `menu.txt`. Four toolkit examples re-pin
(`tests/spec/ui/values_spec.cpp:192`, `:218`, `:389`; `columns_spec.cpp:483`, `:575`).
**`menu_gold_turn`'s header argues the opposite of the fix** — that the menu claims the gold turn so
`ModKnobs` never sees it — and is rewritten, not just re-recorded.

**Ruling taken in this revision, for Kate's review:** the fix covers **full-page number screens as
well as horizontal pages.** It is one mapping with one cause, and fixing only horizontal pages would
leave a vertical Int screen editing on gold beside a horizontal column editing on select — a mode
split legacy does not have.

## 4. The mode: eight sites

| # | legacy | behaviour | port home |
|---|---|---|---|
| 1 | `sound_editor.cpp:274-281` | note editor greys the sidebar (`*cols = 0x03`); note-row editor greys nothing | `MenuScene::greyout_mask()` |
| 2 | `:617-623` | forward to `handleNoteEditorButtonAction` / `handleNoteRowEditorButtonAction` | the guest's button hook |
| 3 | `:540` | the AFFECT_ENTIRE arm's guard fails, so the button reaches site 2 | see below |
| 4 | `:749-755` | `exitNoteEditor()` (plus a repaint); note-row `exitNoteRowEditor()` | `ShellHostUI::closeGuest`, before `close()` |
| 5 | `:836-838` | no source blinks, no shortcut blink | **no code** — see below |
| 6 | `:1047-1052` | forward to `handleNote*HorizontalEncoderAction` | the guest's horizontal hook |
| 7 | `:1593-1598` | forward to `handleNote*VerticalEncoderAction`, both taking `inCardRoutine` | the guest's vertical hook |
| 8 | `:1467`, `:1481-1498` | shortcut arm skipped; note editor main grid → `handleNoteEditorEditPadAction`, sidebar → exit; note-row → `handleNoteRowEditorPadAction`, unhandled → exit | `gui/ui/shell_pad_forward.cpp` |

**Which buttons reach site 2.** Legacy's chain handles SELECT, BACK, SAVE, LEARN, KEYBOARD and
SESSION_VIEW first (`sound_editor.cpp:385`, `:456`, `:488`, `:505`, `:564`, `:605`); each is an
outer `else if` on the button alone, so it consumes the press even when its inner test fails. Every
other button reaches the note handler.

**What the note handlers do with them** (`instrument_clip_view.cpp:3711`, `:3951`): SYNTH/KIT/MIDI/CV
go to `soundEditor.getCurrentMenuItem()->buttonAction(...)` — **a read of the same never-written
navigation record that made `ui/pads-1`'s automation arm a null dereference** — whose target is
`HorizontalMenu::buttonAction`, i.e. §3's column focus. So under the guest those four are fed to
`MenuScene`, not forwarded. X_ENC and SYNC_SCALING go to `instrumentClipView.buttonAction`.
PLAY/SHIFT/MOD_ENCODER_0/1 return `NOT_DEALT_WITH`. Everything else is swallowed — including
CROSS_SCREEN/SCALE — **except AFFECT_ENTIRE in the note-row editor on a kit**, which toggles
`clip->affectEntire` and calls `view.setActiveModControllableTimelineCounter(clip)`.

**Site 3, corrected.** In the note editor AFFECT_ENTIRE is swallowed; in the note-row editor it acts
on a kit. The forward carries both, and the test must show the kit toggle as well as the swallow.

**Site 5 needs no code.** `shortcut_pos` finds nothing in `kNoCells` (`scenes/menu/shortcuts.cpp:94`)
and `source_shortcut_pads` returns `{}` unconditionally (`:454-458`), so `render_grid` lights no pad
once both new roots map to `{&kNoCells, &kNoCells}` in `kByRootV3`/`kByRootV1` (`:336`, `:353`).
Those entries are required anyway: the arrays are aggregate-initialised to `Root::Count`, and a
missing entry would be a null `Cells*`.

**Re-entrancy.** Sites 2, 6, 7 and 8 call legacy handlers from inside the guest's own hooks, and
several can open or close UIs. `ui/pads-1`'s rule applies: nothing touches guest state afterwards
without re-checking, arm the tick only while the guest is current, and **return the handler's own
`ActionResult`** — `REMIND_ME_OUTSIDE_CARD_ROUTINE` included, which `ui/pads-1`'s final review found
the forward path discarding.

**Beyond the eight sites — correction 7, found during execution (Task 4's review).** The eight sites
are SoundEditor's own. The legacy functions sites 2, 6, 7 and 8 forward into, and the offset functions
the bridge (§5.4) writes through, ask the same question themselves:
`getCurrentUI() == &soundEditor && soundEditor.inNote*Editor()` — 21 lines in 7 files
(`instrument_clip_view.cpp` ×14, `view.cpp:340`, `timeline_view.cpp:158`, `action_logger.cpp:99`,
`note_row.cpp:1997`, `notify_drain.cpp:48`, `menu_item/sequence/direction.h` ×3). Under the guest both
halves are false, so each takes its not-in-editor arm: `adjustNoteParameterValue` swallows the first
turn and raises a legacy popup (`instrument_clip_view.cpp:3066`, `:3117`). Several forwarded handlers
also dereference `soundEditor.getCurrentMenuItem()` — `menuItemNavigationRecord[navigationDepth]`,
null under the guest — for `readValueAgain`, `goUpOneLevel` and `toggleNoteEditorParamMenu`
(`:3655`, `:3690-3691`, `:3836`, `:3842`, `:3878`, `:3905-3907`). Approach A holds only if those
reads see the ported mode and those calls reach the ported menu: two legacy-side predicates,
`noteEditorOpen()`/`noteRowEditorOpen()`, replace the pure reads; each navigation call becomes a
gated pair in site 6's spelling whose port arm is the legacy call's port. The legacy leg is unchanged
whenever the ported menu is closed. The plan's Task 7a carries it.

## 5. The menu side

### 5.1 Where the mode lives

Two enumerators, `MenuContext::Root::NoteEditor` and `NoteRowEditor`, appended after `Settings`
(`scenes/menu/context.h:68-80`), whose doc note is rewritten. `openShellMenu`
(`util/shell_menu_entry.h:178`) gains `NoteMode { None, NoteEditor, NoteRowEditor }`, defaulted so the
eight existing sites are unchanged, and overrides `ctx.root` after `context_for` — sites 12/13 pass
the item explicitly (`setup(clip, &noteEditorRootMenu)`), which `context.cpp` records as "the
caller's own choice".

**The kit toggle moves inside that branch**, between context resolution and the open: legacy
toggles `affectEntire` after `setup()` and before `openUI` (`instrument_clip_view.cpp:3590-3598`),
and toggling before `context_for` would change the context it resolves. Everything after the open —
`blinkSelectedNote()`, the repaint, site 13's silent re-audition — stays with the caller.

**No existing node id changes.** Ids come from a pre-order walk of `nodes::kAllRoots` in declaration
order (`scenes/menu/node_id.h:14-18`), so both roots are appended at the end of `kAllRootArray`
(`nodes/nodes.h:144-147`). `sequence_direction` is reached first through its existing parent and
keeps its id. The committed menu dump grows two entries and is regenerated.

### 5.2 Offset writes: `Binding::adjust`

Velocity, probability, iterance and fill, in both families, write by offset:
`instrumentClipView.adjustVelocity(offset)` moves every note under a zoomed-out square relative to
itself, and `gridSquareInfo[y][x].averageVelocity` is an average for that reason. A value `set`
would flatten them; probability's latching bit and iterance's preset/custom toggle also live inside
the offset functions. `Kind::Custom` cannot carry them: the tree has 55 custom nodes against
`kMaxCustomNodes = 56` (`scenes/menu/validate.h:156`), whose standing rule is one above the count,
and a `CustomItem` column on a horizontal page never receives `handle` anyway.

So `Binding` gains `void (*adjust)(const MenuContext&, int32_t offset)`. When set, the edit path
accumulates the offset and commits it through `adjust` instead of computing `read + delta` and
writing the result; `read` stays `get`. The validator requires `get` beside `adjust`.

### 5.3 Bespoke value text and the guard: `value_text`, `permitted`

The horizontal notification prints `name: <number>` (`menu_scene.cpp:1437-1467`). Legacy prints
`getNotificationValue`: `35%` / `35% ltch` for probability, `3 of 4` / `OFF` / `CUSTOM` for iterance,
`OFF` / `NOT FILL` / `FILL` for fill — and the columns draw `35%` / `35L`, `3:4`, and `FILL` struck
through for `NOT_FILL`. So `Binding` gains a value-text hook used by the column and the notification,
and fill's strike-through is drawn by the column renderer it selects.

`note::SelectedNote::shouldEnterSubmenu()` pops **"Select Note"** and refuses when no note is
selected (`gui/menu_item/note/selected_note.h:29-39`); `note_row::SelectedNoteRow` pops **"Select
Row"** unless auditioning (`note_row/selected_note_row.h:34-40`). Both are also
`selectEncoderActionIsPermitted()`, and `HorizontalMenu::selectEncoderAction` consults that before
editing (`horizontal_menu.cpp:321-324`) — so a turn with nothing selected shows the popup and edits
nothing. `Binding` gains `bool (*permitted)(const MenuContext&)`, consulted before an edit; the hook
raises the notice itself, as legacy's side effect does.

### 5.4 The note bridge

`src/scenes` may not include `gui/views/instrument_clip_view.h`. Sixteen `instrumentClipView`
members are named across `gui/menu_item/note/` and `note_row/`, plus the iterance preset labels,
which live in `util/lookuptables/lookuptables.cpp:508` — also outside what `src/scenes` may include.
Fill's strings need no bridge: `FillMode` is in `definitions_cxx.hpp:723`.

A struct of function pointers, declared in `src/scenes/menu/`, installed once at boot from a new
`gui/ui/` translation unit beside `deluge.cpp:804`'s `wireShellGuestServices()` call. **Corrected
precedent:** `wireShellGuestServices` installs data (songs, view-state structs), not hooks. The
hook precedent is `bindings::FeedbackHook` (`scenes/menu/bindings.h:270-275`) — which, measured,
**nothing in the tree ever installs** (`set_feedback_hook` has no caller outside its own spec). That
is a separate finding recorded in §8, not fixed here. The bridge must be synchronous: legacy calls
`readValueAgain()` on the line after each write.

**Reads that allocate.** The note-row leaves' `getIndividualNoteRow` creates a `NoteRow` for a
non-kit clip when none exists (`selected_note_row.h:45-61`); the bridge's reads keep that.

### 5.5 The tree

29 node definitions: four containers and 25 leaves.

| container | children |
|---|---|
| `note_editor_root` (`gui/ui/menus.cpp:1880`, horizontal) | velocity, probability, iterance, fill |
| `note_custom_iterance_root` (`:1859`, vertical) | divisor + eight step toggles |
| `note_row_editor_root` (`:1927`, horizontal) | sequence direction, probability, iterance, fill |
| `note_row_custom_iterance_root` (`:1907`, vertical) | divisor + eight step toggles |

Velocity is a plain number column (`Hint::None` draws legacy's `NUMBER` style). Divisor and the step
toggles write by value and need no hook; each step toggle is relevant only when
`divisor > index` (or index 0 with iterance off). Iterance chains to its family's custom root on
`kCustomIterancePreset` (`definitions_cxx.hpp:715`), through E2's `with_select_chain`.

**Sequence direction under the note-row root edits the auditioned row.** Legacy's
`sequence::Direction::getIndividualNoteRow` has a non-kit arm, keyed on
`soundEditor.selectedNoteRow` and `lastAuditionedYDisplay`, that creates the row if missing
(`gui/menu_item/sequence/direction.h:50-60`, inside `getIndividualNoteRow` at `:42-63`); `selectedNoteRow` is latched from
`isUIModeActive(UI_MODE_AUDITIONING)` at session begin (`:118`). `bindings.h` records that arm as
deliberately unported. Every gated site opens with `currentUIMode == UI_MODE_NONE`, so gating the arm
on `ctx.root == Root::NoteRowEditor` is equivalent on every path the guest can reach. The node also
gets the "Select Row" guard and legacy's extra `NONE` option when a row exists (`getOptions`, `:92-108`, the push at `:103-105`).

## 6. Testing, with coverage stated in advance

- **Toolkit** (`tests/spec/ui`): the value widgets edit on turn, scale by 5 under shift, and ignore
  gold; `columns` no longer moves focus on turn. The four examples in §3.4 re-pin.
- **Menu scene** (`sim/scenes_spec`): instrument-button focus, already-focused press, page switch
  with wrap and position, the LED states, the popup rule on a focused column, and the submenu-column
  no-op.
- **Bindings**: `adjust` commits the accumulated offset exactly once per tick; `value_text` feeds
  column and notification; `permitted` blocks the edit and raises the notice.
- **The 25 leaves**, against a fake bridge: each calls the right bridge entry with the right
  argument, and each guard, relevance rule and chain resolves.
- **The bridge**, against the real `instrumentClipView`: a stated sample, because no existing
  fixture sets up note selection and building one for all sixteen members is a plan of its own.
- **The guest**: button, encoder, pad and exit routing against fakes, including the AFFECT_ENTIRE
  kit toggle proved as an effect, not an absence.
- **Level 3** (`tests/ui-scripts/app/menu.txt`): enter and exit both modes by the legacy gestures
  (a held note pad + SELECT; a held audition pad + SELECT), a select turn that edits a column, and a
  SYNTH press that moves focus. **Not covered at level 3:** both encoder routes into
  `instrumentClipView` and the pad lights. The level-3 script must also turn a gold knob inside a
  menu, which it never has, so the fall-through to `ModKnobs` is seen on both legs.

## 7. Open questions this revision does not answer

- **Whether the two custom-iterance families share node definitions.** Sharing halves 18 leaves to
  9, at the cost of one node id with two parents, which `SceneId::arg` and the dump both observe.
- **The size of the bridge's real-view sample** (§6).

## 8. Found while planning, recorded and not fixed here

- **`bindings::FeedbackHook` is never installed** (§5.4). If that is a gap rather than an intended
  default, MIDI-follow feedback after a ported menu write never fires with the toggle on.
- **The chained-menus feature and `HorizontalMenuGroup`** (§3.3).
- **Legacy's `Submenu::shouldForwardButtons` also forwards `buttonAction` and `selectButtonPress`
  to a horizontal page's focused value column** (`Submenu::selectButtonPress`, `submenu.cpp:187`; `Submenu::buttonAction`, `:196`); the port routes
  `CustomItem::handle` only to the page node. No note node is custom, so it does not bite here.
