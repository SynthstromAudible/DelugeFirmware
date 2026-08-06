#! /usr/bin/env python3
"""Annotate every top level class declaration in a source file with PLACE_SDRAM_DATA.

This task was written specifically to annotate src/deluge/gui/ui/menus.cpp,
where the vast majority of top level statements are declarations of global
menu objects (instances of classes such as Submenu, HorizontalMenu,
arpeggiator::Mode, etc). Those objects are currently placed in the default
data section, but since they are read-only-ish and rarely change once
constructed, they are good candidates for being placed in external SDRAM via
the PLACE_SDRAM_DATA macro (see src/definitions.h) to save precious internal
RAM.

The script performs a purely mechanical, syntax based transformation: it does
NOT change any existing code, it only *prepends* the PLACE_SDRAM_DATA macro to
statements which look like a top level variable declaration whose type is a
class (i.e. "Type[::Type...] name[::name] {...};" or "Type name;"), and which
is not already annotated with PLACE_SDRAM_DATA.

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
    r"^(?:const\s+|constexpr\s+|static\s+|inline\s+)*"  # optional qualifiers
    r"[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*"  # type name
    r"(?:<[^;{}]*>)?"  # optional template args, e.g. std::array<MenuItem*, 3>
    r"(?:\s*[*&])*"  # optional pointer/reference sigils, e.g. "const MenuItem*"
    r"\s+"
    r"[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*"  # variable name
    r"(?:\s*\[[^\]]*\])*"  # optional array dimensions, e.g. "[kDisplayHeight]"
    r"\s*[{;=]"
)

# Matches a leading block comment on the same line as the code that follows
# it, e.g. "/* comment */ Submenu fooMenu{...};".
_LEADING_BLOCK_COMMENT_RE = re.compile(r"^(/\*.*?\*/\s*)+")


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
    in_line_comment = False
    in_block_comment = False
    in_string = False
    in_char = False

    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""

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

        first_word_match = re.match(r"[A-Za-z_][A-Za-z0-9_]*", code_after_comment)
        first_word = first_word_match.group(0) if first_word_match else ""

        # Preprocessor lines, or lines starting with a skip keyword, are left
        # untouched.
        if stripped.startswith("#") or first_word in _SKIP_KEYWORDS:
            result.append(chunk)
            continue

        if _DECL_RE.match(code_after_comment):
            lines[idx] = (
                f"{leading_ws}{comment_prefix}PLACE_SDRAM_DATA {code_after_comment}"
            )
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
