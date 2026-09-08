#!/usr/bin/env python3
r"""Warn about legacy line citations that certainly point at nothing.

The UI-redesign layers (`src/ui`, `src/scenes`, and their specs) document nearly every decision
with a citation into the legacy tree -- `gui/menu_item/gate/selection.h:43-54 selectButtonPress`.
Those citations **rot**, and the rot is not hypothetical: E2 task 11b's own two-line edit to
`gui/ui/menus.cpp` shifted every line below `:1647` by two and broke about fifty citations from
five earlier tasks that had been exactly correct when written. That is a regression a script
catches for free and a convention does not.

WHAT THIS CHECKS, AND WHAT IT CANNOT
------------------------------------
It checks the one thing about a citation that is both mechanical and sound: whether the **whole**
cited range is something no claim could possibly refer to. Three cases, all from E2 task 13's
audit, which is where the predicate and its measurements come from:

  * the range starts past the file's end;
  * the range lies entirely inside the file's leading GPL header block;
  * the range is nothing but `#include` / `#pragma` / `namespace` / brace / blank lines.

There is a **second** predicate, added by E4 task 3 and just as mechanical. The convention is that
a citation names its SYMBOL as well as its line -- `` `View::setModLedStates` (`view.cpp:1548-1679`) ``
-- and that is exactly the thing a line shift breaks while leaving the citation resolvable. So:
when a citation has a symbol written next to it, and that symbol **does occur in the cited file**,
it must occur **inside the cited range**. If it occurs only outside, the range has drifted off it.

That "does occur in the cited file" clause is what keeps the check sound rather than merely strict.
A citation is very often written as `port symbol` (`legacy file:lines`) --
`` `ModKnobs::calculate_knob_pos_for_mod_encoder_turn` … (`view.cpp:949-970`) `` -- where the symbol
names *this* layer and the citation names legacy. Those pair a name with a file that has never
contained it, so there is nothing to compare and they are skipped, not failed.

**Shapes this predicate deliberately skips or narrows.** This list is complete: a shape that is not
here is judged. A shape it cannot judge soundly must produce no verdict, and every exemption below
is one of those, not a convenience.

  * a citation with no backticked token adjacent to it (`` (`:1517`) `` on its own) -- there is no
    symbol to check. This is the biggest hole and it is structural: the bare continuation form
    carries no name, so nothing here can see drift in one. Most of E4 task 3's own drift was of
    exactly this shape;
  * a token that is not a plain identifier: an expression (`` `*getModKnobMode() == whichButton` ``),
    a phrase, an operator. Digging an identifier out of an expression is guesswork;
  * prose between the symbol and the citation. `` `renderOLED`'s polarity switcher (`:125-157`) ``
    does not claim to BE `renderOLED`; only `` `Sym` (`file:a-b`) `` does. Requiring the gap to be
    exactly "` (" took a first run from 493 hits to 255, nearly all of the difference being correct
    interior citations;
  * one of a slash-separated pair of aliases immediately before a citation that is itself two
    ranges, `` `computeCurrentValueForStandardMenuItem` / `computeFinalValueForStandardMenuItem`
    (`value_scaling.cpp:6-8` / `:22-33`) ``. The house convention pairs them positionally -- first
    name with the first range, second with the continuation -- but this predicate finds a symbol by
    taking whichever backticked token sits immediately to the left, which is always the *second*
    name, and would check it against the *first* range. Measured, not guessed: doing that against
    the first run found seven places where it paired the wrong name with the wrong range, one of
    them (`bindings.cpp:36`) checking `computeFinalValueForStandardMenuItem`, a function 14 lines
    below, against the citation that actually names `computeCurrentValueForStandardMenuItem`. Rather
    than build positional pairing for a shape this rare, both names of such a pair are skipped;
  * an identifier of fewer than `MIN_SYMBOL_LEN` characters -- `on`, `kind`, `set` occur everywhere
    and would agree with any range by luck;
  * an identifier that appears **nowhere** in the cited file (the port-symbol case above, and a
    symbol legacy has since deleted -- which is real rot, but rot the "past EOF" and boilerplate
    predicates are the right net for);
  * a symbol whose every occurrence in the cited file is a mention, not a definition -- a pure
    declaration in a header, a macro, a forward declaration, a call site. Concretely, an occurrence
    counts as a definition only in one of three shapes: the name immediately followed by its own
    parameter list (brace/paren nesting tracked so a call's arguments cannot fool this) and that list
    in turn followed -- past any `const`/`override`/`noexcept`, a trailing return type, or a
    constructor's `: base(...), member(...)` initializer list -- by a `{` before any top-level `;`;
    the name immediately preceded by `class` or `struct` (a class/struct declaration, that keyword
    occurring nowhere but there); or the name immediately followed by `::method(` (an out-of-line
    member function -- `` `SoundEditor::buttonAction` `` is as much a definition of `SoundEditor` as
    it is of `buttonAction`, since qualifying a name with its class is what makes it one). A name
    with none of those three has no brace body this can brace-match, so there is nothing to check it
    against; skipped, not failed;
  * a citation into a file **inside the citing file's own layer** -- see `PORT_CITED_ROOTS` for the
    four layers this applies to and for how a reader decides what "inside" means. Until F1 task 3
    this bullet read "a citation into anything but `src/deluge`", full stop, and that was the
    biggest hole in the list, ahead of the bare-continuation one above.

    WHY IT WAS NARROWED, AND WHAT THAT COST (F1 task 3, 2026-09-07). The old rule's rationale --
    "only the legacy tree rots relative to these comments; a citation into `src/ui` or `src/scenes`
    is a citation into a file the same commit can fix" -- is right about a citation written from
    inside the port and wrong about one written from `sim/scenes_spec`, `tests/ui-scripts` or
    `tests/spec`, which are scanned (`SCAN_ROOTS`) and which no commit to `src/scenes` touches.
    Five such citations, all naming `src/scenes/services/mod_knobs.cpp`, had drifted onto
    neighbouring *comment* lines -- one off by fourteen -- and `--strict` read clean over every one
    through a full task review. That was proved by experiment before anything was changed: putting
    `` `show_popup` (`services/mod_knobs.cpp:99999`) `` into `param_popup.txt` -- symbol-adjacent,
    tens of thousands of lines past EOF -- gave "0 certainly-wrong citation(s)", exit 0, while the
    same line written as `` `displayPopup` (`hid/display/oled.cpp:99999`) `` was caught immediately.
    So the file was scanned and the predicates worked; the cited-root test alone hid all five.

    **Measured before it was kept**, the way both prior extensions were. Tree-wide, the widening
    raised resolved citations from **4034 to 4242** (+208: 117 into `src/shell`, 49 into
    `src/scenes`, 35 into `src/ui`, 7 into `src/gfx`), each of which is now subject to the three
    range predicates, and raised the ones the *symbol* predicate can render a verdict on from
    **1580 to 1620** of those (39.2% -> 38.2% of a larger denominator; the port layers are newer and
    carry proportionally more bare-continuation and prose-gap shapes, which is why the ratio dips
    while the absolute count rises). It flagged **8** citations, all of them from `src/scenes` into
    `src/shell`. All 8 were hand-verified against the file they name, all 8 were real drift, **zero**
    false positives, and all 8 were fixed rather than excepted: two copies of `Shell::tick_stack`
    cited `:553-567` when the function is `:573-587` (20 lines, landing inside `Shell::make_context`);
    `deliver_post_render_input` cited `:367` when `Shell::tick`'s step 8 calls it at `:390-391`;
    three copies of `shell/profile.h:156` for `text_or_digits`, which is declared at `:147` (`:156`
    is the blank line after the class's closing brace); `Shell::render_oled`'s `s.canvas.clear()`
    cited `:475` when it is `:662`; and `kSceneStateBytes` cited `scene.h:50` when it is `:49`.

    The five `mod_knobs.cpp` citations that motivated this are not among the 8 because they were
    corrected by hand first (`939cb2c2b`); the point of the widening is that the gate would now
    catch the next five, and the probes below confirm it does.

    **What it deliberately does not reach.** The port index is consulted only when the legacy index
    has no candidate at all -- see `scan` for why -- so a citation naming one of the 24 basenames
    that exist in both trees (`sample.cpp`, `midi_device.h`, `canvas.h`, ...) still resolves to
    legacy and stays invisible if it meant the port file. Under-reporting, chosen over the chance of
    moving a citation that is correct today onto a file it never meant.

**The range is judged against the symbol's actual body, brace-matched, not against a direction.**
Earlier rounds narrowed this to a range ending ABOVE its symbol, on the grounds that a range starting
below is indistinguishable, line-for-line, from a legitimate body citation. That reasoning was sound
about what a *line number alone* can tell you, but the checker does not have to stop at the line
number: braces are physically in the file. So: first, if the cited range contains a line the symbol's
own name is written on (its declaration line, a one-line accessor, a split signature), that is always
sound regardless of any brace body and ends the check. Otherwise, for every occurrence of the symbol
that is a definition (previous bullet), this brace-matches from that definition line to the line of
its matching closing brace, giving that definition's extent `[def_line, body_end]`, extended through
any blank line separating it from the next declaration -- the house convention cites through that gap,
not just to the brace (`view.cpp:1548-1679` for `setModLedStates`, whose brace closes at `:1678`, is
exactly this shape, as is roughly a third of the tree's full-function citations). A citation is judged
clean if its whole range falls inside **any** one of a symbol's extents -- there can be more than one,
because the same name may be defined more than once in a file (overloads, or two classes' same-named
method in one file), and the convention never disambiguates which overload a citation means, so this
does not either. That "any" cannot manufacture a false positive: it only ever adds another interval a
range is allowed to fall inside, so it can turn a flag into a pass but never the reverse; the only
thing it can hide is a citation that drifted into the *wrong* overload's body, which is exactly as
invisible to the "symbol occurs somewhere in the cited range" check this predicate has always been. A
range that falls inside none of a symbol's extents has drifted, in whichever direction: above every
definition, below every one, or straddling a boundary. This retires `DRIFT_WINDOW` along with the
direction narrowing -- a distance-based fudge factor has nothing to add once the real extent is known
exactly.

**Measured, not assumed.** Run tree-wide, the extent test raised the number of citations this
predicate can render a verdict on from 1149 to 1287 of 3151 resolved (36.5% -> 40.8%; 976 to 1073 of
2317 in `src/scenes/**`, 42.1% -> 46.3%). Newly judged, it found 33 citations whose cited range fell
outside their named symbol's real extent, all in shapes braces alone cannot get wrong: a citation
copy-pasted between two near-identical sibling files with different real line numbers
(`sidechain/attack.h`/`release.h`), a comma-joined multi-symbol citation where only the first segment
was checked and against the wrong name (`bindings.cpp`'s `computeCurrentValueForStandardMenuItem` /
`computeFinalValueForStandardMenuItem` pairs, which is what bullet two pages up now skips instead),
an entire wrong function name standing in for the real caller (`PadLEDs::renderExplodeAnimation`
where the real, single caller of each helper was `renderAudioClipCollapseAnimation` /
`renderAudioClipExplodeAnimation`), and about two dozen `getOptions`/`isRelevant`/`readCurrentValue`
overrides cited at another method's lines entirely. All 33 were hand-verified against the file they
name and fixed; none was silenced. Two deliberate mutations -- a citation slid up past its symbol and
one slid down past it into a different function -- were both caught by `--strict`, named by file and
line, and reverted.

It still does **not** check that the cited line *means* what the comment claims -- that is not
mechanizable -- and a citation four lines low that still lands inside its own symbol passes. Task 9
found 176 of that shape by reading, and no predicate would have. So a clean run is not a clean bill
of health. It is a net under two specific regressions, and since E4 task 3 fix round 2 it is a
**blocking** one in CI (`--strict`), on the argument that a warning nobody fails on is what let
three consecutive rounds of drift ship: the cost of a false positive is one named line in the skip
list above, and the cost of a false negative has been measured three times.

  scripts/check_citations.py             # warn (always exit 0), for a quick local look
  scripts/check_citations.py --strict    # exit 1 on any unexcepted hit -- what CI runs
  scripts/check_citations.py --list-all  # ignore the exception list

Known false positives live in `scripts/citation_exceptions.txt`, one per line, in the
`<citing file>:<cited token>` form the warnings print.

IF YOU ARE SWEEPING BY HAND -- WHICH ANY COMMIT THAT INSERTS LINES INTO A CITED FILE MUST
-------------------------------------------------------------------------------------------
This gate is not the mechanism for a sweep and cannot be. It flags a citation that lands somewhere
structurally impossible; a citation that slides three lines inside a doc comment resolves perfectly
and it exits 0 before and after. Every task in this stack that edited a heavily-cited legacy file
has had to remap by hand, and the ratio is worth knowing before you start: F1 task 9 gated eight
entry points, `--strict` went from 0 to **134**, and the number of citations that had actually moved
was **568**.

Nine classes a regex-and-shift-map rewriter cannot see, all of them found the hard way:

  * **bare continuations** (`` `:1804` ``) carry no path and are resolved from the previous citation
    ON THE SAME LINE -- so one written after a citation naming a *different* file resolves to the
    wrong file, and one with no citation before it on its line resolves to nothing and is silently
    skipped by this gate and by your rewriter alike;
  * **comma lists** (`screensaver.cpp:94-96,125-127,223-226`): only the first element matches any
    citation regex. The tail goes stale silently;
  * **citations with a trailing symbol inside the backticks**
    (`` `sound_editor.cpp:2056 maybeGetParentMenu` ``), which do not match a pattern anchored on the
    closing backtick;
  * **your own rewriter run twice**, which shifts a citation you already corrected by hand a second
    time. A shift map is a hypothesis; the file content is the fact;
  * **a citation the SAME COMMIT authored**, which is already written against the post-edit tree and
    which the rewriter then shifts a second time -- the sub-case above, in the form that gets past
    the content check as well. The content check compares `HEAD[old]` against `NOW[new]`, and for a
    freshly-authored citation `old` was never a `HEAD` number, so the comparison succeeds on the
    wrong pair. F1 task 10's own docs commit shipped three of these (`thing_pressed`'s callers,
    `514/522/530` where the file says `513/521/529`), and two of the three were bare continuations,
    so `--strict` could not see them either. **Sweep first, author second** -- or exclude from the
    sweep any file whose citations you wrote in the same commit;
  * **an ambiguous basename.** `midi_learn.h` is both `scenes/services/` and
    `gui/menu_item/song/`; `README.md` is twenty-six files; `settings.h` is two. A matcher keyed on
    the basename will shift citations into the wrong file, and the content check will not notice
    because it re-resolves against the same wrong file. Detect the collision (`git ls-files` by
    basename) and refuse rather than guessing, then do those by hand;
  * **the claim around the citation**, which is the one that is not about numbers at all. A shift map
    keeps a citation pointing where it pointed; it cannot know the same commit made the SENTENCE
    false. F1 task 10 put two arms above `MidiLearn`'s root gate and then swept
    `src/shell/scene.h`, which bumped that gate's line number and left "gates its **entire**
    `handle()`" standing -- **worse than leaving it stale, because an accurate line number reads as
    evidence the text was checked.** For every citation your commit's own diff touches the
    *behaviour* of, re-read the surrounding sentence, not just the number;
  * **a citation written in, or pointing into, a Python script.** `SCAN_ROOTS` below has no
    `scripts/` and `SCAN_SUFFIXES` has no `.py`, so a citation *written in* one is never read; and
    `scripts/` is in neither `CITED_ROOT` nor `PORT_CITED_ROOTS`, so a citation *into* one is
    unresolvable. Both halves are blind, and so is `check_citation_shift.py`, which borrows this
    file's roots. F1 task 12 shipped two of exactly this shape in one commit --
    `check_layer_rules.py`'s `(include, *excludes)` note cited `:214-226` for `rule_covers` (really
    `:716-728`) and `:98` for the row that uses the tuple (really `:313`), *both already wrong at
    that task's own base*, which is how long nothing had looked.

    **Measured, and deliberately NOT closed** (F1 task 12, 2026-09-08). Adding `scripts` to
    `SCAN_ROOTS` and `.py` to `SCAN_SUFFIXES` takes the tree from 2 flags to **13**. All **11** new
    ones are false positives *by construction*, and all 11 are in the two files that cannot avoid
    them: eight in **this docstring**, which quotes deliberately-wrong citations as worked examples
    (`services/mod_knobs.cpp:99999`, `hid/display/oled.cpp:99999`, the eight `src/shell` drifts the
    `CITED_ROOT` widening found), and three in **`citation_exceptions.txt`**, whose entire job is to
    name citations that are wrong. **Zero** new flags come from any of the other 40 scripts.
    Suppressing them needs a per-file exclusion `SCAN_ROOTS` does not have, and a checker that must
    exempt itself and its own exception list to run over its own directory is a mechanism, not a
    configuration change -- so the number is recorded here instead. With those two files excluded it
    is 0, which is the measurement a future round starts from rather than re-derives;
  * **the second endpoint of a range.** A `` :\d+ `` pattern -- which is what every ad-hoc rewriter
    in this stack has been written with -- matches the FIRST number of `` `scene.h:500-552` `` and
    stops, turning it into `510-552`: a range whose start has been swept and whose end has not,
    which is a shape no reader would ever type. F1 task 10 caught one in flight and judged it a
    property of that one rewriter rather than of sweeping. True, and exactly why it belongs here:
    every task in this plan wrote an ad-hoc rewriter and every one of them would hit it. Anchor on
    `` :(\d+)(-(\d+))? `` and rewrite both, or compare both endpoints afterwards.

So run more than one mechanism, and make the second one compare CONTENT -- the text at the citation's
old line in `HEAD` against the text at its rewritten line now -- rather than arithmetic.
`scripts/check_citation_shift.py` is that mechanism, written for this after the trap's seventh
instance and measured over F1's own history before it was kept: it pairs every citation into a file
your range touched, old version against new, and prints both endpoints' text when they disagree. Its
docstring carries the four things it structurally cannot do; the first of them is that it detects
only citations your commit MOVED, never citations that are merely wrong. A third mechanism, cheap and
genuinely independent, is to check each rewrite's delta against `git diff -U0`'s own hunks:
that is what catches a brace-only line paired with the WRONG brace, which a content comparison cannot
see because both lines are `}`.

A fourth, and the only one that catches the two classes no map can reach -- the claim around the
citation, and the citation your own commit authored: for each file your commit actually changed,
list the citations *into* it and read the sentence each one sits in. There is no automated form of
this and there should not be a pretence of one. It is what found F1 task 10's laundered claim in a
SECOND file (`ui-shell-design.md`) after the review had found the first.

**And print the lines. Do not guess them.** F1 task 9's own spot-check of two citations "looked
wrong" and was not -- the ranges had been typed from memory rather than read out of the file, so the
check disagreed with a rewrite that was correct. A verification that fails while measuring the wrong
thing is the sibling of the example that passes while exercising a different mechanism than it names,
and this stack has now shipped both.
"""

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
EXCEPTIONS = REPO / "scripts" / "citation_exceptions.txt"

