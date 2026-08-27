#!/usr/bin/env python3
"""Rewrite model-side display-> presentation calls to notify::notice*. Idempotent.

  display->displayError(e);                                  -> deluge::notify::notice(e);
  display->consoleText("lit");                               -> notice("lit", Notice::Kind::Console);
  display->consoleText(deluge::l10n::get(String::X));        -> notice(String::X, Notice::Kind::Console);
  display->displayPopup("lit"[, ...]);                       -> notice("lit"[, Kind::Text, uint8_t(PopupType::T)]);
  display->displayPopup(deluge::l10n::get(String::X)[, ...]);-> notice(String::X[, ...]);
  display->displayPopup(<expr>[, ...]);   (computed text)    -> notice(std::string_view(<expr>)[, ...]);
  display->cancelPopup();                                    -> notice_cancel_popup();

PopupType is preserved when the call passed one as the last argument. A call that passed no
PopupType is rewritten without one: Notice::popup_type == 0 means "unspecified" and the drain maps
that to PopupType::GENERAL (PopupType::NONE is 0, GENERAL is 1), so spelling GENERAL out here would
be wrong for every default call.

Not rewritten (left for a human):
  * anything under src/deluge/gui/ or src/deluge/hid/, and instrument_clip_minder.cpp;
  * calls inside comments (// and block comments);
  * the working animation -- displayLoadingAnimationText(), removeWorkingAnimation() and
    removeLoadingAnimation(). These are deliberately NOT in the mapping. The animation is armed
    to be visible *during* a long blocking load, and the drain only runs once per
    deluge_app_tick(), so a queued "show" and a queued "clear" bracketing a synchronous load pop
    back-to-back in the same drain pass and the animation is never painted. Converting them
    needs the shell to tick during sd_busy (spec D), not a codemod;
  * displayPopup(..., 0, ...) -- numFlashes 0 means "persistent" on OLED
    (OLED::displayPopup passes `!numFlashes` as `persistent`); that maps to
    Notice::Severity::Warn, which the raisers do not expose yet, so those sites are reported
    instead of being silently downgraded to a flashing popup.
"""

import re
import sys
from pathlib import Path

NS = "deluge::notify::"
L10N = re.compile(
    r"(?:deluge::)?l10n::get\(\s*"
    r"(deluge::l10n::String::\w+|l10n::String::\w+|String::\w+)\s*\)"
)
INCLUDE = '#include "util/notify.h"\n'

CODE, COMMENT, LITERAL = 0, 1, 2


def classify(src: str) -> bytearray:
    """Per-character map: CODE / COMMENT / LITERAL, so the scanners ignore parens, quotes and
    `display->` occurrences that live inside a comment or a string."""
    out = bytearray(len(src))
    i, n, state = 0, len(src), None
    while i < n:
        c = src[i]
        nxt = src[i + 1] if i + 1 < n else ""
        if state is None:
            if c == "/" and nxt == "/":
                state = "line"
                out[i] = out[i + 1] = COMMENT
                i += 2
                continue
            if c == "/" and nxt == "*":
                state = "block"
                out[i] = out[i + 1] = COMMENT
                i += 2
                continue
            if c in "\"'":
                state = "str" if c == '"' else "chr"
                out[i] = LITERAL
            i += 1
        elif state == "line":
            out[i] = COMMENT
            if c == "\n":
                state = None
            i += 1
        elif state == "block":
            out[i] = COMMENT
            if c == "*" and nxt == "/":
                out[i + 1] = COMMENT
                state = None
                i += 2
                continue
            i += 1
        else:  # inside a string or char literal
            out[i] = LITERAL
            if c == "\\" and i + 1 < n:
                out[i + 1] = LITERAL
                i += 2
                continue
            if (state == "str" and c == '"') or (state == "chr" and c == "'"):
                state = None
            i += 1
    return out


# `(?<![\w:])` keeps `my_display->` / `Foo::display->` out; the name is anchored by the `(`.
CALL = re.compile(
    r"(?<![\w:])display->(displayError|consoleText|displayPopup|cancelPopup)\s*\("
)


def call_extent(src: str, cls: bytearray, open_idx: int):
    """(close_paren_idx, semicolon_idx) for a balanced call that is a whole statement, else None."""
    depth, i, n = 0, open_idx, len(src)
    while i < n:
        if cls[i] == CODE:
            ch = src[i]
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    j = i + 1
                    while j < n and src[j] in " \t\r\n":
                        j += 1
                    return (i, j) if j < n and src[j] == ";" else None
        i += 1
    return None


