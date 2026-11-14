# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Tests for check_spec_comparisons.py: what a site is, which kind it is, and which sites a commit adds.

Gate spec-comparisons-selftest:
    a site is an expect() of a comparison, `&&` or `||` judged by to_be_true() or to_be_false(), named by its kind;
    comments, string and character literals, digit separators, template argument lists and bracketed comparisons
    make no site; a commit adds a site only beyond the copies HEAD holds, so an unchanged or moved site passes and a
    second copy or a flipped verdict is refused
"""

from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import check_spec_comparisons as csc
from check_spec_comparisons import added, blank, sites

ONE_SITE = "void f() {\n\texpect(a == b).to_be_true();\n}\n"


def kinds(body: str) -> list[tuple[str, str]]:
    return [(s.kind, s.verdict) for s in sites("tests/x_spec.cpp", body)]


class SiteTest(unittest.TestCase):
    def test_names_the_kind_of_each_comparison(self):
        cases = {
            "expect(a == b).to_be_true();": ("eq", "true"),
            "expect(a != b).to_be_true();": ("ne", "true"),
            "expect(p == nullptr).to_be_true();": ("null", "true"),
            "expect(nullptr != p).to_be_false();": ("null", "false"),
            "expect(a < b).to_be_true();": ("order", "true"),
            "expect(a >= b).to_be_true();": ("order", "true"),
            "expect(a<b).to_be_true();": ("order", "true"),
            "expect(std::fabs(a - b) < 1e-6).to_be_true();": ("tolerance", "true"),
            'expect(s.find("x") != std::string::npos).to_be_true();': ("text", "true"),
            "expect(std::memcmp(a, b, n) == 0).to_be_true();": ("bytes", "true"),
            'expect(std::strcmp(a, "b") == 0).to_be_true();': ("cstring", "true"),
            "expect(a == b && c).to_be_true();": ("and", "true"),
            "expect(a || b).to_be_false();": ("or", "false"),
            "expect(a == b == c).to_be_true();": ("chain", "true"),
            "expect((x << 2) == 8).to_be_true();": ("eq", "true"),
            "expect(static_cast<int*>(p) == q).to_be_true();": ("eq", "true"),
            "expect(std::get<1>(t) == 3).to_be_true();": ("eq", "true"),
            "expect(c == '<').to_be_true();": ("eq", "true"),
            'expect(s == "a && b || c").to_be_true();': ("eq", "true"),
            'expect(s == R"(x < y)").to_be_true();': ("eq", "true"),
            "expect(1'000 == n).to_be_true();": ("eq", "true"),
            "expect(a <=> b == 0).to_be_true();": ("eq", "true"),
            "expect(fabsf(a - b) < t).to_be_true();": ("tolerance", "true"),
            "expect(t > std::fabs(a - b)).to_be_true();": ("tolerance", "true"),
            "expect(std::abs(a - b) <= t).to_be_true();": ("tolerance", "true"),
            "expect(s.rfind(x) != std::string::npos).to_be_true();": ("text", "true"),
            "expect(p == NULL).to_be_true();": ("null", "true"),
            "expect(!a == b).to_be_true();": ("eq", "true"),
            "expect((a) == (b)).to_be_true();": ("eq", "true"),
        }
        for body, want in cases.items():
            with self.subTest(body=body):
                self.assertEqual(kinds(body), [want])

    def test_passes_what_is_not_a_judged_comparison(self):
        for body in (
            "expect(ready).to_be_true();",
            "expect(a).to_equal(b);",
            "expect(a == b).to_equal(true);",
            "expect(std::get<1>(t).empty()).to_be_true();",
            "expect(std::is_same_v<A, B>).to_be_true();",
            "expect(std::vector<std::vector<int>>{}.empty()).to_be_true();",
            "expect(f(1'000)).to_be_true();",
            'expect(f("a == b")).to_be_true();',
            'expect(f(R"(a < b)")).to_be_true();',
            "expect(f(a == b)).to_be_true();",
            "expect(x << 2).to_be_true();",
            "expect(p->ok).to_be_true();",
            "// expect(a == b).to_be_true();",
            "/* expect(a == b).to_be_true(); */",
            "my_expect(a == b).to_be_true();",
            "expect(!p).to_be_true();",
            "expect(not ready).to_be_false();",
            "expect(!f(a == b)).to_be_true();",
            "expect((ready)).to_be_true();",
            "expect(f(a == b)).not_().to_be_true();",
        ):
            with self.subTest(body=body):
                self.assertEqual(kinds(body), [])

    def test_peels_negations_and_wrapping_parentheses_flipping_the_verdict(self):
        cases = {
            "expect((a == b)).to_be_true();": ("eq", "true"),
            "expect(!(a == b)).to_be_true();": ("eq", "false"),
            "expect(not (a == b)).to_be_true();": ("eq", "false"),
            "expect(not(a == b)).to_be_false();": ("eq", "true"),
            "expect(!!(a == b)).to_be_true();": ("eq", "true"),
            "expect( ! ( (a < b) ) ).to_be_false();": ("order", "true"),
            "expect(a == b).not_().to_be_false();": ("eq", "true"),
            "expect(a == b) . not_ ( ) . to_be_true();": ("eq", "false"),
            "expect(!(a == b)).not_().to_be_true();": ("eq", "true"),
            "expect(!(w.first == kLed && w.second)).to_be_true();": ("and", "false"),
        }
        for body, want in cases.items():
            with self.subTest(body=body):
                self.assertEqual(kinds(body), [want])

    def test_a_peeled_site_names_the_inner_comparison_and_reports_what_is_written(self):
        text = "expect(!(a == b)).not_().to_be_false();"
        (s,) = sites("tests/x_spec.cpp", text)
        self.assertEqual(text[s.arg[0] : s.arg[1]], "a == b")
        self.assertEqual((s.text, s.verdict), ("a == b", "false"))
        self.assertEqual(s.end, len(text) - 1)
        self.assertIn("expect(!(a == b)).not_().to_be_false()", csc.describe(s))

    def test_a_site_spans_lines_and_is_reported_at_its_first(self):
        found = sites(
            "tests/x_spec.cpp",
            "int a;\n\texpect(a ==\n\t       b)\n\t    .to_be_true();\n",
        )
        self.assertEqual(
            [(s.line, s.kind, s.text) for s in found], [(2, "eq", "a == b")]
        )

    def test_the_message_is_not_part_of_the_argument(self):
        found = sites("tests/x_spec.cpp", 'expect(a == b).to_be_true("a < b && c");')
        self.assertEqual([(s.kind, s.text) for s in found], [("eq", "a == b")])

    def test_blanking_keeps_every_offset(self):
        text = 'x = \'a\'; // c\n/* d\n e */ s = R"(q)" "w\\"";\n'
        self.assertEqual(len(blank(text)), len(text))
        self.assertEqual(blank(text).count("\n"), text.count("\n"))


class AddedTest(unittest.TestCase):
    def test_an_unchanged_or_moved_site_is_not_added(self):
        moved = "void g() {}\nvoid f() {\n\n\texpect(a  ==  b).to_be_true();\n}\n"
        self.assertEqual(
            added({"tests/a_spec.cpp": ONE_SITE}, {"tests/a_spec.cpp": moved}), []
        )

    def test_a_site_moved_to_another_file_is_not_added(self):
        new = {"tests/a_spec.cpp": "", "tests/b_spec.cpp": ONE_SITE}
        self.assertEqual(added({"tests/a_spec.cpp": ONE_SITE}, new), [])

    def test_a_second_copy_is_added(self):
        new = ONE_SITE + "void h() {\n\texpect(a == b).to_be_true();\n}\n"
        got = added({"tests/a_spec.cpp": ONE_SITE}, {"tests/a_spec.cpp": new})
        self.assertEqual([(s.path, s.line) for s in got], [("tests/a_spec.cpp", 5)])

    def test_a_new_file_adds_its_every_site(self):
        self.assertEqual(len(added({}, {"tests/n_spec.cpp": ONE_SITE})), 1)

    def test_rewriting_a_site_away_adds_nothing(self):
        new = ONE_SITE.replace("expect(a == b).to_be_true()", "expect(a).to_equal(b)")
        self.assertEqual(
            added({"tests/a_spec.cpp": ONE_SITE}, {"tests/a_spec.cpp": new}), []
        )

    def test_a_rewrapped_respaced_or_recommented_site_is_not_added(self):
        for old, new in (
            (
                "expect(f(a, b) == c).to_be_true();",
                "expect(f(\n\t\ta, b) == c).to_be_true();",
            ),
            ("expect(a==b).to_be_true();", "expect(a == b).to_be_true();"),
            (
                "expect(a == /* x */ b).to_be_true();",
                "expect(a == /* y */ b).to_be_true();",
            ),
            ("expect(a == b).to_be_true();", "expect(a == b) // why\n\t.to_be_true();"),
        ):
            with self.subTest(new=new):
                self.assertEqual(
                    added({"tests/a_spec.cpp": old}, {"tests/a_spec.cpp": new}), []
                )

    def test_spacing_that_joins_words_or_sits_in_a_literal_is_a_new_site(self):
        for old, new in (
            (
                "expect(sizeof a == b).to_be_true();",
                "expect(sizeofa == b).to_be_true();",
            ),
            ('expect(s == "a b").to_be_true();', 'expect(s == "ab").to_be_true();'),
        ):
            with self.subTest(new=new):
                got = added({"tests/a_spec.cpp": old}, {"tests/a_spec.cpp": new})
                self.assertEqual(len(got), 1)

    def test_flipping_the_verdict_is_a_new_site(self):
        new = ONE_SITE.replace("to_be_true", "to_be_false")
        self.assertEqual(
            len(added({"tests/a_spec.cpp": ONE_SITE}, {"tests/a_spec.cpp": new})), 1
        )


class StagedTest(unittest.TestCase):
    def test_compares_the_index_with_head_across_files(self):
        diff = "M\0tests/a_spec.cpp\0A\0tests/b_spec.cpp\0D\0tests/c_spec.cpp\0M\0tests/notes.md\0"
        blobs = {
            "HEAD:tests/a_spec.cpp": ONE_SITE,
            ":tests/a_spec.cpp": "",
            ":tests/b_spec.cpp": ONE_SITE + ONE_SITE,
            "HEAD:tests/c_spec.cpp": "",
        }
        with (
            mock.patch.object(csc, "_git", return_value=diff),
            mock.patch.object(
                csc, "_show", side_effect=lambda spec: blobs.get(spec, "")
            ),
            mock.patch.object(csc, "_merging", return_value=False),
        ):
            got = csc.staged_sites()
        self.assertEqual([(s.path, s.line) for s in got], [("tests/b_spec.cpp", 5)])


class RepoTest(unittest.TestCase):
    """The staged and tree modes against a real repository in a temporary directory."""

    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
        env.update(GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM="1")
        patcher = mock.patch.dict(os.environ, env, clear=True)
        patcher.start()
        self.addCleanup(patcher.stop)
        repo = mock.patch.object(csc, "REPO", self.root)
        repo.start()
        self.addCleanup(repo.stop)
        self.git("init", "-q", "-b", "main")
        self.git("config", "user.name", "spec")
        self.git("config", "user.email", "spec@example.invalid")
        self.git("config", "commit.gpgsign", "false")
        self.write("tests/a_spec.cpp", "void f() {}\n")
        self.commit("base")

    def git(self, *args: str) -> str:
        return subprocess.run(
            ["git", *args], cwd=self.root, capture_output=True, text=True, check=True
        ).stdout

    def write(self, path: str, text: str) -> None:
        (self.root / path).parent.mkdir(parents=True, exist_ok=True)
        (self.root / path).write_text(text)
        self.git("add", "--", path)

    def commit(self, subject: str) -> None:
        self.git("commit", "-q", "--no-verify", "-m", subject)

    def test_a_merge_budgets_the_sites_of_the_merged_branch(self):
        self.git("checkout", "-q", "-b", "other")
        self.write("tests/b_spec.cpp", ONE_SITE)
        self.write("tests/a_spec.cpp", ONE_SITE.replace("a == b", "c == d"))
        self.commit("other")
        self.git("checkout", "-q", "main")
        self.write("tests/z_spec.cpp", "void z() {}\n")
        self.commit("main")
        self.git("merge", "-q", "--no-ff", "--no-commit", "other")
        self.assertEqual(csc.staged_sites(), [])
        self.write("tests/b_spec.cpp", ONE_SITE + ONE_SITE)
        got = csc.staged_sites()
        self.assertEqual([(s.path, s.line) for s in got], [("tests/b_spec.cpp", 5)])

    def test_without_a_merge_the_same_sites_are_added(self):
        self.write("tests/b_spec.cpp", ONE_SITE)
        self.assertEqual([s.path for s in csc.staged_sites()], ["tests/b_spec.cpp"])

    def test_paths_with_spaces_and_non_ascii_characters_are_read(self):
        paths = ["tests/with space_spec.cpp", "tests/\u00e9t\u00e9_spec.cpp"]
        for path in paths:
            self.write(path, ONE_SITE)
        self.assertEqual(sorted(s.path for s in csc.staged_sites()), sorted(paths))
        self.commit("sites")
        self.assertEqual(
            sorted(s.path for s in csc.tree_sites(("tests",))), sorted(paths)
        )


if __name__ == "__main__":
    unittest.main()