# Where citations are written. These are the layers of the UI redesign, which is where the
# legacy-citation convention is practised; the rest of the repo does not cite this way.
#
# `tests/ui-scripts` joined in E4 task 14, and it was a real hole rather than a tidy-up: the
# level-2 scripts carry `#` comment blocks in exactly the house citation style, and E4 task 13
# found two wrong ones there BY HAND -- one landing on a closing brace, one naming a 51-line file
# for line 221 -- while this gate, which CI runs with `--strict`, could not see the directory at
# all. Widening it was a SWEEP over 27 pre-existing scripts nothing had ever checked, so it was
# measured before it was done, and exactly ONE thing was flagged -- a citation pinned to a past
# revision, reworded at its site rather than excepted (see
# `tests/ui-scripts/shell/menu_midi_device.txt`).
#
# The sub-counts, with how they were obtained, because a number nobody else can re-derive is an
# argument wearing a measurement's clothes: over `tests/ui-scripts/**/*.txt`, `CITATION_RE` matches
# **74** path-bearing citations and `CONTINUATION_RE` a further **15** bare `` `:NNN` ``
# continuations (89 together); **49** of those resolve to a file under `CITED_ROOT`; **12** of the
# resolved carry a symbol `adjacent_symbol` can extract, i.e. are eligible for the second predicate
# at all. Measured on the tree AFTER the one flag was fixed -- before it, the same script counted
# 90 and 50, because the flagged citation was still written in the live `file:line` form. Re-run it
# against the exact tree you are reporting on.
#
# `.txt` is what the scripts are. It also pulls in the four `CMakeLists.txt` under the other scan
# roots, which is a gain rather than a side effect (`src/scenes/CMakeLists.txt`'s header block
# cites legacy) and which flags nothing today.
#
# `.md` JOINED IN F2 TASK 9, AND IT WAS MEASURED BOTH WAYS FIRST. The paragraph this replaces named
# `src/scenes/README.md` and the design specs as "the densest citation sites in the repo, and no
# suffix here matches them", and called widening its own measured decision rather than a suffix to
# add in passing. F2 task 8 then shipped a stale `README.md` citation that BOTH gates -- this one and
# `check_citation_shift.py`, which borrows these roots -- reported clean, and it was found by reading
# the sentence. That is the fifth distinct blind spot this stack has found in its own citation
# sweeps, and it lands on the READMEs, which is where a contributor reads first.
#
# THE MEASUREMENT, on the tree at the time (F2 task 9, 2026-09-08), from a clean baseline of 0:
#
#   * `.md` alone, over the roots below: **4 flags, all in `src/scenes/README.md`, and 3 of the 4
#     were REAL stale citations** rather than false positives by construction -- `sound_editor.cpp`
#     `:1015` for `beginScreen` (really `:957`; `:1015` is inside `blinkShortcut`), `linked.h:73`
#     for the linked file's base name (really `:72` -- the SAME endpoint F2 task 8 corrected in
#     `nodes/output_items.h` and could not see here), and two bare `` `:762` ``/`` `:770` `` that
#     the row's own continuation rule resolved to `clip_view.cpp` while the sentence said
#     `AudioClipView::adjustLoopLength`, i.e. `audio_clip_view.cpp:780`/`:788`. All four were fixed
#     in the same commit as this widening, so the gate goes back to 0 WITH `.md` scanned. This is
#     the opposite result from the `.py` measurement below, where all 11 new flags were false by
#     construction -- there the number was recorded and the widening refused; here the number is
#     the argument FOR it.
#   * `.md` PLUS `docs/superpowers/specs` in `SCAN_ROOTS`: **31 flags**, i.e. 27 more from the specs
#     tree. **REFUSED.** Most belong to plans this stack never ran (the waveform prescan, the storage
#     execution model, the voices race) and are stale against code nobody here is editing; fixing
#     them is a different task with a different owner, and freezing them behind an exception list
#     would be a mechanism rather than a configuration change. So the two READMEs are gated and the
#     specs are not, which is worth knowing before trusting a green run: **every citation in
#     `docs/superpowers/specs/**` is still hand-checked only.** `docs/superpowers/plans/**` is
#     deliberately stale by policy (a plan records the tree as it stood) and must stay outside.
#
# Prose citations in `.md` really do differ in kind -- a range across two functions, a file with no
# line at all -- which was the original reason for caution. The measurement is what settles it: the
# checker already skips a citation carrying no line, and the range-versus-symbol predicate flagged
# nothing spurious in either README.
SCAN_ROOTS = (
    "src/scenes",
    "src/ui",
    "sim/scenes_spec",
    "tests/spec",
    "tests/ui-scripts",
    "src/shell",
    "src/deluge/gui/ui",
    "src/deluge/util",
)
SCAN_SUFFIXES = {".h", ".hpp", ".cpp", ".txt", ".md"}

