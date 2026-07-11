# Issue #1069 — Remove the slot model from song naming

**Status:** approved design, not yet implemented
**Issue:** [#1069](https://github.com/SynthstromAudible/DelugeFirmware/issues/1069) "new default for naming is broken" (milestone: Release 1.3)

## Problem

On a 7-segment Deluge, repeatedly saving a song to snapshot "states" produces
`185_186`, `185_187`, … instead of `185A`, `185B`, `185C`. The names are also too
long to read on a four-character display, which destroys the workflow the feature
exists for.

### Root cause

`FileItem::displayName` is the sort/search key for the file list (it is the first
member of `FileItem`, and `CStringArray` keys on the first member). On 7-seg only,
`readFileItemsFromFolder()` (`browser.cpp:343-377`) points `displayName` *past* the
`SONG` prefix, so a card file `SONG185.XML` has `displayName == "185.XML"` and
`enteredText` becomes `"185"`. On OLED, `displayName == filename`, so `enteredText`
is `"SONG185"`.

That single divergence causes both halves of the bug:

1. **The letter suffix never happens on 7-seg.** The subslot generator at
   `browser.cpp:691` is guarded by `!memcasecmp(enteredTextChars, "SONG", 4)`.
   On 7-seg the prefix has been stripped, so the guard never passes and the code
   falls through to `doNormal:`, the underscore-suffix path meant for non-numeric
   names. OLED passes the guard and correctly yields `SONG185A` — the letter
   suffix is *current, working behaviour on OLED*, not a legacy artifact.

2. **`doNormal:` then miscounts.** `numberStartPos` is computed against
   `enteredText` (`"185"`, prefix stripped → 4), but the previous file's number is
   read back via `prevFile->getFilenameWithoutExtension()` (`browser.cpp:807`),
   which returns the *raw* name `"SONG185"` with the prefix still attached. The
   index is off by exactly `strlen("SONG")`, and `&"SONG185"[4]` coincidentally
   lands on `"185"` → parses 185 → increments → `185_186`. The `_186` is the slot
   number leaking through a wrong offset, not a counter.

Verified with a standalone harness replaying the real `strcmpspecial()` and
`stringToUIntOrError()` against a simulated card of `SONG001.XML`…`SONG185.XML`:
OLED yields `SONG185A`; 7-seg yields `185_186`, the reporter's exact string.

### Corroborating evidence that the codebase was already drifting away from slots

- `selectEncoderAction()`'s two branches are written for the opposite display's
  convention. The 7-seg branch (`browser.cpp:1054-1060`) requires `enteredText` to
  *start with* `filePrefix` — impossible on 7-seg today. The OLED branch
  (`browser.cpp:1003-1011`) calls `getSlot(enteredText)` on `"SONG185"`, which starts
  with `S` and fails. **Shift+select slot jumping and horizontal-encoder digit-scrub
  are dead on both displays right now.**
- `Song::slot` / `Song::subSlot` (`song.h:228-229`) are assigned in the constructor
  and never read.
- `SaveUI::displayText`'s slot-rendering path (`save_ui.cpp:53-67`) is commented out.

## Design

**One invariant, from which everything else follows:**

> `enteredText` and `FileItem::displayName` always hold the real on-card name
> (`SONG185`), on both displays. The `SONG` / `nnn` split is a *rendering* concern
> and exists only at the point of drawing.

This *is* the removal of the slot model: a name stops being a `(slot, subSlot)` pair
that gets reassembled into a filename, and becomes just a name.

### Delete (slot-as-data-model)

| Site | What it is |
|---|---|
| `browser.cpp:343-377` | 7-seg `displayName` prefix-strip — the source of both bugs |
| `browser.cpp:928-971` | `getUnusedSlot()`'s 7-seg slot-scan branch; the existing OLED branch serves both |
| `slot_browser.cpp:173` | `getCurrentFilenameWithoutExtension()` — re-prefixing + zero-padding hack |
| `slot_browser.cpp:123` | `convertToPrefixFormatIfPossible()` |
| `file_item.cpp:66` | `getDisplayNameWithoutExtension()` — becomes identical to `getFilenameWithoutExtension()`; fold call sites onto the latter |
| `song.h:228-229` | `Song::slot` / `Song::subSlot` — dead |

`SlotBrowser` survives as a class: it still holds genuinely non-slot behaviour
(`beginSlotSession`, `getCurrentFilePath`, horizontal-encoder handling). Only the
slot *semantics* go.

`getCurrentFilePath()` (`slot_browser.cpp:192`) currently reaches the filename via
`getCurrentFilenameWithoutExtension()`. With that override deleted, it uses
`enteredText` directly — which, under the invariant, already *is* the filename.

### Keep, demoted to rendering

`getSlot()` and `setTextAsSlot()` stay, used only by `Browser::displayText`
(`browser.cpp:1517`). It now takes `enteredText + strlen(filePrefix)` rather than a
pre-stripped name. This is what still draws `185A` in four characters.

### Un-invert

The two dead `selectEncoderAction` branches collapse into the single correct one —
strip `filePrefix`, then `getSlot` — restoring shift+select slot jumping and
digit-scrub navigation on both displays.

### Typing (prefix-aware search)

`concatenateAtPos()` truncates, so the typing invariant is that
`enteredText[0..enteredTextEditPos)` is exactly what the user typed, and everything
beyond `enteredTextEditPos` is prediction. A prefix-aware search therefore cannot
merely rewrite the search key — the matched name has to land back in `enteredText`
consistently.

**Rule:** when typing begins with a digit on a browser that has a `filePrefix`, treat
the prefix as *implicitly typed*: seed `enteredText` with `filePrefix` and count those
characters in `enteredTextEditPos`. Typing `1`,`8`,`5` yields `SONG1` → `SONG18` →
`SONG185`, rendered on 7-seg as `1` → `18` → `185`.

This costs nothing. A song name beginning with a digit is *already* unreachable on
7-seg: typing `1` and saving runs `getSlot("1")` → slot 1 → writes `SONG001.XML`, and
even `1st idea` is mangled (`getSlot` reads digit `1`, takes `s` as a subslot letter →
`SONG001S.XML`). Auto-prefixing is strictly no worse, and predictable.

### Explicitly out of scope

- **Zero-padding.** `getUnusedSlot()` keeps its existing dynamic digit count, applied
  uniformly to both displays. An empty `SONGS/` folder yields `SONG1.XML` rather than
  vanilla's `SONG001.XML`. Still display-agnostic, which is the property that matters.
- **Generalising the letter suffix beyond songs.** The `"SONG"` literal at
  `browser.cpp:691` stays a literal. Presets (`SYNT`/`KIT`) keep falling to `doNormal:`
  and its numeric suffix. Changing this would alter current OLED preset behaviour.

## Behaviour after the change

Identical on both displays; only the rendering differs.

```
save 1:  SONG185.XML    7SEG "185"     OLED "SONG185"
save 2:  SONG185A.XML   7SEG "185A"    OLED "SONG185A"
save 3:  SONG185B.XML   7SEG "185B"    OLED "SONG185B"
```

Non-numeric names are unaffected and keep the `doNormal:` numeric suffix
(`MYSONG` → `MYSONG 2`).

**Card compatibility is preserved.** 7-seg previously wrote `enteredText="185"` →
`SONG185.XML`; it now writes `enteredText="SONG185"` → `SONG185.XML`. Identical bytes
on disk. Existing vanilla cards keep working, and a card written by the new firmware
still loads on vanilla.

## Testing

The browser is coupled to display and storage, so the naming logic is not directly
unit-testable as it stands. Plan:

1. Extend the existing standalone harness into a checked-in test that exercises the
   default-name derivation against a simulated `FileItem` list, asserting
   `SONG185 → SONG185A → SONG185B` on both display modes and `185_186` never recurs.
   This requires lifting the name-derivation out of `arrivedInNewFolder()` into a
   pure helper that takes the file list and the current name — worth doing anyway,
   since that function is currently a 270-line `goto` tangle.
2. Manual verification on 7-seg hardware (or the 7-seg display emulation): fresh save,
   repeated save, save on a vanilla card populated with `SONG001`–`SONG185`, and a
   non-numeric name.

## Risks

- `displayName` becoming identical to `filename.get()` makes the field redundant; it
  must stay as the `CStringArray` sort key (first member of `FileItem`), so it is
  retained rather than removed.
- Sort order changes on 7-seg: the file list is now keyed on `SONG185.XML` rather than
  `185.XML`. `strcmpspecial` is numeric-aware and the common prefix is constant across
  all song files, so relative order is unchanged — but folders and non-numeric names
  intermix differently and this needs checking.
