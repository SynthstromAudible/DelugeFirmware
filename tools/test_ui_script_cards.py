#!/usr/bin/env python3
"""Tests for ui_script_cards: `python3 -m unittest test_ui_script_cards` (from tools/).

Gate ui-script-cards:
    a UI script's `# card:` header lines are read before its first step and resolve to a repository directory, a
    repository card manifest or a content card under DELUGE_CONTENT_CARDS, and a leg's label tells a content card
    from a repository one of the same name
"""

import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import ui_script_cards as cards


def script(text: str) -> Path:
    path = Path(tempfile.mkdtemp()) / "s01-x.txt"
    path.write_text(text)
    return path


class HeaderTest(unittest.TestCase):
    def test_cards_are_the_header_lines_in_order(self):
        s = script(
            "# What it does.\n# card: tests/fixtures/card\n#\n# card: content/bare.img\n100 snapshot boot\n"
        )
        self.assertEqual(cards.cards(s), ["tests/fixtures/card", "content/bare.img"])

    def test_a_card_line_after_the_first_step_is_a_comment(self):
        s = script(
            "# card: tests/fixtures/card\n100 snapshot boot\n# card: content/bare.img\n"
        )
        self.assertEqual(cards.cards(s), ["tests/fixtures/card"])

    def test_a_script_without_a_header_has_no_cards(self):
        self.assertEqual(cards.cards(script("100 snapshot boot\n")), [])


class ResolveTest(unittest.TestCase):
    ROOT = Path("/repo")

    def test_a_repository_card_is_under_the_repository(self):
        self.assertEqual(
            cards.resolve("tests/fixtures/card-x.card", self.ROOT),
            Path("/repo/tests/fixtures/card-x.card"),
        )

    def test_a_content_card_is_under_the_content_directory(self):
        with mock.patch.dict(os.environ, {"DELUGE_CONTENT_CARDS": "/cards"}):
            self.assertEqual(
                cards.resolve("content/bare.img", self.ROOT), Path("/cards/bare.img")
            )
        with mock.patch.dict(os.environ, {"DELUGE_CONTENT_CARDS": ""}):
            self.assertEqual(
                cards.resolve("content/shellmenu", self.ROOT),
                Path("/repo/.superpowers/emu-card/shellmenu"),
            )

    def test_labels(self):
        self.assertEqual(cards.label("tests/fixtures/card"), "card")
        self.assertEqual(
            cards.label("tests/fixtures/card-old-shell-menu.card"),
            "card-old-shell-menu",
        )
        self.assertEqual(cards.label("content/card.img"), "content-card")
        self.assertEqual(cards.label("content/shellmenu"), "content-shellmenu")

    def test_a_directory_is_its_own_tree(self):
        self.assertEqual(
            cards.tree(Path("/repo/tests/fixtures/card"), Path("/unused")),
            Path("/repo/tests/fixtures/card"),
        )


class NamesTest(unittest.TestCase):
    def test_numbered_names(self):
        for good in (
            "p01-views.txt",
            "sm05-vertical-menus.txt",
            "c32-midi-linked.txt",
            "g11-weird.txt",
            "a02-menu.txt",
        ):
            self.assertRegex(good, cards.NUMBERED)
        for bad in (
            "menu.txt",
            "midi_follow.txt",
            "p1-x.txt",
            "P01-x.txt",
            "p01_x.txt",
        ):
            self.assertNotRegex(bad, cards.NUMBERED)


if __name__ == "__main__":
    unittest.main()
