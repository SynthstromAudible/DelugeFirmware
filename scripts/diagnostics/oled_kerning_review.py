#!/usr/bin/env python3
"""Render OLED kerning specimens with the current firmware C++ text routines.

Requires Python 3 and a C++20 compiler (CXX, default clang++). No Python packages.
Extracts the unchanged drawString/drawChar/metrics/blitter implementations, links
actual fonts.c, and replaces only hardware-dependent class declarations. Optional
pair overrides are injected into the temporary translation unit, never firmware.
Usage: python3 scripts/diagnostics/oled_kerning_review.py --output /tmp/oled-review
"""

import argparse
import html
import json
import os
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def png(path, rows, scale=5):
    height, width = len(rows), len(rows[0])
    raw = b"".join(
        (b"\0" + bytes(235 if c == "1" else 15 for c in row for _ in range(scale)))
        * scale
        for row in rows
    )

    def chunk(kind, data):
        return (
            struct.pack("!I", len(data))
            + kind
            + data
            + struct.pack("!I", zlib.crc32(kind + data))
        )

    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(
            b"IHDR", struct.pack("!2I5B", width * scale, height * scale, 8, 0, 0, 0, 0)
        )
        + chunk(b"IDAT", zlib.compress(raw))
        + chunk(b"IEND", b"")
    )


