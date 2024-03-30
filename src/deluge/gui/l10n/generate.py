import json
import sys
import argparse


def process(input, output):
    input = json.loads(input.read())
    strings = input["strings"]

    output.write(
        """#include "definitions.h"
#include "gui/l10n/language.h"
#include "gui/l10n/strings.h"

namespace deluge::l10n::built_in {
using enum String;"""
    )

    type_name = input["type_name"]
    language_name = input["language_name"]
    output.write(f"PLACE_SDRAM_DATA Language {type_name}{{\n")
    output.write(f'    "{language_name}",\n')
    output.write("    {\n")

    prefix = "    " * 2
    for symbolic, value in input["strings"].items():
        output.write(f'{prefix}{{{symbolic}, "{value}"}},\n')

    output.write("    },\n")
    if "fallback" in input:
        fallback = input["fallback"]
        output.write(f"    &built_in::{fallback},\n")
    output.write(
        """};
} // namespace deluge::l10n::built_in"""
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input")
    parser.add_argument("output")

    args = parser.parse_args()

    with open(args.input) as inf:
        with open(args.output, "w") as outf:
            process(inf, outf)


if __name__ == "__main__":
    main()
