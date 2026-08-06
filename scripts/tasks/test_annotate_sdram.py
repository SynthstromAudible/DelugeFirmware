#! /usr/bin/env python3
"""Unit tests for task-annotate_sdram.py.

These tests exercise the mechanical parsing/annotation logic against a
variety of tricky inputs (comments, strings, nested braces, templates,
already-annotated lines, non-declaration statements, etc.) to make sure the
script never misparses the file it is run against.

Run directly with:
    python3 scripts/tasks/test_annotate_sdram.py

or via the dbt task's self-test flag:
    ./dbt annotate_sdram --self-test
"""

import importlib
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

annotate_sdram = importlib.import_module("task-annotate_sdram")


class AnnotateTextTests(unittest.TestCase):
    def assertAnnotated(self, source: str, expected: str, expected_count: int = 1):
        new_text, count = annotate_sdram.annotate_text(source)
        self.assertEqual(new_text, expected)
        self.assertEqual(count, expected_count)

    def test_simple_brace_init(self):
        self.assertAnnotated(
            "Submenu fooMenu{STRING_FOR_FOO};\n",
            "PLACE_SDRAM_DATA Submenu fooMenu{STRING_FOR_FOO};\n",
        )

    def test_simple_semicolon_declaration(self):
        self.assertAnnotated(
            "gate::Mode gateModeMenu;\n",
            "PLACE_SDRAM_DATA gate::Mode gateModeMenu;\n",
        )

    def test_copy_init_with_equals(self):
        self.assertAnnotated(
            "int foo = bar();\n",
            "PLACE_SDRAM_DATA int foo = bar();\n",
        )

    def test_qualified_type_and_name(self):
        self.assertAnnotated(
            "arpeggiator::OctaveModeToNoteMode arpeggiator::arpOctaveModeToNoteModeMenu{A, B};\n",
            "PLACE_SDRAM_DATA arpeggiator::OctaveModeToNoteMode arpeggiator::arpOctaveModeToNoteModeMenu{A, B};\n",
        )

    def test_multiline_declaration(self):
        source = "Submenu fooMenu{\n    STRING_FOR_FOO,\n    {&a, &b},\n};\n"
        expected = "PLACE_SDRAM_DATA Submenu fooMenu{\n    STRING_FOR_FOO,\n    {&a, &b},\n};\n"
        self.assertAnnotated(source, expected)

    def test_already_annotated_is_left_untouched(self):
        source = "PLACE_SDRAM_DATA Submenu fooMenu{STRING_FOR_FOO};\n"
        new_text, count = annotate_sdram.annotate_text(source)
        self.assertEqual(new_text, source)
        self.assertEqual(count, 0)

    def test_using_declaration_untouched(self):
        source = "using enum l10n::String;\n"
        new_text, count = annotate_sdram.annotate_text(source)
        self.assertEqual(new_text, source)
        self.assertEqual(count, 0)

    def test_namespace_alias_untouched(self):
        source = "namespace params = deluge::modulation::params;\n"
        new_text, count = annotate_sdram.annotate_text(source)
        self.assertEqual(new_text, source)
        self.assertEqual(count, 0)

    def test_function_definition_untouched(self):
        source = (
            "void setCvNumberForTitle(int32_t num) {\n"
            "    num++;\n"
            "    cvSubmenu.format(num);\n"
            "}\n"
        )
        new_text, count = annotate_sdram.annotate_text(source)
        self.assertEqual(new_text, source)
        self.assertEqual(count, 0)

    def test_preprocessor_include_untouched(self):
        source = '#include "gui/menu_item/voice/priority.h"\n'
        new_text, count = annotate_sdram.annotate_text(source)
        self.assertEqual(new_text, source)
        self.assertEqual(count, 0)

    def test_class_declaration_untouched(self):
        source = "class Foo {\n  int x;\n};\n"
        new_text, count = annotate_sdram.annotate_text(source)
        self.assertEqual(new_text, source)
        self.assertEqual(count, 0)

    def test_template_type_declaration(self):
        self.assertAnnotated(
            "std::array<MenuItem*, 3> fooArray{&a, &b, &c};\n",
            "PLACE_SDRAM_DATA std::array<MenuItem*, 3> fooArray{&a, &b, &c};\n",
        )

    def test_brace_inside_string_literal_does_not_confuse_depth(self):
        source = 'Submenu fooMenu{"{not a brace}"};\n'
        expected = 'PLACE_SDRAM_DATA Submenu fooMenu{"{not a brace}"};\n'
        self.assertAnnotated(source, expected)

    def test_semicolon_inside_string_literal_does_not_split_statement(self):
        source = 'Submenu fooMenu{"a;b"};\n'
        expected = 'PLACE_SDRAM_DATA Submenu fooMenu{"a;b"};\n'
        self.assertAnnotated(source, expected)

    def test_escaped_quote_inside_string_literal(self):
        source = 'Submenu fooMenu{"a\\"b"};\n'
        expected = 'PLACE_SDRAM_DATA Submenu fooMenu{"a\\"b"};\n'
        self.assertAnnotated(source, expected)

    def test_char_literal_brace_does_not_confuse_depth(self):
        source = "Submenu fooMenu{'{', 'a'};\n"
        expected = "PLACE_SDRAM_DATA Submenu fooMenu{'{', 'a'};\n"
        self.assertAnnotated(source, expected)

    def test_line_comment_before_declaration_is_kept(self):
        source = "// A comment\nSubmenu fooMenu{STRING_FOR_FOO};\n"
        expected = "// A comment\nPLACE_SDRAM_DATA Submenu fooMenu{STRING_FOR_FOO};\n"
        self.assertAnnotated(source, expected)

    def test_block_comment_containing_brace_is_ignored(self):
        source = "/* a { fake brace */ Submenu fooMenu{STRING_FOR_FOO};\n"
        expected = (
            "/* a { fake brace */ PLACE_SDRAM_DATA Submenu fooMenu{STRING_FOR_FOO};\n"
        )
        self.assertAnnotated(source, expected)

    def test_multiple_statements_each_annotated_once(self):
        source = "Submenu fooMenu{A};\nSubmenu barMenu{B};\n"
        expected = (
            "PLACE_SDRAM_DATA Submenu fooMenu{A};\n"
            "PLACE_SDRAM_DATA Submenu barMenu{B};\n"
        )
        self.assertAnnotated(source, expected, expected_count=2)

    def test_pointer_array_declaration(self):
        source = "const MenuItem* midiOrCVParamShortcuts[kDisplayHeight] = {\n    &a,\n    nullptr,\n};\n"
        expected = (
            "PLACE_SDRAM_DATA const MenuItem* midiOrCVParamShortcuts[kDisplayHeight] = {\n"
            "    &a,\n"
            "    nullptr,\n"
            "};\n"
        )
        self.assertAnnotated(source, expected)

    def test_extern_declaration_untouched(self):
        source = "extern Submenu fooMenu;\n"
        new_text, count = annotate_sdram.annotate_text(source)
        self.assertEqual(new_text, source)
        self.assertEqual(count, 0)

    def test_no_annotation_needed_returns_unchanged_text(self):
        source = (
            "using enum l10n::String;\nnamespace params = deluge::modulation::params;\n"
        )
        new_text, count = annotate_sdram.annotate_text(source)
        self.assertEqual(new_text, source)
        self.assertEqual(count, 0)

    def test_idempotent_on_already_fully_annotated_file(self):
        source = "PLACE_SDRAM_DATA Submenu fooMenu{A};\nPLACE_SDRAM_DATA gate::Mode gateModeMenu;\n"
        new_text, count = annotate_sdram.annotate_text(source)
        self.assertEqual(new_text, source)
        self.assertEqual(count, 0)


if __name__ == "__main__":
    unittest.main()