def build(directory, source_path=None):
    source = (
        source_path or ROOT / "src/deluge/hid/display/oled_canvas/canvas.cpp"
    ).read_text()
    routines = source[
        source.index("void Canvas::drawString(std::string_view") : source.index(
            "void Canvas::drawStringCentred("
        )
    ]
    routines += source[
        source.index("#define DO_CHARACTER_SCALING") : source.index(
            "void Canvas::drawIcon("
        )
    ]
    hook = "\n\tif (text_height == overrideHeight && previous_char == overrideLeft && current_char == overrideRight) return overrideValue;\n"
    marker = "\t// loop through all configured kerning adjustment rules"
    assert routines.count(marker) == 1
    routines = routines.replace(marker, hook + marker)
    preamble = r"""
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>
#include "gui/fonts/fonts.h"
constexpr int OLED_MAIN_WIDTH_PIXELS = 128;
constexpr int OLED_MAIN_HEIGHT_PIXELS = 48;
constexpr int kTextSpacingX = 6;
int overrideHeight=-1, overrideLeft=0, overrideRight=0, overrideValue=0;
namespace { int32_t getPreviousCharSpacingAdjustmentInPixels(uint8_t,uint8_t,int32_t); }
class Canvas {
public:
uint8_t image_[6][128]{};
void drawString(std::string_view,int32_t,int32_t,int32_t,int32_t,int32_t=0,int32_t=128,bool=false);
void drawChar(uint8_t,int32_t,int32_t,int32_t,int32_t,int32_t=0,int32_t=128);
int32_t getCharIndex(uint8_t);
int32_t getCharWidthInPixels(uint8_t,int32_t);
int32_t getCharSpacingInPixels(uint8_t,int32_t,bool);
int32_t getStringWidthInPixels(char const*,int32_t);
void drawGraphicMultiLine(uint8_t const*,int32_t,int32_t,int32_t,int32_t,int32_t,bool=false);
};
"""
    main = r"""
int main(int argc, char** argv) {
 if(argc == 1) {
  for(const auto& r : kKerningRules) std::cout << int(r.text_height) << ' ' << int(r.previous_char) << ' ' << int(r.current_char) << ' ' << int(r.adjustment) << '\n';
  return 0;
 }
 if(argc != 6) return 2;
 int height=atoi(argv[1]);
 overrideHeight=height; overrideLeft=argv[2][0]; overrideRight=argv[2][1]; overrideValue=atoi(argv[3]);
 if(overrideLeft=='_') overrideHeight=-1;
 Canvas c;
 c.drawString(argv[4],0,0,6,height,atoi(argv[5]));
 std::cout << c.getStringWidthInPixels(argv[4],height) << '\n';
 for(int y=0;y<height+2;++y) {
  for(int x=0;x<128;++x) std::cout << ((c.image_[y/8][x]>>(y%8))&1);
  std::cout << '\n';
 }
}
"""
    cpp = directory / "render.cpp"
    cpp.write_text(preamble + routines + main)
    binary = directory / "render"
    subprocess.run(
        [
            os.environ.get("CXX", "clang++"),
            "-std=c++20",
            "-O1",
            "-I" + str(ROOT / "src/deluge"),
            str(cpp),
            "-x",
            "c++",
            str(ROOT / "src/deluge/gui/fonts/fonts.c"),
            "-o",
            str(binary),
        ],
        check=True,
    )
    return binary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("/tmp/oled-review"))
    parser.add_argument(
        "--baseline",
        type=Path,
        help="Pre-change canvas.cpp for actual before/after renders",
    )
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="oled-kerning-") as tmp:
        binary = build(Path(tmp))

        def render(text, height, pair="__", adjustment=0, scroll=0):
            lines = subprocess.check_output(
                [str(binary), str(height), pair, str(adjustment), text, str(scroll)],
                text=True,
            ).splitlines()
            return int(lines[0]), lines[1:]

        specimens = []
        pairs = [
            "AY",
            "YA",
            "AV",
            "VA",
            "AW",
            "WA",
            "AT",
            "TA",
            "FA",
            "PA",
            "LA",
            "LT",
            "LV",
            "LY",
            "TY",
            "YO",
            "OY",
            "NR",
            "ES",
            "-N",
            "W.",
            "F.",
            'L"',
        ]
        rules = [
            tuple(map(int, line.split()))
            for line in subprocess.check_output([str(binary)], text=True).splitlines()
        ]
        pairs += sorted(
            {chr(left) + chr(right) for _, left, right, _ in rules} - set(pairs)
        )
        for height in (5, 7, 10, 13, 20):
            for pair in pairs:
                variants = []
                configured = next(
                    (
                        a
                        for h, l, r, a in rules
                        if h == height and chr(l) + chr(r) == pair
                    ),
                    0,
                )
                for adjustment in dict.fromkeys((None, 0, -1, -2, configured)):
                    width, rows = render(
                        pair,
                        height,
                        pair if adjustment is not None else "__",
                        adjustment or 0,
                    )
                    name = f"{height}-{pairs.index(pair)}-{adjustment}.png"
                    png(
                        args.output / name, [row[: min(128, width + 3)] for row in rows]
                    )
                    variants.append((name, width, adjustment))
                specimens.append((height, pair, variants))
        words = []
        for word in [
            "PLAY DIRECTION",
            "PLAY",
            "LAYER",
            "DELAY",
            "WAVE",
            "SAVE",
            "ATTACK",
            "RATE",
            "TYPE",
            "FORWARD",
            "REVERSED",
            "PING-PONG",
        ]:
            variants = []
            for adjustment in (None, -1, -2):
                width, rows = render(
                    word, 10, "AY" if adjustment is not None else "__", adjustment or 0
                )
                name = f"word-{len(words)}-{adjustment}.png"
                png(args.output / name, rows)
                variants.append((name, width, adjustment))
            words.append((10, word, variants))
        # Basic invariants exercise the compiled production renderer/metrics.
        checks = 0
        for height in (5, 7, 8, 9, 10, 13, 20):
            for pair in pairs:
                width, rows = render(pair, height)
                assert (width, rows) == render(pair.lower(), height), (
                    height,
                    pair,
                    "case",
                )
                right = max(
                    (x + 1 for row in rows for x, c in enumerate(row) if c == "1"),
                    default=0,
                )
                assert right <= width, (height, pair, "width", right, width)
                checks += 2
        recommendations = [
            ("AY", -2),
            ("YA", -2),
            ("AV", -1),
            ("AW", -2),
            ("AT", -1),
            ("TA", -1),
            ("FA", -1),
            ("PA", -1),
        ]
        for pair, adjustment in recommendations:
            width, rows = render(pair, 10, pair, adjustment)
            assert (width, rows) == render(pair.lower(), 10, pair, adjustment)
            assert width == render(pair, 10, pair, 0)[0] + adjustment
            left_width, left_rows = render(pair[0], 10)
            _, right_rows = render(pair[1], 10)
            # Single-glyph width includes its 1px left bearing. Two-pixel
            # tracking makes the next origin left_width + 1 + adjustment.
            offset = left_width + 1 + adjustment
            left = [
                (x, y)
                for y, row in enumerate(left_rows)
                for x, c in enumerate(row)
                if c == "1"
            ]
            right = [
                (x + offset, y)
                for y, row in enumerate(right_rows)
                for x, c in enumerate(row)
                if c == "1"
            ]
            assert (
                min(max(abs(x - u), abs(y - v)) for x, y in left for u, v in right) >= 2
            ), pair
            checks += 3
        report = [
            '<!doctype html><meta charset="utf-8"><title>OLED kerning review</title><style>body{font:16px system-ui;background:#eee}td{padding:10px}img{image-rendering:pixelated;max-width:100%}table{border-collapse:collapse}tr{border-bottom:1px solid #aaa}</style><h1>OLED kerning review</h1><p>Actual firmware C++ text routines and font bitmaps; nearest-neighbor enlargement. Heights are API textHeight values (10 uses a 9-pixel bitmap). Overrides affect only the named pair. “Current” uses kKerningRules unchanged.</p>'
        ]
        report.append(
            "<p><b>Review:</b> For textHeight 10, recommend AY/YA = -2px, AV = -1px, AW = -2px. Conservative additional candidates: AT/TA/FA/PA = -1px. These preserve pixel separation. For 13/20px, inspect AY/YA/AV/AT/TA/LV/LY independently; do not scale the 10px table mechanically. Keep the monospaced 7–9px font unkerned. NR +1 at 10px and ES +1 at 13px deserve reconsideration; removing them restores the standard advance without touching glyphs. Values are optical judgments, not standardized numerical requirements.</p>"
        )
        for title, items in [
            ("Title words (AY override only)", words),
            ("Pair specimens", specimens),
        ]:
            report.append(f"<h2>{title}</h2><table>")
            for height, pair, variants in items:
                report.append(f"<tr><th>{height}px {html.escape(pair)}</th>")
                for name, width, adjustment in variants:
                    label = (
                        "Current"
                        if adjustment is None
                        else f"{adjustment:+d}px override"
                    )
                    report.append(
                        f'<td>{label}; width {width}<br><img src="{name}"></td>'
                    )
                report.append("</tr>")
            report.append("</table>")
        report.append(
            f"<p>{checks} case/width/candidate checks passed. This is a host simulation of the text path, not hardware emulation or an optical model of the OLED.</p>"
        )
        (args.output / "index.html").write_text("\n".join(report))
        (args.output / "specimens.json").write_text(
            json.dumps(
                {"pairs": specimens, "words": words, "checks": checks, "rules": rules},
                indent=2,
            )
        )
        if args.baseline:
            baseline_directory = Path(tmp) / "baseline"
            baseline_directory.mkdir()
            baseline_binary = build(baseline_directory, args.baseline)
            comparison(binary, baseline_binary, args.output)
        print(f"{args.output}/index.html ({checks} checks passed)")


