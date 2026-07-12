# Remove 7SEG display support

**Date:** 2026-07-12
**Branch target:** `next`
**Status:** Design approved, pending implementation plan

## Motivation

The Deluge firmware currently supports two physical displays: the original
4-digit/7-segment panel and the OLED panel. Support is dispatched at runtime —
one binary boots on either unit and picks a `Display` subclass based on a
boot-time hardware probe.

Maintaining the 7SEG path taxes every UI change: each new feature needs a
parallel 4-character rendering path, a 4-character abbreviation in a second
localization table, and a `have7SEG()` branch. There is an official OLED
upgrade path for existing 7SEG units. Dropping 7SEG removes that ongoing tax
and is intended to incentivize the upgrade.

This is a **deliberate hardware-support drop**, not a refactor. `next` and
future releases will not run on 7SEG units.

## Current state

7SEG is runtime-dispatched, not compile-time. `deluge::hid::Display` is an
abstract base with two subclasses; the global `display` pointer is assigned at
boot in `deluge.cpp` based on `deluge_board_probe_oled()`.

The 7SEG surface area:

| Component | Location | Approx. size |
|---|---|---|
| 7SEG display driver | `hid/display/seven_segment.{cpp,h}` | ~840 lines |
| Numeric layer stack (5 classes) | `hid/display/numeric_layer/` | ~490 lines |
| 7SEG localization table | `gui/l10n/g_seven_segment.cpp`, `seven_segment.json` | ~1150 lines |
| Emulated-display runtime feature | `gui/menu_item/runtime_feature/emulated_display.cpp` | ~50 lines |
| Display-type branches | `have7SEG()` (68 sites) / `haveOLED()` (277 sites) | 42+ files |
| Per-UI numeric render hooks | `drawValue` (114 sites), `redrawNumericDisplay` (39 sites) | many files |
| 7SEG-only `Display` virtuals | see below | ~125 call sites |

The 7SEG-only virtuals on `Display`, with call-site counts:
`setText` (73), `setScrollingText` (35), `setTextAsNumber` (12),
`getEncodedPosFromLeft` (4), `setTextAsSlot` (3), `setTextWithMultipleDots` (1),
`getLast` (1).

**These virtuals have empty default bodies in the base class.** An unguarded
call to any of them on OLED hardware today does nothing. This fact is what
makes their removal behavior-preserving, and it is the central assumption of
this design.

The 7SEG localization table is a full `deluge::l10n::Language`
(`built_in::seven_segment`) with fallback to English, selected in the
`SevenSegment` constructor.

## Design

### End state

`deluge::hid::Display` becomes a single concrete OLED display. `DisplayType`,
`have7SEG()`, `haveOLED()`, `have_oled_screen`, `swapDisplayType()`, and the
`SevenSegment` class are all removed. Its interface retains only methods that
do something on an OLED.

The global `display` pointer stays. Renaming it would touch hundreds of call
sites for no behavioral gain; it simply now points at the one implementation.

`l10n::chosenLanguage` becomes unconditionally English.

### Boot flow and the tombstone

`deluge_board_probe_oled()` is retained — we still need to know what panel is
fitted, we just act on it differently:

```cpp
bool have_oled = deluge_board_probe_oled();
if (!have_oled) {
    deluge_display_tombstone();  // writes a static message to the segments
    while (true) {}              // halt; never returns
}
// ...normal OLED boot, unconditionally, from here down
```

The halt goes **early** — before audio, SD, song load, and task registration —
so a 7SEG unit does the minimum work and lands in a known inert state rather
than a half-booted one.

**The tombstone** is a new file, `hid/display/seven_segment_tombstone.{cpp,h}`,
on the order of 40 lines: the letter/number segment lookup tables lifted from
the old `seven_segment.cpp`, a single "encode 4 chars and call
`deluge_display_write_seven_segment()`" function, and nothing else. No layers,
no popups, no blinking, no scrolling, no l10n, no `Display` inheritance. It is
not a display driver; it is a static message printer.

It exists so that a user who flashes `next` onto a 7SEG unit sees *why* it
stopped, rather than concluding they bricked the device. This is the moment the
upgrade nudge gets delivered, so it is worth the 40 lines.

The low-level primitive `deluge_display_write_seven_segment()` lives in
`src/bsp/rza1/control_surface.cpp` and is **out of scope** — the PIC it talks
to also drives the pads and LEDs, so that file stays regardless.

Open detail, to settle during implementation: the exact four characters shown.
`OLED` is the most self-explanatory message expressible in four segments. A
scrolling message would drag the scroll machinery back in and defeat the
purpose.

### The emulated-display feature must go first

The runtime-feature toggle that lets an OLED user simulate a 7SEG panel means
`display->have7SEG()` can be true on OLED hardware. While it exists, no
`have7SEG()` branch is safely constant-foldable. Removing it is what makes the
display type a boot-time constant, and it is therefore a **prerequisite** for
the collapse, not an optional extra.