# WIDENED, AND MEASURED FIRST (F1 task 5, 2026-09-07). Before this round, `SCAN_ROOTS` contained
# neither `src/deluge` nor `src/shell`, so a citation written *in* either tree was never read by this
# script -- only citations written in the five roots above were. That hole was maximally live at the
# time it was found: spec F's guest (`ShellHostUI` and friends) is written inside `src/deluge/gui/ui/`
# by design, and F1 task 4's two deliverable files (`gui/ui/shell_guest_surfaces.{h,cpp}`) carried
# **45** citations the gate had never read. They were hand-verified instead and all 45 resolved --
# which was the point: the gate reported "0 certainly-wrong citations" over a task whose citations it
# had not looked at.
#
# THE SCOPE CHOSEN, AND WHY NOT MORE. `src/deluge` whole is ~227k lines across 1043 files -- the
# entire legacy application, most of it commented long before this stack's citation convention
# existed. Widening to all of it, measured the same way, resolves 248 more citations and flags
# **16** (2 already-known `sidechain/shape.h:6` resolver artefacts aside), scattered across files no
# task in this plan touches -- `gui/l10n/strings.h`, `model/sample/waveform_navigator.h`,
# `model/sample/waveform_scan.h`, `processing/engines/audio_engine.cpp` -- none of them owned by the
# work that motivated this round. Fixing those is a legitimate future sweep but not this one's: this
# round widens to `src/shell` (this stack's own code, where all four originally-measured flags live)
# plus `src/deluge/gui/ui` specifically (the one legacy subdirectory this plan writes guest code
# into), and leaves the rest of `src/deluge` for a round scoped to it on its own terms.
#
# What that scope found, measured by temporarily adding `src/shell` and `src/deluge/gui/ui` to
# `SCAN_ROOTS` and running `--strict`: the same **four** certainly-wrong citations found in the
# original survey, all pre-existing, none in task 4's own files. Two were genuine line drift and
# were corrected in place: `leds.cpp`'s `indicator_leds.cpp:122-126` for `setLedState` was two lines
# short of the call it actually names (`:128`, inside the `blinksLeft` zero-check), fixed to
# `:122-128`; and `shell.cpp`'s `pad_leds.cpp:984-987` for the `amountDone > 1` snap to
# `setGreyoutAmount(1)` likewise stopped one line short of that call (`:988`), fixed to `:986-988`.
# Both are named WITHOUT the citing line, deliberately and on the same convention as the
# `input_lowering` pair two paragraphs down: a number recording where a citation SAT when a past
# round fixed it is pinned to that round's tree, so writing it in live `file:line` form invites the
# next sweeper to shift it and quietly falsify the history. (F1 task 12 shipped the second of these
# in live form, with a line number, and then had to argue at review why it alone was exempt from its
# own sweep -- while the `input_lowering` pair below had been reworded out of that form for exactly
# this reason. An unmarked exception to a convention the same paragraph follows is what makes the
# next sweeper guess, so it now follows the convention instead of arguing against it.) The other two, `src/shell/input_lowering.cpp:208` and `input_lowering.h:72`, are the
# same citation in both places (`hid/encoders.cpp:242-292` at `e460593c8^`) and are not drift at
# all: they cite the file as it stood at a past revision, before an encoder overhaul cut it to 107
# lines, exactly the shape already worked out for `tests/ui-scripts/shell/menu_midi_device.txt` --
# reworded out of the live `file:line` form rather than excepted, since the citation is correct at
# the revision it names and the tree it is read against is not that revision.
#
# Fixing input_lowering.cpp/.h's wording changed their line counts (+2 net each), which shifted three
# *other* citations that were already being checked before this widening (`src/shell` was already in
# `PORT_CITED_ROOTS`, so `sim/scenes_spec` citing into it was already live) -- `set_input_lock` moved
# from :308 to :311, and two `:298` continuations naming the encoder-button event constructor moved
# to :300. Both are exactly task 11b's regression, caught for free by re-running the measurement
# after the fix rather than trusting the first pass; all three were corrected in the same round.
#
# Resolved citations rose from 4252 to 4500 (+248: 10 into `src/deluge/gui/ui`'s own citations of
# itself do not count here, since those are legacy-into-legacy; the rest split across `src/shell`
# gaining new resolvable targets now that its own files are indexable and `src/deluge` gaining the
# `gui/ui` subtree). Symbol-verdict-eligible citations rose from 1662 to 1732 (+70). After the fixes
# above, `--strict` over the scoped `SCAN_ROOTS` (temporarily widened for the measurement) reports
# 0 shown, 2 known exceptions suppressed -- the same two pre-existing `sidechain/shape.h:6` resolver
# artefacts as before, unrelated to this round.
#
# WIDENED AGAIN, TO `src/deluge/util` (F1 task 9 fix round, 2026-09-07), and the reason is a hole
# this stack opened rather than one it found. `gui/ui/shell_menu_entry.h` was moved to
# `src/deluge/util/` so that `model/clip/instrument_clip_minder.cpp` -- inside
# `check_layer_rules.py`'s `NON_UI` glob -- could include it without raising the `model-includes-ui`
# ratchet. Satisfying one gate walked its four citations straight out of this one, because
# `src/deluge/gui/ui` was in `SCAN_ROOTS` and `src/deluge/util` was not. That is the shape to watch:
# **a header moved to satisfy a layer rule leaves this gate's scope at the same moment**, and the
# move looks clean from inside either checker.
#
# Measured before adding, the same way the round above was: `--strict` over the widened roots reports
# **0 certainly-wrong**, so it is free. It is a small root today -- exactly **4** citation tokens in
# **1** file, that header's own -- and that is the point rather than an argument against it: the
# entry cost nothing and it means the next declaration pushed out of `gui/` for the layer ratchet
# stays judged. The rest of `src/deluge/util` carries no citations at all.