def comparison(after_binary, before_binary, output):
    """Compare both actual tables without overrides, including unaffected pairs."""

    def render(binary, text, height):
        lines = subprocess.check_output(
            [str(binary), str(height), "__", "0", text, "0"], text=True
        ).splitlines()
        return int(lines[0]), lines[1:]

    changes = {
        (10, pair): adjustment
        for pair, adjustment in [
            ("AY", -2),
            ("YA", -2),
            ("AV", -1),
            ("AW", -2),
            ("AT", -1),
            ("TA", -1),
            ("FA", -1),
            ("PA", -1),
            ("NR", -1),
        ]
    }
    changes[13, "ES"] = -1
    checks = 0
    for height in (5, 7, 8, 9, 10, 13, 20):
        for left in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
            for right in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
                pair = left + right
                before_width, before_rows = render(before_binary, pair, height)
                after_width, after_rows = render(after_binary, pair, height)
                delta = changes.get((height, pair), 0)
                assert after_width - before_width == delta, (height, pair)
                if delta == 0:
                    assert before_rows == after_rows, (
                        height,
                        pair,
                        "unexpected pixel change",
                    )
                checks += 1

    samples = [
        (10, text)
        for text in [
            "PLAY DIRECTION",
            "LAYER",
            "DELAY",
            "SAVE",
            "DRAW",
            "ATTACK",
            "RATE",
            "FADE",
            "PATTERN",
            "UNROLL",
        ]
    ]
    samples += [(13, "PRESET")]
    samples += list(changes)
    report = [
        '<!doctype html><meta charset="utf-8"><title>OLED before and after</title>',
        "<style>body{font:16px system-ui}td,th{padding:12px;text-align:left}img{image-rendering:pixelated;max-width:100%}</style>",
        "<h1>OLED kerning: before and after</h1>",
        "<p>Both columns use compiled firmware text routines and actual font data, with no pair overrides. Images are enlarged 5× without smoothing.</p>",
        "<table><tr><th>Sample / textHeight</th><th>Before</th><th>After</th></tr>",
    ]
    measurements = []
    for index, (height, text) in enumerate(samples):
        report.append(f"<tr><th>{html.escape(text)} / {height}</th>")
        widths = []
        for label, binary in [("before", before_binary), ("after", after_binary)]:
            width, rows = render(binary, text, height)
            name = f"comparison-{index}-{label}.png"
            png(output / name, rows)
            widths.append(width)
            report.append(
                f'<td>{width} px<br><img src="{name}" alt="{label}: {html.escape(text)}"></td>'
            )
        report.append("</tr>")
        measurements.append(
            {"text": text, "height": height, "before": widths[0], "after": widths[1]}
        )
    report.append(
        f"</table><p>{checks} uppercase-pair comparisons passed across seven text heights. Only the ten intended pairs changed.</p>"
    )
    (output / "comparison.html").write_text("\n".join(report))
    (output / "comparison.json").write_text(json.dumps(measurements, indent=2))
    annotate_comparison(output, before_binary, after_binary)
    print(f"{output}/comparison.html ({checks} before/after pair checks passed)")


