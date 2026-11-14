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


class TempTest(unittest.TestCase):
    def script(self, text: str) -> Path:
        d = tempfile.TemporaryDirectory()
        self.addCleanup(d.cleanup)
        path = Path(d.name) / "s01-x.txt"
        path.write_text(text)
        return path


class HeaderTest(TempTest):
    def test_cards_are_the_header_lines_in_order(self):
        s = self.script(
            "# What it does.\n# card: tests/fixtures/card\n#\n# card: content/bare.img\n100 snapshot boot\n"
        )
        self.assertEqual(cards.cards(s), ["tests/fixtures/card", "content/bare.img"])

    def test_a_card_line_after_the_first_step_is_a_comment(self):
        s = self.script(
            "# card: tests/fixtures/card\n100 snapshot boot\n# card: content/bare.img\n"
        )
        self.assertEqual(cards.cards(s), ["tests/fixtures/card"])

    def test_a_script_without_a_header_has_no_cards(self):
        self.assertEqual(cards.cards(self.script("100 snapshot boot\n")), [])

    def test_blank_lines_and_a_trailing_space_stay_in_the_header(self):
        s = self.script("# card: a \n\n# note\n# card: b\n1 x\n")
        self.assertEqual(cards.cards(s), ["a", "b"])


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
        with mock.patch.dict(os.environ):
            os.environ.pop("DELUGE_CONTENT_CARDS", None)
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


class ProblemsTest(unittest.TestCase):
    def setUp(self):
        d = tempfile.TemporaryDirectory()
        self.addCleanup(d.cleanup)
        self.repo = Path(d.name)
        for sub in ("app", "parity", "shell", "__pycache__"):
            (self.repo / "tests/ui-scripts" / sub).mkdir(parents=True)
        (self.repo / "tests/fixtures/card").mkdir(parents=True)
        (self.repo / "tests/fixtures/card-x.card").write_text("")

    def add(self, directory: str, name: str, *header: str):
        path = self.repo / "tests/ui-scripts" / directory / name
        path.write_text("".join(f"# card: {c}\n" for c in header) + "1 x\n")
        return path

    def problems(self) -> list[str]:
        return cards.problems(self.repo)

    def assertOne(self, fragment: str):
        found = self.problems()
        self.assertEqual(len(found), 1, found)
        self.assertIn(fragment, found[0])

    def test_a_clean_repository_has_no_problems(self):
        self.add("app", "a01-x.txt", "tests/fixtures/card")
        self.add(
            "parity",
            "p01-x.txt",
            "tests/fixtures/card",
            "tests/fixtures/card-x.card",
            "content/bare.img",
        )
        self.assertEqual(self.problems(), [])

    def test_a_script_naming_the_directory_and_the_manifest_is_clean(self):
        self.add(
            "parity", "p01-x.txt", "tests/fixtures/card", "tests/fixtures/card-x.card"
        )
        self.assertEqual(self.problems(), [])

    def test_an_unnumbered_name(self):
        self.add("parity", "menu.txt", "tests/fixtures/card")
        self.assertOne("not named")

    def test_no_header(self):
        (self.repo / "tests/ui-scripts/app/a01-x.txt").write_text("1 x\n")
        self.assertOne("no `# card:` header")

    def test_the_gate_runs_no_content_card(self):
        self.add("app", "a01-x.txt", "content/bare.img")
        self.assertOne("the ui-scripts gate runs in CI")

    def test_a_content_card_is_one_name(self):
        for card in ("content/a/b", "content/"):
            with self.subTest(card=card):
                self.add("parity", "p01-x.txt", card)
                self.assertOne("a content card is content/<name>")

    def test_a_missing_card(self):
        self.add("parity", "p02-x.txt", "tests/fixtures/nope")
        self.assertOne("no such directory or card manifest")

    def test_a_malformed_card_line(self):
        path = self.repo / "tests/ui-scripts/parity/p01-x.txt"
        for line in (
            "#card: a",
            "# Card: a",
            "# card:  a",
            "# card: a b",
            "  # card: a",
        ):
            with self.subTest(line=line):
                path.write_text(f"# card: tests/fixtures/card\n{line}\n1 x\n")
                self.assertOne("malformed `# card:` line")

    def test_a_card_named_twice(self):
        self.add("parity", "p01-x.txt", "tests/fixtures/card", "tests/fixtures/card")
        self.assertOne("named twice")

    def test_two_cards_with_one_label(self):
        self.add("parity", "p01-x.txt", "content/bare", "content/bare.img")
        self.assertOne("one label for several cards")

    def test_a_repository_card_stays_under_tests(self):
        outside = self.repo / "elsewhere"
        outside.mkdir()
        for card in (str(outside), "tests/../elsewhere", "tests/..", "."):
            with self.subTest(card=card):
                self.add("parity", "p01-x.txt", card)
                self.assertOne("a repository card is a path under tests/")

    def test_an_unknown_script_directory(self):
        (self.repo / "tests/ui-scripts/new").mkdir()
        self.assertOne("not a known script directory")


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