# What a citation may point INTO. The legacy tree, always: it rots relative to these comments and
# nothing in this repo can stop it.
CITED_ROOT = "src/deluge"

# And the port's own layers, but only for a citation made from OUTSIDE the layer it names -- see
# `citing_file_is_inside`. The original rule was legacy-only, on the rationale that "a citation into
# `src/ui` or `src/scenes` is a citation into a file the same commit can fix". That rationale is
# exactly right about a citation written *inside* the port -- `src/scenes/menu/bindings.cpp` naming
# `src/scenes/services/mod_knobs.cpp` moves with it under any reasonable edit, and judging it would
# be noise -- and exactly wrong about one written from `sim/scenes_spec`, `tests/ui-scripts` or a
# spec, which no commit to `src/scenes` touches. Those are scanned (`SCAN_ROOTS`), so before F1
# task 3 they were read and then dropped for pointing at the wrong tree, and five of them, all
# naming `src/scenes/services/mod_knobs.cpp`, had drifted onto neighbouring comment lines while
# `--strict` read clean over every one through a full task review. So the exemption is kept and
# narrowed rather than deleted: same layer, still exempt; across the boundary, judged.
#
# HOW A READER DECIDES WHAT COUNTS AS "INSIDE". Purely by repo path prefix, with the layer roots
# below as the units -- the citing file is inside the cited layer when its repo-relative path is
# that layer's root or begins with it plus a `/`. Nothing about include graphs, build targets or
# CMake libraries enters into it, because the property the rationale rests on is "one commit
# plausibly edits both", and a directory is what a commit is scoped to in practice. Two consequences
# worth stating, since both look like omissions otherwise: the layers do NOT nest for this purpose,
# so `src/ui` citing `src/scenes` is judged (they are separate layers with separate owners, and
# `src/ui` is no better placed to notice `src/scenes` drift than `sim/` is); and `src/deluge` is not
# in this list because it is never exempt -- a citation from inside legacy into legacy is still
# judged, as it always has been.
PORT_CITED_ROOTS = ("src/scenes", "src/shell", "src/ui", "src/gfx")