def split_top_level(s: str):
    depth, cur, out = 0, "", []
    for ch in s:
        if ch in "([{":
            depth += 1
        if ch in ")]}":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    out.append(cur)
    return out


def popup_type(args: str) -> str:
    m = re.search(r"PopupType::(\w+)\s*$", args.strip())
    return (
        f", {NS}Notice::Kind::Text, static_cast<uint8_t>(PopupType::{m.group(1)})"
        if m
        else ""
    )


def rewrite_popup(args: str):
    parts = [a.strip() for a in split_top_level(args)]
    # numFlashes == 0 is "persistent" on OLED -- Severity::Warn territory, not a plain notice.
    if len(parts) > 1 and parts[1] == "0":
        return None
    text = parts[0]
    ptype = popup_type(args)
    lm = L10N.fullmatch(text)
    if lm:
        return f"{NS}notice({lm.group(1)}{ptype});"
    if text.startswith('"'):
        return f"{NS}notice({text}{ptype});"
    return f"{NS}notice(std::string_view({text}){ptype});"


def rewrite_console(args: str):
    arg = args.strip()
    lm = L10N.fullmatch(arg)
    inner = lm.group(1) if lm else arg
    return f"{NS}notice({inner}, {NS}Notice::Kind::Console);"


RULES = {
    "displayError": lambda args: f"{NS}notice({args.strip()});",
    "consoleText": rewrite_console,
    "displayPopup": rewrite_popup,
    "cancelPopup": lambda args: (
        f"{NS}notice_cancel_popup();" if not args.strip() else None
    ),
    # The working animation is deliberately absent -- see the module docstring.
}


def deferred_here(src: str, idx: int) -> bool:
    """A `// NOTIFY-DEFERRED: ...` on the call's own line, or anywhere in the run of `//` comment
    lines directly above it, pins the site as it is."""
    line_end = src.find("\n", idx)
    line_start = src.rfind("\n", 0, idx) + 1
    if "NOTIFY-DEFERRED" in src[line_start : line_end if line_end != -1 else len(src)]:
        return True
    above = src[:line_start].split("\n")[:-1]
    for line in reversed(above):
        if not line.strip().startswith("//"):
            return False
        if "NOTIFY-DEFERRED" in line:
            return True
    return False


def rewrite(path: Path, skipped: list) -> bool:
    src = path.read_text()
    cls = classify(src)
    edits = []
    for m in CALL.finditer(src):
        if cls[m.start()] != CODE:
            continue
        if deferred_here(src, m.start()):
            continue
        extent = call_extent(src, cls, m.end() - 1)
        if extent is None:
            skipped.append(
                (path, src[: m.start()].count("\n") + 1, "not a plain statement")
            )
            continue
        close, semi = extent
        new = RULES[m.group(1)](src[m.end() : close])
        if new is None:
            skipped.append(
                (
                    path,
                    src[: m.start()].count("\n") + 1,
                    f"{m.group(1)}: unhandled argument shape",
                )
            )
            continue
        edits.append((m.start(), semi + 1, new))
    if not edits:
        return False
    out = src
    for start, end, new in reversed(edits):
        out = out[:start] + new + out[end:]
    if INCLUDE not in out:
        out = re.sub(
            r"(^\s*#include[^\n]*\n)", r"\1" + INCLUDE, out, count=1, flags=re.MULTILINE
        )
    path.write_text(out)
    return True


def main(argv):
    roots = [Path(a) for a in argv[1:]] or [Path("src/deluge")]
    files = []
    for root in roots:
        files.extend(sorted(root.rglob("*")) if root.is_dir() else [root])
    changed, skipped = 0, []
    for p in files:
        if p.suffix not in {".cpp", ".h", ".hpp"}:
            continue
        s = str(p)
        if "/gui/" in s or "/hid/" in s or "/bsp/" in s:
            continue
        if p.name == "instrument_clip_minder.cpp":
            continue
        if rewrite(p, skipped):
            changed += 1
            print(f"rewrote {p}")
    for path, line, why in skipped:
        print(f"SKIPPED {path}:{line}: {why}")
    print(f"{changed} file(s) changed, {len(skipped)} site(s) left for a human")


if __name__ == "__main__":
    main(sys.argv)
