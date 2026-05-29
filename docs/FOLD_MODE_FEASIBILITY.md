# Feasibility — Fold Active Notes

> **Status:** Phase 2 analysis. Findings + complexity assessment only — no implementation.
> **Depends on:** `ARCHITECTURE_MAP.md` (§2 Piano Roll, §5 Display).
> **Priority:** Highest. Smallest, most self-contained, independently useful, easiest to justify upstream.

---

## 1. Problem

Composing across octaves spreads notes vertically: bass in octave 1, pads in octave 4,
lead in octave 6. The piano roll shows a contiguous block of pitch rows, most of them empty,
so the musician scrolls constantly and loses context.

This is the problem Ableton Live and Logic solve with **Fold**.

## 2. Desired behavior

Display **only note rows that currently contain ≥1 note**. Hidden rows are purely hidden from
*display* — the underlying note data is unchanged. This is a visibility/navigation feature only.

Out of scope for the first cut (possible later modes): Fold Scale Notes, Fold Chord Notes,
Fold Used + Neighboring. Ship **Fold Active Notes** alone first.

## 3. Why this fits the existing architecture (it's ~70% there)

The piano-roll renderer is **already sparse-row-tolerant**:
- Synth/melodic clips don't store empty pitches as rows; a pitch with no `NoteRow` renders
  black. The render loop already handles a non-contiguous set of populated rows.
- The "does this row have notes?" predicate already exists and is widely used:
  `NoteRow::hasNoNotes()` (`note_row.cpp:3178`, used in 11 sites).
- Filtering is a **display concern** that can live entirely in the render/colour/scroll paths
  without touching `noteRows` or `NoteVector notes`.
- Clip-level boolean display flags that persist to song XML are an established pattern
  (`inScaleMode`, `affectEntire`, `wrapEditing` — written at `instrument_clip.cpp:2299`).

So Fold is mostly *intercepting the row-index mapping* and *adjusting scroll bounds*, plus a
flag and a toggle. No new subsystem.

## 4. Modules that would change

| # | Site | File:line | Change (described, not coded) |
|---|---|---|---|
| 1 | `performActualRender()` | `instrument_clip_view.cpp:7086` (loop 7095–7133) | When fold is on, map each `yDisplay` to the *Nth non-empty* `NoteRow` instead of `yDisplay+yScroll`. |
| 2 | `recalculateColours()` | `instrument_clip_view.cpp:4182` | Use the same filtered mapping so colours line up with displayed rows. |
| 3 | `scrollVertical_limit()` | `instrument_clip_view.cpp:4284` | Bound scroll by *count of non-empty rows* rather than `getNumNoteRows()`. |
| 4 | New helper on `InstrumentClip` | `instrument_clip.{h,cpp}` | `screen-row → (NoteRow*, index)` over non-empty rows; shared by 1–3 to avoid divergence. |
| 5 | `foldMode` flag | `instrument_clip.h` (beside `inScaleMode` `:105`) | One `bool`. |
| 6 | XML read/write | `instrument_clip.cpp` (~`:2299` write, ~`:2473`+ read) | Persist `foldMode` like `affectEntire`. |
| 7 | Button toggle | `InstrumentClipView::buttonAction()` | A SHIFT+button toggle, mirroring the SHIFT+SCALE idiom (`:866`); re-render on toggle. |
| 8 | Display feedback | render path | One `display->haveOLED()` branch: OLED label vs a 4-char tag (e.g. `FOLD`) on 7-seg. |

## 5. Does row filtering already exist?

- **No** dedicated fold/hide-empty mechanism exists.
- **But** the prerequisites do: sparse-row rendering (synth), `hasNoNotes()`, and a clean
  central render loop. This is why the estimate is small.

## 6. Complexity estimate

- **New logic:** ~80–120 lines (filtering + index mapping in the render loop, scroll bounds, helper).
- **Integration:** ~20 lines (flag, XML, button, feedback).
- **Containment:** ~4 functions + 1 helper, all within `instrument_clip_view.cpp` /
  `instrument_clip.{h,cpp}`. No entanglement with other subsystems.

## 7. Risks

- **Kit vs synth.** Kit rows are linear (`getNoteRowOnScreen`, `instrument_clip.cpp:967`);
  synth rows are sparse (binary search, `:998`). The helper must do the right thing for both, or
  Fold should be scoped to **melodic/synth clips first** (recommended) and extended to kits later.
- **Interaction with vertical zoom/scroll and existing scale-mode row collapsing** — verify the
  filtered mapping composes with `inScaleMode` rather than conflicting.
- **Editing a folded row to empty** (delete its last note) should re-fold it out gracefully on the
  next recalculate; define when the filtered set is recomputed (on note add/remove + on toggle).
- **Cursor/selection stability** when the visible set changes under the user.

## 8. Performance impact

- Negligible. Computing the non-empty row set is an O(rows) scan over `noteRows` (tens of
  entries), done on toggle / scroll / edit — not per audio frame, not in the card routine.
- No extra heap; the filter is an index transform, not a copied structure (a small cached
  index array is optional, not required).

## 9. Hardware (OLED + 7-segment) impact

- **Pad grid is display-agnostic** (`pad_leds.h:35–39`, `renderMainPads` signature `ui.h:105–107`).
  The folded view *is* the pad grid lighting different rows — identical on both hardware families.
- Only the optional text feedback differs: one `if (display->haveOLED())` branch.
  This is the cheap (~10%) display category. **The user's 7-segment unit is fully supported with
  trivial extra work.**

## 10. Verdict

**Feasible and well-contained — the right first build.** It works with the grain of the existing
sparse-row renderer, reuses `hasNoNotes()` and the proven clip-flag/XML/SHIFT-toggle patterns, and
costs almost nothing on the dual-display front. Recommended scope for v1: **melodic/synth clips,
Fold Active Notes only**, persisted per clip, toggled by a SHIFT+button combo. Kit support and
additional fold modes are clean follow-ups.

*No code is proposed here; implementation planning is the next phase once this analysis is accepted.*