CITED_SUFFIXES = {".h", ".hpp", ".cpp", ".c"}

# `path.ext:N` or `path.ext:N-M`. The path may be any suffix of the real one (`gate/selection.h`),
# which is how these are written; resolution below turns it into a repo path.
CITATION_RE = re.compile(
    r"([A-Za-z0-9_][A-Za-z0-9_./+-]*\.(?:h|hpp|cpp|c)):(\d+)(?:-(\d+))?"
)
# The bare continuation form -- `` `:55` `` -- which inherits the path from the previous citation
# on the same line. Task 11b reported 19 citations its own sweep could not match for exactly this
# reason, so they are captured here rather than silently skipped.
CONTINUATION_RE = re.compile(r"`:(\d+)(?:-(\d+))?`")

# The symbol written next to a citation, in either of the two house forms:
#   `View::setModLedStates` (`view.cpp:1548-1679`)   -- symbol to the LEFT, its own backticks
#   `gate/selection.h:43-54 selectButtonPress`       -- symbol to the RIGHT, same backticks
# The left form is the common one and is allowed to sit on the *previous* line, because a long
# symbol and its citation are routinely split across two comment lines.
# The left form, and *only* when the citation claims to BE that symbol: the symbol's closing
# backtick, then nothing but " (" before the citation. `` `renderOLED`'s polarity switcher
# (`patch_cable_strength.cpp:125-157`) `` does not claim to be `renderOLED` -- it names a block
# inside it -- and matching it would flag every interior citation in the tree. The trailing
# `` ` `` is the citation's own opening backtick.
LEFT_SYMBOL_RE = re.compile(r"`([^`]+)`\s*\(\s*`?$")
# The same, split across two comment lines: the symbol ends the line above and the citation opens
# this one after nothing but the comment lead-in.
LEFT_SYMBOL_PREV_RE = re.compile(r"`([^`]+)`\s*$")
LEAD_IN_RE = re.compile(r"^\s*(?:///|//|\*|)\s*\(\s*`?$")
# `` `Sym1` / `Sym2` `` immediately before the citation -- a slash-separated pair of aliases, each
# meant for one of two ranges the citation goes on to give. Distinct from a comment's own leading
# `//` or `///`, which also ends in a bare `/` but is not preceded by a backtick-quoted token.
SIBLING_ALIAS_RE = re.compile(r"`[^`]+`\s*/\s*$")
# Nothing but a comment's own lead-in before a candidate -- the same emptiness `LEAD_IN_RE` accepts,
# without requiring what follows it to be a citation's opening.
ONLY_LEAD_IN_RE = re.compile(r"^\s*(?:///|//|\*|)\s*$")
RIGHT_SYMBOL_RE = re.compile(r"\s+([A-Za-z_][A-Za-z0-9_:]*)")
# A plain, possibly-qualified identifier, optionally with an empty or filled argument list. Anything
# else -- an expression, a phrase, an operator -- is not a symbol this can check.
SYMBOL_RE = re.compile(
    r"^(?:[A-Za-z_][A-Za-z0-9_]*::)*([A-Za-z_][A-Za-z0-9_]*)(?:\s*\(.*\))?$"
)
# Below this length an identifier agrees with almost any range by accident.
MIN_SYMBOL_LEN = 4
# How far past a candidate definition's own parameter list this will scan looking for the `{` that
# opens its body (past qualifiers, a trailing return type, or an initializer list) before giving up
# on that occurrence as unreadable rather than trusting a `{` found who knows how far downstream.
MAX_SIGNATURE_SCAN = 4000

