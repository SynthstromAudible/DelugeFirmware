#! /usr/bin/env python3
"""Annotate every top level variable declaration in a source file with the
appropriate PLACE_SDRAM_BSS/PLACE_SDRAM_DATA/PLACE_SDRAM_RODATA macro.

This task was written specifically to annotate src/deluge/gui/ui/menus.cpp,
where the vast majority of top level statements are declarations of global
menu objects (instances of classes such as Submenu, HorizontalMenu,
arpeggiator::Mode, etc). Those objects are currently placed in the default
data/bss sections, but since they are read-only-ish and rarely change once
constructed, they are good candidates for being placed in external SDRAM
(see src/definitions.h) to save precious internal RAM.

The script performs a purely mechanical, syntax based transformation: it does
NOT change any existing code, it only *prepends* the appropriate PLACE_SDRAM_*
macro to statements which look like a top level variable declaration whose
type is a class (i.e. "Type[::Type...] name[::name] {...};" or "Type name;"),
using this rule to pick the macro:
  - const/constexpr/constinit-qualified objects (not counting pointers/
    references, since e.g. "const MenuItem*" only const-qualifies the
    pointee) go to PLACE_SDRAM_RODATA, since they are genuinely read-only.
  - array declarations (std::array<...> or a plain C array) go to
    PLACE_SDRAM_DATA, since a plain aggregate array's initializer data is
    only copied into RAM at startup for the .data section; placing it in
    .bss would silently zero it out.
  - everything else defaults to PLACE_SDRAM_BSS.
Declarations already annotated with the wrong macro get corrected; already
correctly annotated declarations are left untouched. Declarations nested
inside a namespace block are still annotated; only the "namespace ... {"
line itself (and namespace alias declarations) are left untouched.

Usage: ./dbt annotate_sdram <path/to/file.cpp> [more files...]
"""

import argparse
import os
import re
import sys
import unittest

import util

# Keywords that, if the declaration starts with them, mean the statement is
# NOT a plain class-typed variable declaration and must be left untouched.
_SKIP_KEYWORDS = {
    "using",
    "namespace",
    "void",
    "extern",
    "class",
    "struct",
    "enum",
    "template",
    "typedef",
    "static_assert",
    "return",
    "PLACE_SDRAM_DATA",
    "PLACE_SDRAM_RODATA",
    "public",
    "private",
    "protected",
    "if",
    "for",
    "while",
    "switch",
    "do",
}

# A declaration candidate looks like:
#   [const] Type[::Type...][<Targs>][*|&...] name[::name...][[dim]...] {  -> aggregate/brace init
#   [const] Type[::Type...][<Targs>][*|&...] name[::name...][[dim]...] =  -> copy init
#   [const] Type[::Type...][<Targs>][*|&...] name[::name...][[dim]...];   -> default init
_DECL_RE = re.compile(
    r"^((?:const\s+|constexpr\s+|constinit\s+|static\s+|inline\s+)*)"  # optional qualifiers
    r"[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*"  # type name
    r"(?:<[^;{}]*>)?"  # optional template args, e.g. std::array<MenuItem*, 3>
    r"((?:\s*[*&])*)"  # optional pointer/reference sigils, e.g. "const MenuItem*"
    r"\s+"
    r"[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*"  # variable name
    r"(?:\s*\[[^\]]*\])*"  # optional array dimensions, e.g. "[kDisplayHeight]"
    r"\s*[{;=]"
)

# Matches a leading block comment on the same line as the code that follows
# it, e.g. "/* comment */ Submenu fooMenu{...};".
_LEADING_BLOCK_COMMENT_RE = re.compile(r"^(/\*.*?\*/\s*)+")

# Matches an existing PLACE_SDRAM_BSS/PLACE_SDRAM_DATA/PLACE_SDRAM_RODATA
# annotation prefix, so that already-annotated declarations can be inspected
# (and corrected if the wrong macro was used) instead of being blindly
# skipped.
_EXISTING_MACRO_RE = re.compile(
    r"^(PLACE_SDRAM_BSS|PLACE_SDRAM_DATA|PLACE_SDRAM_RODATA)\s+"
)


def _is_array_decl(decl_text: str) -> bool:
    """Return True if *decl_text* (a string matched by _DECL_RE) declares an
    array, i.e. a std::array<...> or a plain C array with [dim] following
    the variable name. Array declarations must live in PLACE_SDRAM_DATA
    (not PLACE_SDRAM_BSS) since, unlike class-typed objects whose
    constructors run at startup regardless of section, a plain aggregate
    array's initializer data is only copied into RAM at startup for the
    .data section; placing it in .bss would silently zero it out."""
    return "std::array<" in decl_text or "[" in decl_text