def annotate_comparison(output, before_binary, after_binary):
    """Label each specimen using the differences between the compiled rule tables."""

    def rules(binary):
        entries = [
            tuple(map(int, line.split()))
            for line in subprocess.check_output([str(binary)], text=True).splitlines()
        ]
        return {
            (height, chr(left) + chr(right)): adjustment
            for height, left, right, adjustment in entries
        }

    before, after = rules(before_binary), rules(after_binary)
    measurements = json.loads((output / "comparison.json").read_text())
    report = (output / "comparison.html").read_text()
    report = report.replace(
        "<th>Sample / textHeight</th><th>Before</th>",
        "<th>Sample / textHeight</th><th>Adjusted pairs (kerning offset)</th><th>Before</th>",
    )
    for sample in measurements:
        text, height = sample["text"], sample["height"]
        pairs = [text[index : index + 2].upper() for index in range(len(text) - 1)]
        adjustments = []
        labels = []
        for pair in dict.fromkeys(pairs):
            old, new = before.get((height, pair), 0), after.get((height, pair), 0)
            if old == new:
                continue
            count = pairs.count(pair)
            adjustments.append(
                {"pair": pair, "before": old, "after": new, "occurrences": count}
            )
            repeated = f" (×{count})" if count > 1 else ""
            labels.append(
                f"<code>{html.escape(pair)}</code>: {old:+d} → {new:+d} px{repeated}"
            )
        sample["adjusted_pairs"] = adjustments
        heading = f"<tr><th>{html.escape(text)} / {height}</th>"
        report = report.replace(
            heading, heading + "<td>" + ("<br>".join(labels) or "None") + "</td>"
        )
    (output / "comparison.html").write_text(report)
    (output / "comparison.json").write_text(json.dumps(measurements, indent=2))


if __name__ == "__main__":
    main()