BOILERPLATE_RE = re.compile(
    r"^\s*(?:"
    r"#\s*(?:include|pragma|endif|else)\b"
    r"|(?:inline\s+)?namespace\b"
    r"|using\s+namespace\b"
    r"|\}\s*;?\s*(?://.*)?"
    r"|\{\s*"
    r")?\s*$"
)


def cited_index(*roots: str) -> dict[str, list[str]]:
    """Every path under `roots`, indexed by every suffix of itself."""
    index: dict[str, list[str]] = {}
    for root in roots:
        for p in sorted((REPO / root).rglob("*")):
            if not p.is_file() or p.suffix not in CITED_SUFFIXES:
                continue
            rel = str(p.relative_to(REPO))
            parts = p.relative_to(REPO).parts
            for i in range(len(parts)):
                index.setdefault("/".join(parts[i:]), []).append(rel)
    return index


def cited_layer_of(rel: str) -> str | None:
    """Which of `PORT_CITED_ROOTS` a resolved path lies in, or None for the legacy tree."""
    for root in PORT_CITED_ROOTS:
        if rel == root or rel.startswith(root + "/"):
            return root
    return None


def citing_file_is_inside(citing: str, cited: str) -> bool:
    """Is the file writing the citation inside the port layer the citation names?

    Path prefix, nothing cleverer -- see `PORT_CITED_ROOTS` for why that is the right test and
    what it deliberately does not do. Always False for a citation into legacy, which is never
    exempt on these grounds.
    """
    layer = cited_layer_of(cited)
    if layer is None:
        return False
    return citing == layer or citing.startswith(layer + "/")


_LINE_COUNTS: dict[str, int] = {}


def line_count(rel: str) -> int:
    if rel not in _LINE_COUNTS:
        _LINE_COUNTS[rel] = len(
            (REPO / rel).read_text(encoding="utf-8", errors="replace").splitlines()
        )
    return _LINE_COUNTS[rel]


def resolve(
    token: str, index: dict[str, list[str]], recent_dirs: list[str], line: int = 0
) -> str | None:
    """Turn a written citation path into a repo path, breaking ties the way a reader would."""
    hits = index.get(token)
    if not hits:
        return None
    if len(hits) > 1 and line > 0:
        # A candidate too short to contain the cited line is almost never the one meant --
        # `colour.h:43` cannot be `gui/colour/colour.h`, which is a four-line re-export, so it is
        # `gui/menu_item/colour.h`. Only narrows when it leaves something.
        long_enough = [h for h in hits if line_count(h) >= line]
        if long_enough:
            hits = long_enough
    if len(hits) == 1:
        return hits[0]
    # Ambiguous (`mode.h` matches five files). Prefer a file under a directory this same source
    # file has already cited unambiguously, then the shallowest path (`sidechain/shape.h` means
    # `menu_item/sidechain/shape.h`, not `menu_item/reverb/sidechain/shape.h`), then give up in a
    # stable way. This is a heuristic and it is wrong sometimes -- `integer.cpp` in `bindings.cpp`
    # means `patched_param/integer.cpp` and this picks `menu_item/integer.cpp`; those seven live
    # in the exception list, named as resolver artefacts rather than as bad citations.
    for d in reversed(recent_dirs[-4:]):
        for h in hits:
            if h.startswith(d):
                return h
    return min(hits, key=lambda h: (h.count("/"), h))


def header_block_end(lines: list[str]) -> int:
    """1-based line number of the last line of the file's leading `/* ... */` licence block, or 0."""
    for i, line in enumerate(lines):
        s = line.strip()
        if not s:
            continue
        if not s.startswith("/*"):
            return 0
        for j in range(i, len(lines)):
            if "*/" in lines[j]:
                return j + 1
        return len(lines)
    return 0


def certainly_wrong(path: Path, start: int, end: int) -> str | None:
    """Why the whole cited range can refer to no claim, or None if it might be fine."""
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    if start < 1 or start > len(lines):
        return "past EOF"
    end = min(max(end, start), len(lines))
    if end <= header_block_end(lines):
        return "inside the licence header"
    if all(BOILERPLATE_RE.match(lines[n - 1]) for n in range(start, end + 1)):
        return "only include/namespace/brace/blank lines"
    return None


_FILE_SYMBOL_LINES: dict[str, dict[str, list[int]]] = {}


def symbol_lines(rel: str, symbol: str) -> list[int]:
    """1-based lines of `rel` on which `symbol` appears as a whole word."""
    per_file = _FILE_SYMBOL_LINES.setdefault(rel, {})
    if symbol not in per_file:
        pattern = re.compile(r"\b" + re.escape(symbol) + r"\b")
        per_file[symbol] = [
            n
            for n, line in enumerate(
                (REPO / rel).read_text(encoding="utf-8", errors="replace").splitlines(),
                1,
            )
            if pattern.search(line)
        ]
    return per_file[symbol]


_FILE_TEXT: dict[str, str] = {}
_FILE_MASKED: dict[str, str] = {}


def file_text(rel: str) -> str:
    if rel not in _FILE_TEXT:
        _FILE_TEXT[rel] = (REPO / rel).read_text(encoding="utf-8", errors="replace")
    return _FILE_TEXT[rel]


_FILE_LINES: dict[str, list[str]] = {}


def lines_of(rel: str) -> list[str]:
    if rel not in _FILE_LINES:
        _FILE_LINES[rel] = file_text(rel).splitlines()
    return _FILE_LINES[rel]


def masked_text(rel: str) -> str:
    """`file_text(rel)`, but with every comment and string/char literal blanked to spaces (newlines
    kept, so line numbers computed from an offset into this are the same as into the original).

    Brace- and paren-matching below has to trust every `{`, `}`, `(`, `)`, and `;` it sees; one that
    is actually inside a comment or a string literal would misdirect it. `"quote " + ")"` and
    `// note: unbalanced (` are both real code in this tree.
    """
    if rel in _FILE_MASKED:
        return _FILE_MASKED[rel]
    text = file_text(rel)
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j == -1 else j
            out.append(" " * (j - i))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j == -1 else j + 2
            out.append("".join(ch if ch == "\n" else " " for ch in text[i:j]))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" and j + 1 < n else 1
            j = min(j + 1, n)
            out.append("".join(ch if ch == "\n" else " " for ch in text[i:j]))
            i = j
        else:
            out.append(c)
            i += 1
    masked = "".join(out)
    _FILE_MASKED[rel] = masked
    return masked


