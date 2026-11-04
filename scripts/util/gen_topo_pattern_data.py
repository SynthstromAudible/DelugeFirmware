#!/usr/bin/env python3
"""Generate topo_pattern_data.cpp from the original MI Grids resources.

Extracts the 25 drum pattern nodes (node_0..node_24, 96 bytes each: 3 banks x 32
steps) and the Euclidean bitmask LUT from the original Grids V1 firmware
(Mutable Instruments, GPLv3, Copyright 2011 Emilie Gillet), and writes them out
as a plain C++ data file for the Deluge "Topo Drums" engine.

Usage:
    python3 scripts/util/gen_topo_pattern_data.py <path-to-grids-repo>

The grids repo: https://github.com/pichenettes/eurorack (grids/ subdirectory)
"""

import re
import sys
from pathlib import Path

NODE_COUNT = 25
NODE_SIZE = 96
EUCLIDEAN_LUT_SIZE = 1024

# The 5x5 topographic map of node indices, from grids/pattern_generator.cc
DRUM_MAP = [
    [10, 8, 0, 9, 11],
    [15, 7, 13, 12, 6],
    [18, 14, 4, 5, 3],
    [23, 16, 21, 1, 2],
    [24, 19, 17, 20, 22],
]

HEADER = """\
/*
 * Pattern data for the "Topo Drums" generative drum engine.
 *
 * Data extracted from the original Mutable Instruments Grids firmware:
 * Copyright 2011/2012 Emilie Gillet (emilie.o.gillet@gmail.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * -----------------------------------------------------------------------------
 *
 * GENERATED FILE - DO NOT EDIT.
 * Regenerate with:
 *     python3 scripts/util/gen_topo_pattern_data.py <path-to-grids-repo>
 *
 * Source: grids/resources.cc of the MI eurorack firmware
 * (https://github.com/pichenettes/eurorack), node_0..node_24 and
 * lut_res_euclidean.
 */

#include "model/generative/topo/topo_pattern_data.h"

namespace deluge::model::topo {

// clang-format off

// 25 reference patterns. Each node holds kNodeSize = 96 bytes: 3 banks
// (kick, snare, hi-hat) x 32 steps, values 0-255 read as level/velocity.
const uint8_t kPatternNodes[kNumMapNodes][kNodeSize] = {
"""


def parse_array(text: str, pattern: str, name: str) -> list[int]:
    m = re.search(pattern, text, re.DOTALL)
    if not m:
        sys.exit(f"error: {name} not found in resources.cc")
    return [int(v) for v in re.findall(r"0[xX]?[0-9a-fA-F]+|\d+", m.group(1))]


def format_rows(values: list[int], per_line: int, indent: str) -> str:
    lines = []
    for i in range(0, len(values), per_line):
        chunk = ", ".join(str(v) for v in values[i : i + per_line])
        lines.append(f"{indent}{chunk},")
    return "\n".join(lines)


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    grids_dir = Path(sys.argv[1])
    resources_cc = grids_dir / "resources.cc"
    if not resources_cc.is_file():
        sys.exit(f"error: {resources_cc} not found - pass the path of the grids repo")

    text = resources_cc.read_text()

    # node_N tables
    nodes: dict[int, list[int]] = {}
    for m in re.finditer(
        r"const prog_uint8_t node_(\d+)\[\] PROGMEM = \{(.*?)\};", text, re.DOTALL
    ):
        values = [int(v) for v in re.findall(r"\d+", m.group(2))]
        if len(values) != NODE_SIZE:
            sys.exit(
                f"error: node_{m.group(1)} has {len(values)} values, expected {NODE_SIZE}"
            )
        nodes[int(m.group(1))] = values
    if len(nodes) != NODE_COUNT:
        sys.exit(f"error: found {len(nodes)} nodes, expected {NODE_COUNT}")

    # Euclidean LUT
    m = re.search(
        r"const prog_uint32_t lut_res_euclidean\[\] PROGMEM = \{(.*?)\};",
        text,
        re.DOTALL,
    )
    if not m:
        sys.exit("error: lut_res_euclidean not found")
    euclidean = [int(v) for v in re.findall(r"\d+", m.group(1))]
    if len(euclidean) != EUCLIDEAN_LUT_SIZE:
        sys.exit(
            f"error: lut_res_euclidean has {len(euclidean)} values, expected {EUCLIDEAN_LUT_SIZE}"
        )

    out = [HEADER]
    for i in range(NODE_COUNT):
        out.append(f"    /* node_{i} */\n    {{")
        out.append(format_rows(nodes[i], 12, "        "))
        out.append("    },")
    out.append("};\n")

    out.append("// Index into kPatternNodes for each point of the 5x5 topographic map.")
    out.append("const uint8_t kDrumMap[5][5] = {")
    for row in DRUM_MAP:
        out.append("    {" + ", ".join(str(v) for v in row) + "},")
    out.append("};\n")

    out.append(
        "// Euclidean (Bjorklund) bitmask patterns: [length - 1][density] -> 32-bit mask."
    )
    out.append(f"const uint32_t kEuclideanPatterns[{EUCLIDEAN_LUT_SIZE}] = {{")
    out.append(format_rows(euclidean, 8, "    "))
    out.append("};\n")

    out.append("// clang-format on\n")

    out.append("} // namespace deluge::model::topo")

    dest = (
        Path(__file__).resolve().parents[2]
        / "src/deluge/model/generative/topo/topo_pattern_data.cpp"
    )
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text("\n".join(out) + "\n")
    print(f"wrote {dest}")


if __name__ == "__main__":
    main()