def _is_const_qualified_object(decl_match: "re.Match") -> bool:
    """Return True if *decl_match* (a match of _DECL_RE) declares an object
    that is itself const/constexpr/constinit (as opposed to e.g. a mutable
    pointer to const data, such as "const MenuItem* foo"). Such objects are
    genuinely read-only and must live in PLACE_SDRAM_RODATA rather than
    PLACE_SDRAM_DATA/PLACE_SDRAM_BSS."""
    qualifiers = decl_match.group(1) or ""
    pointer_sigils = decl_match.group(2) or ""
    if pointer_sigils.strip():
        # A qualifier before the type only const-qualifies the pointee, not
        # the pointer/reference variable itself.
        return False
    return bool(re.search(r"\b(?:const|constexpr|constinit)\b", qualifiers))


def _determine_macro(decl_match: "re.Match") -> str:
    """Return the PLACE_SDRAM_* macro that should annotate the declaration
    matched by *decl_match*."""
    if _is_const_qualified_object(decl_match):
        return "PLACE_SDRAM_RODATA"
    if _is_array_decl(decl_match.group(0)):
        return "PLACE_SDRAM_DATA"
    return "PLACE_SDRAM_BSS"


def _first_code_line_index(chunk_lines: list[str]) -> int | None:
    """Return the index of the first line in *chunk_lines* that is not blank
    and not a pure line comment. Returns None if no such line exists."""
    for i, line in enumerate(chunk_lines):
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.startswith("//"):
            continue
        return i
    return None


def _split_top_level_statements(text: str) -> list[str]:
    """Split *text* into a list of chunks, where each chunk is either a top
    level statement (terminated by a top level ';' or, for brace blocks not
    followed by ';', terminated right after the closing '}') or a leading
    piece of text (comments/blank lines/preprocessor lines) glued to the
    following statement."""
    chunks = []
    depth = 0
    i = 0
    n = len(text)
    start = 0
    line_start = 0
    in_line_comment = False
    in_block_comment = False
    in_string = False
    in_char = False

    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""

        if c == "\n" and not (
            in_line_comment or in_block_comment or in_string or in_char
        ):
            # A preprocessor line (e.g. "#include ...") does not end with a
            # ';' or a brace, so without this it would stay glued to
            # whatever top level construct follows it (e.g. a namespace
            # block), preventing that construct from ever being recognized.
            if depth == 0:
                current_line = text[line_start:i]
                if current_line.strip().startswith("#"):
                    chunks.append(text[start : i + 1])
                    start = i + 1
            line_start = i + 1
            i += 1
            continue

        if in_line_comment:
            if c == "\n":
                in_line_comment = False
            i += 1
            continue
        if in_block_comment:
            if c == "*" and nxt == "/":
                in_block_comment = False
                i += 2
                continue
            i += 1
            continue
        if in_string:
            if c == "\\":
                i += 2
                continue
            if c == '"':
                in_string = False
            i += 1
            continue
        if in_char:
            if c == "\\":
                i += 2
                continue
            if c == "'":
                in_char = False
            i += 1
            continue

        if c == "/" and nxt == "/":
            in_line_comment = True
            i += 2
            continue
        if c == "/" and nxt == "*":
            in_block_comment = True
            i += 2
            continue
        if c == '"':
            in_string = True
            i += 1
            continue
        if c == "'":
            in_char = True
            i += 1
            continue

        if c in "({[":
            depth += 1
            i += 1
            continue
        if c in ")}]":
            was_brace_close = c == "}"
            depth -= 1
            i += 1
            if depth == 0 and was_brace_close:
                # Peek ahead: if the next non-whitespace char is ';', let the
                # ';' branch below close the chunk (keeps trailing ';' with
                # the chunk). Otherwise, close the chunk right here.
                j = i
                while j < n and text[j] in " \t\r\n":
                    j += 1
                if j >= n or text[j] != ";":
                    chunks.append(text[start:i])
                    start = i
            continue

        if c == ";" and depth == 0:
            i += 1
            chunks.append(text[start:i])
            start = i
            continue

        i += 1

    if start < n:
        chunks.append(text[start:n])

    return chunks