_FILE_EXTENTS: dict[str, dict[str, list[tuple[int, int]]]] = {}


_CLASS_KEYWORD_RE = re.compile(r"\b(?:class|struct)\s*$")
# `Symbol::method(` right after the match -- an out-of-line member function definition. It is
# unambiguously part of `Symbol`'s own definition (that is exactly what qualifying a name with its
# class means), so its body counts as one of `Symbol`'s extents even though `Symbol` itself is not
# what has the parameter list.
_QUALIFIED_METHOD_RE = re.compile(r"\s*::\s*[A-Za-z_]\w*\s*\(")


def _scan_for_body_open(masked: str, i: int, limit: int) -> int | None:
    """From offset `i`, the position of the `{` that opens a body, or None if a top-level `;` (a
    declaration) is reached first, or nothing recognisable turns up within `limit` characters.
    Tracks paren depth only, so a base-clause's `Base(args)`, a constructor's initializer list, a
    trailing return type, `final`, `override`, or template arguments all pass through harmlessly --
    none of them can contain a top-level `{` or `;` of their own.
    """
    depth = 0
    n = min(len(masked), limit)
    for j in range(i, n):
        c = masked[j]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        elif depth == 0 and c == "{":
            return j
        elif depth == 0 and c == ";":
            return None
    return None


def definition_extents(rel: str, symbol: str) -> list[tuple[int, int]]:
    """Brace-matched `[definition_line, body_end_line]` for every place `rel` actually DEFINES
    `symbol` -- every occurrence that is EITHER:

      * immediately followed by its own parameter list, then, past any qualifiers, a trailing return
        type, or a constructor's initializer list, a `{` reached before any top-level `;` (a function
        or method definition; a call's own arguments cannot fool this, since only what follows the
        matching `)` decides it); OR
      * immediately preceded by the keyword `class` or `struct` (a class or struct declaration; that
        keyword occurs nowhere but there, so this cannot mistake a variable of the type, a return
        type, or any other mention for the declaration itself), with the same `{`-before-`;` test
        applied to what follows the name.

    A pure declaration, a macro, a forward declaration (`;` before `{`, either shape), or a call site
    (whose own parameter list is followed by more expression, not a `{`) contributes nothing here.

    More than one entry means the name is defined more than once in this file -- overloads, or two
    classes' same-named method sharing a file. Nothing here decides which one a citation means; see
    the module docstring for why "any of them contains the range" cannot manufacture a false positive.
    """
    per_file = _FILE_EXTENTS.setdefault(rel, {})
    if symbol in per_file:
        return per_file[symbol]
    text = file_text(rel)
    masked = masked_text(rel)
    n = len(masked)
    pattern = re.compile(r"\b" + re.escape(symbol) + r"\b")
    extents: list[tuple[int, int]] = []
    for m in pattern.finditer(masked):
        i = m.end()
        if _CLASS_KEYWORD_RE.search(masked[: m.start()]):
            # `class Foo final : public Bar { ... };` -- straight to the base-clause/body scan, no
            # parameter list of its own to walk first.
            pass
        else:
            qm = _QUALIFIED_METHOD_RE.match(masked, i)
            if qm is not None:
                i = qm.end() - 1  # at the method's own opening "(", to be walked below
            else:
                while i < n and masked[i].isspace():
                    i += 1
                if i >= n or masked[i] != "(":
                    continue  # not a call, a def, a class/struct name, or Symbol::method -- a mention
            # Walk the parameter list (the symbol's own, or `Symbol::method`'s) so a call's arguments
            # (which may themselves contain `(`, `)`, even a stray `{}` default) cannot be mistaken
            # for what follows it.
            depth, i = 1, i + 1
            while i < n and depth > 0:
                if masked[i] == "(":
                    depth += 1
                elif masked[i] == ")":
                    depth -= 1
                i += 1
            if depth != 0:
                continue  # ran off the end of the file unbalanced -- not a shape this can trust
        body_open = _scan_for_body_open(masked, i, min(n, i + MAX_SIGNATURE_SCAN))
        if body_open is None:
            continue  # a declaration (`;` first), or nothing recognisable within the scan window
        def_line = text.count("\n", 0, m.start()) + 1
        depth, j = 1, body_open + 1
        while j < n and depth > 0:
            if masked[j] == "{":
                depth += 1
            elif masked[j] == "}":
                depth -= 1
            j += 1
        if depth != 0:
            continue  # unbalanced to EOF -- do not trust it
        body_end_line = text.count("\n", 0, j - 1) + 1
        # The house convention cites through the blank line separating a function from the next
        # declaration, not just to its closing brace -- the docstring's own canonical example,
        # `view.cpp:1548-1679` for `setModLedStates`, whose brace closes at :1678, is exactly this
        # shape, as is roughly a third of the tree's citations to a function's full extent. Absorb
        # it into the extent rather than flagging every one of them.
        file_lines = lines_of(rel)
        while body_end_line < len(file_lines) and not file_lines[body_end_line].strip():
            body_end_line += 1
        extents.append((def_line, body_end_line))
    per_file[symbol] = extents
    return extents


def adjacent_symbol(line: str, prev_line: str, start: int, end: int) -> str | None:
    """The identifier written next to the citation spanning [start, end), or None to skip.

    Returns the *last* segment of a qualified name -- `View::setModLedStates` is checked as
    `setModLedStates`, because that is what appears at the definition. See the module docstring for
    the shapes that deliberately yield None.
    """
    candidate: str | None = None
    # Right form: `path:43-54 selectButtonPress`, the symbol trailing inside the same backticks.
    m = RIGHT_SYMBOL_RE.match(line[end:])
    if m is not None:
        candidate = m.group(1)
    else:
        before = line[:start]
        m = LEFT_SYMBOL_RE.search(before)
        if m is not None:
            lead = before[: m.start()]
            # The sibling's own trailing `/` is on THIS line before the candidate, or -- when the
            # candidate is the first thing on its line -- dangling off the end of the previous one:
            # `computeCurrentValueForHalfPrecisionMenuItem` /\n    `computeFinalValueForHalfPrecisionMenuItem`
            # (`value_scaling.cpp:10-12` / `:35-46`).
            if SIBLING_ALIAS_RE.search(lead) or (
                ONLY_LEAD_IN_RE.match(lead) and SIBLING_ALIAS_RE.search(prev_line)
            ):
                return None  # `Sym1` / `Sym2` (`file:A` / `:B`) -- see the docstring
            candidate = m.group(1)
        elif LEAD_IN_RE.match(before):
            m = LEFT_SYMBOL_PREV_RE.search(prev_line)
            if m is not None:
                if SIBLING_ALIAS_RE.search(prev_line[: m.start()]):
                    return None
                candidate = m.group(1)
    if candidate is None:
        return None
    m = SYMBOL_RE.match(candidate.strip())
    if m is None:
        return None
    name = m.group(1)
    return name if len(name) >= MIN_SYMBOL_LEN else None


