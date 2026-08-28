#!/usr/bin/env python3
"""Rewrite model-side UI redraw calls to notify::raise*. Idempotent. Run from repo root.

  uiNeedsRendering(X, 0xFFFFFFFF, 0)        -> deluge::notify::raise(deluge::notify::Dirty::GridMain);
  uiNeedsRendering(X, 0, 0xFFFFFFFF)        -> raise(Dirty::GridSidebar)
  uiNeedsRendering(X) / (X, 0xFFFFFFFF, 0xFFFFFFFF) / (X, 0x00000000, 0xFFFFFFFF)
                                            -> raise(GridMain); raise(GridSidebar)  (both / sidebar)
  renderUIsForOled();                       -> raise(Dirty::Oled);
  getRootUI()->notifyPlaybackBegun();  getRootUI()->playbackEnded();   -> raise(Dirty::PlaybackState);
  getRootUI()->sampleNeedsReRendering(s);   -> raise(Dirty::SampleReady);
  getRootUI()->noteRowChanged(clip, row);   -> clip->notify_content_changed(); raise(Dirty::NoteRows);
  arrangerView.notifyActiveClipChangedOnOutput(o); -> raise(Dirty::OutputChanged);

CAUTION: notifyPlaybackBegun()/playbackEnded()/notifyActiveClipChangedOnOutput() are NOT pure
redraw requests -- their RootUI/ArrangerView overrides also mutate state (audition
reassessment, ArrangerView::mustRedrawTickSquares + endAudition, auto-scroll-on-stop, the held
row's mod-controllable target). Converting a call site to a bare raise() only stays correct
because gui/ui/notify_drain.cpp's drain_notifications() separately re-dispatches those hooks
itself whenever it sees the matching Dirty bit (PlaybackState / OutputChanged) -- see the
"Legacy PlaybackHandler::.../Legacy Arrangement playback..." comments there. If that drain-side
dispatch is ever removed, these three rules must not be applied blindly again without restoring
it, or the hooks' state-mutating side effects silently disappear.

instrument_clip_minder.cpp is a UI mixin slated to move wholesale later and is left untouched
here even though it isn't under gui/ or hid/ -- its remaining uiNeedsRendering()/renderUIsForOled()
calls are expected and not converted by this pass.

Line-based: a line whose first non-space characters are "//" is left untouched (so a comment
mentioning one of these calls isn't rewritten into code-shaped text). Each pattern below is
anchored with a negative lookbehind so it can't match as a suffix of a longer identifier or a
qualified name (e.g. ``Foo::renderUIsForOled()``).
"""

import re
import sys
from pathlib import Path

RAISE = "deluge::notify::raise(deluge::notify::Dirty::{})"
# (?<![\w:]) before each call's leading identifier: refuses to match as a suffix of a longer
# name (e.g. "myuiNeedsRendering") or after a scope/member-access operator that would make it
# part of a different, unrelated qualified name.
NB = r"(?<![\w:])"
RULES = [
    (
        re.compile(NB + r"uiNeedsRendering\([^,()]+,\s*0xFFFFFFFF,\s*0\)\s*;"),
        RAISE.format("GridMain") + ";",
    ),
    (
        re.compile(NB + r"uiNeedsRendering\([^,()]+,\s*0(?:x0+)?,\s*0xFFFFFFFF\)\s*;"),
        RAISE.format("GridSidebar") + ";",
    ),
    (
        re.compile(
            NB + r"uiNeedsRendering\([^,()]+(?:,\s*0xFFFFFFFF,\s*0xFFFFFFFF)?\)\s*;"
        ),
        RAISE.format("GridMain") + "; " + RAISE.format("GridSidebar") + ";",
    ),
    (re.compile(NB + r"renderUIsForOled\(\)\s*;"), RAISE.format("Oled") + ";"),
    (
        re.compile(
            NB + r"getRootUI\(\)->(?:notifyPlaybackBegun|playbackEnded)\(\)\s*;"
        ),
        RAISE.format("PlaybackState") + ";",
    ),
    (
        re.compile(NB + r"getRootUI\(\)->sampleNeedsReRendering\([^;]*\)\s*;"),
        RAISE.format("SampleReady") + ";",
    ),
    (
        re.compile(
            NB
            + r"getRootUI\(\)->noteRowChanged\(\s*([A-Za-z_][\w>\-.]*)\s*,[^;]*\)\s*;"
        ),
        r"\1->notify_content_changed(); " + RAISE.format("NoteRows") + ";",
    ),
    (
        re.compile(NB + r"arrangerView\.notifyActiveClipChangedOnOutput\([^;]*\)\s*;"),
        RAISE.format("OutputChanged") + ";",
    ),
]
INCLUDE = '#include "util/notify.h"\n'


def rewrite(path: Path) -> bool:
    src = path.read_text()
    lines = src.splitlines(keepends=True)
    changed = False
    out_lines = []
    for line in lines:
        # Leave comment-only lines alone -- a "// ...renderUIsForOled();" explanatory comment
        # must not be rewritten into code-shaped text.
        if line.lstrip().startswith("//"):
            out_lines.append(line)
            continue
        new_line = line
        for rx, rep in RULES:
            new_line = rx.sub(rep, new_line)
        if new_line != line:
            changed = True
        out_lines.append(new_line)
    if not changed:
        return False
    out = "".join(out_lines)
    if INCLUDE not in out:
        # after the first #include line
        out = re.sub(
            r"(^\s*#include[^\n]*\n)", r"\1" + INCLUDE, out, count=1, flags=re.MULTILINE
        )
    path.write_text(out)
    return True


def main(argv):
    roots = [Path(a) for a in argv[1:]] or [Path("src/deluge")]
    changed = 0
    for root in roots:
        for p in root.rglob("*"):
            if (
                p.suffix not in {".cpp", ".h", ".hpp"}
                or "/gui/" in str(p)
                or "/hid/" in str(p)
            ):
                continue
            if p.name == "instrument_clip_minder.cpp":
                continue
            if rewrite(p):
                changed += 1
                print(f"rewrote {p}")
    print(f"{changed} file(s) changed")


if __name__ == "__main__":
    main(sys.argv)