### Collapse mechanics

The ~345 display-type branches each become their OLED arm, inlined:

- `if (display->haveOLED())` → keep the body, drop the branch
- `if (display->have7SEG())` → delete the body

Most are trivially one or the other. The cases needing judgment are `else`
pairs where the 7SEG arm did something the OLED arm assumed had already
happened.

**Removals are declaration-first.** For each 7SEG-only virtual, remove the
declaration from `Display` *before* touching call sites. Every call site then
becomes a hard build error that must be consciously classified as either:

1. inside a 7SEG guard → delete, or
2. unguarded → it was a no-op on OLED (empty base body), so deleting it is
   behavior-preserving.

Class 2 is exactly what a grep-first approach gets wrong: those sites *look*
like live code and are not. Declaration-first means the toolchain enumerates
the work instead of a human.

The per-UI numeric hooks (`drawValue`, `redrawNumericDisplay`) are UI-class
methods rather than `Display` virtuals, and get the same treatment one class at
a time: delete the override, let the compiler find the callers.

### The one genuinely behavioral case

`displayPopup(char const* shortLong[2], ...)` is the **only** place where 7SEG
is not a no-op but a real fork — it reads `have7SEG()` to choose between a
4-character abbreviation and the full string:

```cpp
displayPopup(have7SEG() ? shortLong[0] : shortLong[1], ...);
```

Collapsing it means every `shortLong` call site drops to `shortLong[1]` (the
long form) and the array parameter degrades to a plain `char const*`. Both arms
typecheck, so **the compiler cannot help here.** Getting a site backwards
silently changes what the user reads. Every one of these is a hand-checked
edit.

### Localization

`strings.h` declares each string once; `g_english.cpp` and `g_seven_segment.cpp`
each define a table for it. Removing the 7SEG table collapses `l10n::get()` to a
single lookup and removes the 7SEG output path from `generate.py`. Any string
that existed solely to carry a 7SEG abbreviation is removed with it.

## Sequencing

One PR, internally staged so a bisect lands on something meaningful and a
reviewer can read it in passes. Each step is what makes the next step's compiler
errors trustworthy.

1. **Remove the emulated-display feature.** Makes the display type a boot-time
   constant. Must be first.
2. **Add the tombstone and the boot halt.** 7SEG hardware is now formally
   unsupported and `have_oled` is guaranteed true past the halt. This is the
   product change, small enough to review on its own.
3. **Delete `SevenSegment`, `numeric_layer/`, and the 7SEG l10n table.** The
   build breaks loudly and comprehensively. This is intended.
4. **Fix the fallout** — the ~345 branch collapses, the ~125 dead virtual call
   sites, the `shortLong` popups. The large, boring commit.
5. **Flatten `Display`** — drop the virtuals, the base/subclass split,
   `DisplayType`. Pure refactor, no behavior change.

Steps 3 and 4 are one continuous "get it compiling again" push. Split only if
the diff becomes unreviewable.

## Verification

In the order it catches things:

- **`./dbt build Debug` at every step.** Beta asserts on. Primary net for steps
  3–5 and most of the value of the plan.
- **Hand-audit of every `shortLong` popup site.** The compiler cannot help;
  both arms typecheck. Enumerate each site and diff the chosen string against
  what an OLED user sees today.
- **Hardware pass on an OLED unit**, targeting the flows that leaned hardest on
  the removed virtuals: sample browser (scrolling text), song load/save naming,
  the menu system, popups in all overloads, and value-editing paths that went
  through `drawValue`. A silently-deleted `setText` shows up here as a blank or
  stale screen.
- **Tombstone check on a 7SEG unit.** Neither the compiler nor an OLED unit can
  cover this. A team member has 7SEG hardware; this pass is delegated to them
  and is a merge gate.

## Risks

- **The unguarded `setText`-family call sites are the sharp edge.** The claim
  that deleting them is behavior-preserving rests entirely on the base-class
  body being empty. If any is reached through a path where the object is not the
  OLED subclass, that reasoning breaks. It should not be possible after step 1,
  but it is the assumption the whole plan rests on and it will be stated
  explicitly in the PR rather than buried.
- **Naming/slot code.** The slot model was already removed and names are
  display-agnostic (issue #1069), which eliminates what would otherwise be the
  nastiest tangle. Re-confirm during step 4 rather than assume.
- **This is a one-way door for 7SEG users.** The tombstone is the mitigation,
  but the changelog and release notes must **lead** with the hardware drop, not
  bury it. Someone who auto-updates and gets a dead panel with no warning is the
  failure case that turns an upgrade incentive into a support fire.

## Out of scope

- `src/bsp/rza1/control_surface.cpp` and the PIC layer (still drives pads/LEDs)
- `deluge_board_probe_oled()` (still needed for the halt)
- Any unrelated cleanup the collapse happens to walk past