def symbol_drifted(rel: str, symbol: str, start: int, end: int) -> str | None:
    """Why `symbol` says the range has drifted, or None if it does not say so."""
    lines = symbol_lines(rel, symbol)
    if not lines:
        # The symbol is not in this file at all: a port symbol paired with a legacy citation, or a
        # symbol legacy has deleted. Nothing to compare -- see the docstring.
        return None
    if any(start <= n <= end for n in lines):
        # The range contains a line the symbol's own name is written on -- a class's own
        # declaration line, an one-line accessor, a signature split across the cited range. This is
        # always sound regardless of any brace body: the symbol is right there.
        return None
    extents = definition_extents(rel, symbol)
    if not extents:
        # Every occurrence is a mention, not a definition: a declaration-only header, a macro, a
        # forward declaration. No brace body to brace-match -- see the docstring's skip list.
        return None
    if any(d <= start and end <= b for d, b in extents):
        return None  # inside SOME definition's body -- see the docstring for why "any" is sound

    # Report against whichever extent the range is closest to straddling, so the message points at
    # the definition a reader would actually suspect.
    def _gap(e: tuple[int, int]) -> int:
        d, b = e
        return (d - end) if end < d else (start - b) if start > b else 0

    d, b = min(extents, key=_gap)
    if end < d:
        return f"`{symbol}` is defined at :{d}-{b}, {d - end} line(s) above the cited range"
    if start > b:
        return f"`{symbol}` is defined at :{d}-{b}, {start - b} line(s) below the end of the cited range"
    return (
        f"`{symbol}` is defined at :{d}-{b}, which the cited range only partly overlaps"
    )


def load_exceptions() -> set[str]:
    if not EXCEPTIONS.exists():
        return set()
    return {
        line.split("#", 1)[0].strip()
        for line in EXCEPTIONS.read_text().splitlines()
        if line.split("#", 1)[0].strip()
    }


def scan() -> list[tuple[str, int, str, str, str]]:
    """Every certainly-wrong citation as `(citing file, line, token, resolved path, why)`."""
    index = cited_index(CITED_ROOT)
    # The port layers are a SEPARATE index, consulted only when the legacy one has no candidate at
    # all, and with its own `recent_dirs`. Both halves of that are deliberate. 24 basenames exist in
    # both trees (`sample.cpp`, `midi_device.h`, `canvas.h`, ...), so a single merged index would
    # re-resolve existing legacy citations against new candidates and could move a citation that is
    # correct today onto a port file it never meant -- a false positive in a blocking gate, which is
    # the one outcome this must not buy. Legacy-first makes the widening strictly ADDITIVE: every
    # citation that resolves today resolves to exactly the same file, and the only ones that change
    # are those that resolved to nothing. The cost is that a citation genuinely naming, say,
    # `src/gfx/oled/canvas.h` by a basename legacy also has stays invisible; that is the same shape
    # as the resolver artefacts already named in `citation_exceptions.txt`, and under-reporting is
    # the correct failure for this gate. Qualifying the path (`gfx/oled/canvas.h:12`) is the fix at
    # the citation site, and is what the exception file's own header already recommends.
    port_index = cited_index(*PORT_CITED_ROOTS)
    out = []
    for root in SCAN_ROOTS:
        for p in sorted((REPO / root).rglob("*")):
            if not p.is_file() or p.suffix not in SCAN_SUFFIXES:
                continue
            rel = str(p.relative_to(REPO))
            recent_dirs: list[str] = []
            recent_port_dirs: list[str] = []
            prev_line = ""
            for lineno, line in enumerate(
                p.read_text(encoding="utf-8", errors="replace").splitlines(), 1
            ):
                spans: list[tuple[str, int, int, str, str | None]] = []
                # Where each full citation ends, so a bare `` `:N` `` continuation can inherit the
                # path of the citation that precedes it **on the same line and to its left**.
                # Deliberately not "the last citation anywhere on the line": the table style used
                # throughout `menu/nodes/*.h` writes `| `fooMenu` (`:806`) | `Foo` (`foo.h:21`) |`,
                # where the bare token means `menus.cpp` (named in the prose above the table) and
                # the only citation on the line sits to its RIGHT. Inheriting rightwards turned
                # every such row into a false "past EOF". Unresolvable continuations are skipped --
                # under-reporting is the right failure for a warning-only net.
                anchors: list[tuple[int, str]] = []
                for m in CITATION_RE.finditer(line):
                    token, s, e = (
                        m.group(1),
                        int(m.group(2)),
                        int(m.group(3) or m.group(2)),
                    )
                    resolved = resolve(token, index, recent_dirs, e)
                    if resolved is not None:
                        recent_dirs.append(str(Path(resolved).parent) + "/")
                    else:
                        resolved = resolve(token, port_index, recent_port_dirs, e)
                        if resolved is None or citing_file_is_inside(rel, resolved):
                            # No such file anywhere, or a citation a file makes into its own layer,
                            # which stays exempt. Skipping it here also stops a bare `` `:N` ``
                            # continuation from inheriting the path, which is right: the
                            # continuation is as exempt as the citation it continues.
                            continue
                        recent_port_dirs.append(str(Path(resolved).parent) + "/")
                    anchors.append((m.end(), resolved))
                    spans.append(
                        (
                            resolved,
                            s,
                            e,
                            f"{token}:{m.group(2)}",
                            adjacent_symbol(line, prev_line, m.start(), m.end()),
                        )
                    )
                for m in CONTINUATION_RE.finditer(line):
                    prior = [path for end, path in anchors if end <= m.start()]
                    if not prior:
                        continue
                    s, e = int(m.group(1)), int(m.group(2) or m.group(1))
                    # A bare continuation carries no symbol of its own: the backticked token to its
                    # left is the citation it inherits from, not a name for these lines.
                    spans.append((prior[-1], s, e, f":{m.group(1)}", None))
                for resolved, s, e, token, symbol in spans:
                    why = certainly_wrong(REPO / resolved, s, e)
                    if why is None and symbol is not None:
                        why = symbol_drifted(resolved, symbol, s, e)
                    if why is not None:
                        out.append((rel, lineno, token, resolved, why))
                prev_line = line
    return out


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument(
        "--strict", action="store_true", help="exit 1 on any unexcepted hit"
    )
    ap.add_argument(
        "--list-all", action="store_true", help="ignore scripts/citation_exceptions.txt"
    )
    a = ap.parse_args()

    excepted = set() if a.list_all else load_exceptions()
    hits = scan()
    shown = [h for h in hits if f"{h[0]}:{h[2]}" not in excepted]
    for rel, lineno, token, resolved, why in shown:
        print(f"{rel}:{lineno}: citation `{token}` -> {resolved}: {why}")
    skipped = len(hits) - len(shown)
    print(
        f"check_citations: {len(shown)} certainly-wrong citation(s)"
        + (f", {skipped} known exception(s) suppressed" if skipped else "")
        + (
            ""
            if a.strict
            else " -- warning only, see the module docstring for what this predicate cannot see"
        )
    )
    return 1 if (a.strict and shown) else 0


if __name__ == "__main__":
    sys.exit(main())