def annotate_text(text: str) -> tuple[str, int]:
    """Annotate every top level class declaration in *text*. Returns the new
    text and the number of declarations annotated."""
    chunks = _split_top_level_statements(text)
    result = []
    annotated_count = 0

    for chunk in chunks:
        lines = chunk.splitlines(keepends=True)
        idx = _first_code_line_index(lines)
        if idx is None:
            result.append(chunk)
            continue

        code_line = lines[idx]
        stripped = code_line.lstrip()
        leading_ws = code_line[: len(code_line) - len(stripped)]

        # A leading same-line block comment (e.g. "/* note */ Type foo;")
        # should not prevent the declaration after it from being recognized,
        # and the annotation must be inserted after the comment, not before.
        comment_match = _LEADING_BLOCK_COMMENT_RE.match(stripped)
        comment_prefix = comment_match.group(0) if comment_match else ""
        code_after_comment = stripped[len(comment_prefix) :]

        # Declarations that are already annotated are inspected instead of
        # being blindly skipped: if the wrong macro was used (e.g. a
        # PLACE_SDRAM_BSS array, or a const/constexpr/constinit object that
        # should live in PLACE_SDRAM_RODATA), the annotation is corrected;
        # everything else already-annotated is left as-is.
        existing_macro_match = _EXISTING_MACRO_RE.match(code_after_comment)
        if existing_macro_match:
            existing_macro = existing_macro_match.group(1)
            rest = code_after_comment[existing_macro_match.end() :]
            rest_decl_match = _DECL_RE.match(rest)
            if rest_decl_match:
                correct_macro = _determine_macro(rest_decl_match)
                if correct_macro != existing_macro:
                    lines[idx] = f"{leading_ws}{comment_prefix}{correct_macro} {rest}"
                    annotated_count += 1
            result.append("".join(lines))
            continue

        first_word_match = re.match(r"[A-Za-z_][A-Za-z0-9_]*", code_after_comment)
        first_word = first_word_match.group(0) if first_word_match else ""

        # A namespace block (as opposed to a namespace alias declaration
        # like "namespace foo = bar;", which has no '{') only has its
        # opening "namespace ... {" line skipped; everything declared
        # inside the braces is still a top level declaration from the
        # annotation script's point of view and must be recursed into.
        if first_word == "namespace" and "{" in chunk:
            brace_start = chunk.index("{")
            brace_end = chunk.rfind("}")
            if brace_end > brace_start:
                inner = chunk[brace_start + 1 : brace_end]
                new_inner, inner_count = annotate_text(inner)
                annotated_count += inner_count
                result.append(chunk[: brace_start + 1] + new_inner + chunk[brace_end:])
                continue

        # Preprocessor lines, or lines starting with a skip keyword, are left
        # untouched.
        if stripped.startswith("#") or first_word in _SKIP_KEYWORDS:
            result.append(chunk)
            continue

        decl_match = _DECL_RE.match(code_after_comment)
        if decl_match:
            macro = _determine_macro(decl_match)
            lines[idx] = f"{leading_ws}{comment_prefix}{macro} {code_after_comment}"
            annotated_count += 1

        result.append("".join(lines))

    return "".join(result), annotated_count


def annotate_file(path: str) -> int:
    with open(path, "r") as f:
        text = f.read()

    new_text, count = annotate_text(text)

    if count:
        with open(path, "w") as f:
            f.write(new_text)

    return count


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="annotate_sdram",
        description="Annotate every top level class declaration in the given "
        "file(s) with PLACE_SDRAM_DATA. Makes no other code changes.",
    )
    parser.group = "Development"
    parser.add_argument(
        "files",
        nargs="*",
        default=["src/deluge/gui/ui/menus.cpp"],
        help="Path(s) to the file(s) to annotate (default: "
        "src/deluge/gui/ui/menus.cpp)",
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="Run the annotate_sdram unit test suite instead of annotating any files.",
    )
    return parser


def run_self_test() -> int:
    this_dir = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, this_dir)

    loader = unittest.TestLoader()
    suite = loader.discover(
        this_dir, pattern="test_annotate_sdram.py", top_level_dir=this_dir
    )
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


def main(argv=None) -> int:
    (args, _unknown_args) = argparser().parse_known_args(argv)

    if args.self_test:
        return run_self_test()

    os.chdir(util.get_git_root())

    total = 0
    for file_path in args.files:
        count = annotate_file(file_path)
        print(f"{file_path}: annotated {count} declaration(s)")
        total += count

    print(f"Total: annotated {total} declaration(s)")
    return 0


if __name__ == "__main__":
    main()
