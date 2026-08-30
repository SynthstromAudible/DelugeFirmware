#!/usr/bin/env python3
"""Layer-rule ratchet enforcing the UI redesign's dependency direction.

Each rule is (name, from_glob, kind, regex, spec). `kind` is "include" (match the include
path) or "symbol" (match any source line). The baseline holds counts per (file, rule) and
may only shrink: a PR that raises a count, or adds a new (file, rule) pair, fails.

  scripts/check_layer_rules.py            # check (exit 1 on regression)
  scripts/check_layer_rules.py --list     # print current counts
  scripts/check_layer_rules.py --update   # rewrite the baseline from the tree
"""

import argparse
import re
import sys
from fnmatch import fnmatch
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BASELINE = REPO / "scripts" / "layer_rules_baseline.txt"
SUFFIXES = {".c", ".cpp", ".h", ".hpp"}
NON_UI = "src/deluge/{model,playback,processing,storage,io,modulation,memory,sync,util,dsp}/**"
RULES = [
    (
        "model-includes-ui",
        NON_UI,
        "include",
        r"^(gui/(?!l10n/)|hid/)",
        "2026-08-26-ui-notifications-design.md",
    ),
    (
        "model-calls-ui",
        NON_UI,
        "symbol",
        (
            r"\b(uiNeedsRendering\(|renderUIsForOled\(|display->|getRootUI\(|getCurrentUI\(|currentUIMode|isUIModeActive\(|"
            r"indicator_leds::|PadLEDs::|uiTimerManager\.|openUI\(|changeRootUI\()"
        ),
        "2026-08-26-ui-notifications-design.md",
    ),
    (
        "model-calls-display",
        NON_UI,
        "symbol",
        r"\bdisplay->",
        "2026-08-26-ui-notifications-design.md",
    ),
    (
        "gfx-includes-outside",
        "src/gfx/**",
        "include",
        r"^(?!gfx/).*/",
        "2026-08-26-ui-stack-redesign-design.md",
    ),
    (
        "notify-is-leaf",
        "src/notify/**",
        "include",
        r"^(?!notify/).*/",
        "2026-08-26-ui-notifications-design.md",
    ),
    (
        # Slashed paths outside ui/ and gfx/ -- which is every project header except the handful
        # named explicitly below (definitions*, board_config.h, board_layout.hpp): a std header
        # (<cstdint>, <span>) has no slash and no such name, so it still never matches.
        "ui-includes-outside",
        "src/ui/**",
        "include",
        r"^(?!ui/|gfx/)(?:.*/|definitions.*|board_config\.h|board_layout\.hpp)",
        "2026-08-26-ui-toolkit-design.md",
    ),
    (
        "ui-oled-no-grid",
        "src/ui/oled/**",
        "include",
        r"^ui/grid/",
        "2026-08-26-ui-toolkit-design.md",
    ),
    (
        "ui-grid-no-oled",
        "src/ui/grid/**",
        "include",
        r"^ui/oled/",
        "2026-08-26-ui-toolkit-design.md",
    ),
    (
        # The input-consumption contract (ui/core/frame_context.h): a widget reads input only
        # through its frame's accessors -- f.turn(), f.edit(0), f.select_pressed() -- so that
        # focus masking and input claims are not something a widget can forget to apply. Reaching
        # into the snapshot behind the frame skips both. The two frame files are excluded because
        # they *are* the accessors: `ctx.input.turn` inside FrameInput::turn() is the one place
        # the field may be read.
        #
        # Line-based, like every rule here: `auto& in = f.ctx.input;` followed by `in.turn` slips
        # past it, and a `/* ... */` block comment naming `input.turn` counts (only `//`-prefixed
        # lines are skipped). It is a ratchet against drift, not a proof.
        "ui-widgets-read-input-directly",
        ("src/ui/{oled,grid}/**", "src/ui/{oled,grid}/frame.{h,cpp}"),
        "symbol",
        r"\binput\.(turn|edit|horizontal|vertical|select_pressed|back_pressed|shift|now_ms)\b",
        "2026-08-26-ui-toolkit-design.md",
    ),
    (
        # Mirrors ui-includes-outside for src/shell: only shell/**, ui/**, gfx/**, notify/** and
        # libdeluge/** (the one hardware seam, board_surfaces.cpp/profile.h) may be included --
        # never model/, gui/, hid/, or the root headers (definitions*, board_config.h,
        # board_layout.hpp) that would pull the model layer in by the back door.
        "shell-includes-outside",
        "src/shell/**",
        "include",
        r"^(?!shell/|ui/|gfx/|notify/|libdeluge/)(?:.*/|definitions.*|board_config\.h|board_layout\.hpp)",
        "2026-08-26-ui-shell-design.md",
    ),
    (
        # Spec G §5: the shell reads no wall clock of its own -- every tick and gate deadline is
        # driven by the `now_ms` the caller passes in, so the shell can be driven by a fake clock
        # in a spec exactly as it is driven by the real one on device. Two rows, one glob each
        # (this checker matches one glob per rule row): src/shell/** and src/ui/** both apply.
        "shell-no-clock",
        "src/shell/**",
        "symbol",
        r"\bdeluge_clock_|\bstd::chrono\b|\bclock\(\)|\btime\(",
        "2026-08-26-ui-test-harness-design.md",
    ),
    (
        # Same rule, src/ui/** side -- the scenes and widgets that draw on the shell's ticks must
        # be exactly as clock-free as the shell that drives them (task-5 ratchet extension).
        "shell-no-clock",
        "src/ui/**",
        "symbol",
        r"\bdeluge_clock_|\bstd::chrono\b|\bclock\(\)|\btime\(",
        "2026-08-26-ui-test-harness-design.md",
    ),
    (
        # Mirrors shell-includes-outside for src/scenes: the layer's own CMakeLists.txt
        # (src/scenes/CMakeLists.txt) is the authority -- deluge_scenes may include scenes/**,
        # shell/**, ui/**, gfx/**, notify/**, libdeluge/**, and -- being the first UI layer
        # allowed to reach into the model -- model/**, modulation/**, playback/**, processing/**,
        # storage/**, gui/l10n/**, definitions_cxx.hpp. Never gui/ otherwise (gui/l10n/ is the
        # sole exception), never hid/. `definitions_cxx.hpp` sits in the allow-list itself (not
        # the trailing catch-all group, unlike shell-includes-outside's `definitions.*`, which
        # shell must NOT reach): scenes is deliberately the first model-aware UI layer, so the
        # header that pulls in `params::Kind` and friends is expected, not a back door.
        #
        # `dsp/**` joined the list in task 7 (spec E1), on exactly the argument task 3 made for
        # `processing/**` below: `check_layer_rules`'s own NON_UI glob already counts
        # `src/deluge/dsp/` as non-UI, held to the same no-`gui/`, no-`hid/` rules as `model/`. It
        # is needed because the reverb menu's relevance predicates ask
        # `AudioEngine::reverb.getModel()` (`gui/menu_item/reverb/hpf.h:27`) and
        # `processing/engines/audio_engine.h` only forward declares `deluge::dsp::Reverb` -- no
        # `model/` or `processing/` header completes it, so `menu/relevance.cpp` cannot ask the
        # question at all without this.
        #
        # `io/**` joined the list in task 8 (spec E1), on that same argument: `check_layer_rules`'s
        # own NON_UI glob counts `src/deluge/io/` as non-UI, held to the same no-`gui/`, no-`hid/`
        # rules as `model/`. The settings root is where it became unavoidable -- `midiEngine`
        # (`io/midi/midi_engine.h`), `MIDIDeviceManager` (`io/midi/midi_device_manager.h`) and
        # `MIDITranspose` (`io/midi/midi_transpose.h`) hold the state seven of the MIDI page's
        # items edit, and there is no `model/` spelling of any of them.
        #
        # `processing/**` joined the list in task 3 (spec E1). It is not a widening of what
        # "model-aware" means -- `check_layer_rules`'s own NON_UI glob above already counts
        # `src/deluge/processing/` as non-UI, held to the same no-`gui/`, no-`hid/` rules as
        # `model/` -- it is where two of the types spec E §4's `MenuContext` is *defined* in terms
        # of happen to live: `Sound` (`processing/sound/sound.h`) and `Source`
        # (`processing/source.h`). `menu/context.cpp` cannot resolve `sound->sources[i]`,
        # `source->ranges` or `sound->voicePriority` from a forward declaration, so without this
        # the ported `SoundEditor::setup` could not be written at all.
        "scenes-includes-outside",
        "src/scenes/**",
        "include",
        r"^(?!scenes/|shell/|ui/|gfx/|notify/|libdeluge/|dsp/|io/|model/|modulation/|playback/|processing/|storage/|gui/l10n/|definitions_cxx\.hpp)(?:.*/)",
        "2026-08-26-ui-scenes-menus-design.md",
    ),
    (
        # Narrower than the rule above, and the reason `menu::model_stack_for()` exists (spec E1
        # task-3 ruling 6): `model/model_stack.h` may be reached only by the two files that own the
        # model-stack boundary. Everything else in src/scenes goes through `model_stack_for`, so
        # there is exactly one place that knows how a menu's stack is assembled.
        #
        # `menu/bindings.cpp` does not exist yet -- Task 4 adds it; excluding it now means that task
        # needs no edit here. Added in fix round 1 (Minor 8): the restriction was prose in
        # src/scenes/README.md while every other rule in this stack is machine-checked.
        "scenes-model-stack-outside-context",
        (
            "src/scenes/**",
            "src/scenes/menu/context.cpp",
            "src/scenes/menu/bindings.cpp",
        ),
        "include",
        r"^model/model_stack\.h$",
        "2026-08-26-ui-scenes-menus-design.md",
    ),
    (
        # Mirrors ui-includes-outside/shell-includes-outside for the gui/hid split specifically:
        # scenes-includes-outside above already forbids gui/ (except gui/l10n/) and hid/ wholesale,
        # but names every other allowed directory explicitly rather than the two forbidden ones --
        # this row restates the gui/hid half on its own so a reviewer scanning by rule name finds
        # it without re-deriving the negation. The (include, *excludes) glob tuple already exists
        # (`rule_covers`, check_layer_rules.py:214-226, as ui-widgets-read-input-directly uses at
        # :98) -- no checker change needed; this row needs no `except` field of its own.
        "scenes-includes-gui",
        "src/scenes/**",
        "include",
        r"^(gui/(?!l10n/)|hid/)",
        "2026-08-26-ui-scenes-menus-design.md",
    ),
    (
        # Narrower still than scenes-model-stack-outside-context: `modulation/automation/auto_param.h`
        # carries the same "reach into the model stack machinery directly" risk as `model_stack.h`
        # itself, so it is confined to the same two files.
        "scenes-model-stack",
        (
            "src/scenes/**",
            "src/scenes/menu/bindings.cpp",
            "src/scenes/menu/context.cpp",
        ),
        "include",
        r"^(model/model_stack\.h|modulation/automation/auto_param\.h)",
        "2026-08-26-ui-scenes-menus-design.md",
    ),
    (
        # Spec E §3 rule 4: `currentSong` is confined to the two files that resolve a MenuContext
        # (context.cpp, bindings.cpp) and even there only inside doc comments describing what was
        # *not* ported -- the live code takes the Song& from MenuContext. Baseline is the counted
        # number of currentSong uses in context.cpp after Task 3: 0 (shrink-only).
        "scenes-currentSong",
        "src/scenes/**",
        "symbol",
        r"\bcurrentSong\b",
        "2026-08-26-ui-scenes-menus-design.md",
    ),
    (
        # Menus draw only through widgets: no direct OledFrame primitive calls from the scene
        # layer. The real risk is `f.<primitive>`, not `canvas.` -- OledFrame exposes
        # pixel/hline/rect/glyphs directly (ui/oled/frame.h:165-269).
        #
        # A ratchet against drift, not a proof: this only sees the two local names it lists, so a
        # primitive reached through some other local (`region.pixel(...)`, say) slips past. Same
        # caveat as ui-widgets-read-input-directly -- the row catches the habit, not every spelling.
        "scenes-no-frame-primitives",
        "src/scenes/menu/**",
        "symbol",
        r"\b(canvas|f)\.(pixel|hline|vline|line|rect|invert|clear|glyph)",
        "2026-08-26-ui-scenes-menus-design.md",
    ),
    (
        # A THIRD shell-no-clock row: the checker matches one glob per row, which is why this rule
        # is already two rows (src/shell/** above, src/ui/** above). The scenes layer must be
        # exactly as clock-free as the shell and toolkit layers it sits on top of.
        "shell-no-clock",
        "src/scenes/**",
        "symbol",
        r"\bdeluge_clock_|\bstd::chrono\b|\bclock\(\)|\btime\(",
        "2026-08-26-ui-test-harness-design.md",
    ),
]
# Matches both #include "path" and #include <path>, capturing only the path (the delimiter
# itself is stripped either way). Angle-bracket project includes (e.g. #include <hid/buttons.h>)
# are real layer violations and must count the same as the quoted spelling -- mirrors the
# include regex in scripts/check_gfx_layer.py, which treats angle-bracket project includes
# the same way.
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[>"]')


def glob_match(rel: str, pattern: str) -> bool:
    # expand {a,b}
    m = re.search(r"\{([^}]*)\}", pattern)
    if m:
        return any(
            glob_match(rel, pattern[: m.start()] + alt + pattern[m.end() :])
            for alt in m.group(1).split(",")
        )
    return fnmatch(rel, pattern)


def rule_covers(rel: str, frm) -> bool:
    """Does rule scope `frm` cover file `rel`?

    A plain string is one include glob. A tuple is `(include, *excludes)`: the file must match
    the include glob and none of the excludes -- for a rule that holds across a directory
    except for the file that implements the thing being ratcheted.
    """
    if isinstance(frm, tuple):
        include, *excludes = frm
        return glob_match(rel, include) and not any(
            glob_match(rel, e) for e in excludes
        )
    return glob_match(rel, frm)


def count_file(path: Path) -> dict[str, int]:
    rel = str(path.relative_to(REPO))
    counts: dict[str, int] = {}
    text = path.read_text(encoding="utf-8", errors="replace").splitlines()
    for name, frm, kind, rx, _ in RULES:
        if not rule_covers(rel, frm):
            continue
        r = re.compile(rx)
        n = 0
        for line in text:
            if kind == "include":
                m = INCLUDE_RE.match(line)
                if m and r.search(m.group(1)):
                    n += 1
            elif r.search(line) and not line.lstrip().startswith("//"):
                n += 1
        if n:
            counts[name] = n
    return counts


def current() -> dict[tuple[str, str], int]:
    out = {}
    for p in sorted((REPO / "src").rglob("*")):
        if p.suffix in SUFFIXES and p.is_file():
            for rule, n in count_file(p).items():
                out[(str(p.relative_to(REPO)), rule)] = n
    return out


def load_baseline() -> dict[tuple[str, str], int]:
    out = {}
    if BASELINE.exists():
        for line in BASELINE.read_text().splitlines():
            if line and not line.startswith("#"):
                f, r, n = line.split("\t")
                out[(f, r)] = int(n)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--update", action="store_true")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    cur = current()
    if a.list:
        for (f, r), n in sorted(cur.items()):
            print(f"{f}\t{r}\t{n}")
        return 0
    if a.update:
        BASELINE.write_text(
            "# Layer-rule baseline: file<TAB>rule<TAB>count. May only shrink.\n"
            + "".join(f"{f}\t{r}\t{n}\n" for (f, r), n in sorted(cur.items()))
        )
        print(f"wrote {len(cur)} entries")
        return 0
    base = load_baseline()
    bad = 0
    for key, n in cur.items():
        if n > base.get(key, 0):
            f, r = key
            spec = next(s for name, *_, s in RULES if name == r)
            print(
                f"{f}: rule '{r}' count {n} > baseline {base.get(key, 0)} (see docs/superpowers/specs/{spec})"
            )
            bad += 1
    if bad:
        print(
            f"{bad} layer-rule regression(s). Remove the new dependency, or (only for an agreed move) run --update.",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
