"""The audible window's report: what moved between a reference render set and a candidate, for Kate to read and accept.

Sections, in order: the threshold flips; the changed cases ranked by null-test level; each changed case's moved
parameters and a corpus-wide table per parameter; voice lifetime changes; cases missing from either set; and the cases
identical in both audio and dumps.
"""

import dataclasses
import json
import math
from collections import defaultdict
from pathlib import Path

PARAMS_PER_CASE = 12


def _db(x: float) -> str:
    return "silent" if x == -math.inf else f"{x:.1f}"


def render(results: list, ref: str, new: str) -> str:
    changed = [r for r in results if not r.identical and r.missing is None]
    out = [
        "# Audible window comparison",
        "",
        (
            f"Reference `{ref}` against candidate `{new}`: {len(results)} renders, {len(changed)} changed, "
            f"{sum(r.identical for r in results)} identical, {sum(r.missing is not None for r in results)} missing."
        ),
        "",
        "## Threshold flips",
        "",
    ]
    flips = [(r.name, f) for r in results for f in r.flips]
    if flips:
        out += [
            "| render | predicate | sound | voice | column | first time | blocks | ref | new |",
            "|---|---|---|---|---|---|---|---|---|",
        ]
        for name, f in flips:
            out.append(
                f"| {name} | {f['predicate']} | {f.get('sound', '')} | {f.get('voice', '')} | {f.get('column', '')} | "
                f"{f.get('first_time', '')} | {f.get('blocks', '')} | {f['ref']} | {f['new']} |"
            )
    else:
        out.append("None.")
    out += ["", "## Changed renders, by null-test level", ""]
    if changed:
        out += [
            "| render | null dB | onset | RMS Δ dB | peak Δ dB | envelope r | centroid % | wave r | params moved |",
            "|---|---|---|---|---|---|---|---|---|",
        ]
        for r in sorted(
            changed, key=lambda r: -r.null_db if r.null_db != -math.inf else math.inf
        ):
            a = r.audio or {}
            out.append(
                f"| {r.name} | {_db(r.null_db)} | {a.get('onset', '')} | {a.get('rms_diff', 0):.2f} | "
                f"{a.get('peak_diff', 0):.2f} | {a.get('env', 0):.4f} | {a.get('centroid', 0):.2f} | "
                f"{a.get('wave', 0):.4f} | {len(r.params)} |"
            )
    else:
        out.append("None.")
    out += ["", "## Moved parameters", ""]
    by_param: dict = defaultdict(lambda: [0, 0])
    for r in changed:
        if not r.params:
            continue
        out += [
            f"### {r.name}",
            "",
            "| sound | scope | voice | param | max LSB | first time | blocks |",
            "|---|---|---|---|---|---|---|",
        ]
        for p in sorted(r.params, key=lambda p: -p["max_lsb"])[:PARAMS_PER_CASE]:
            out.append(
                f"| {p['sound']} | {'voice' if p['scope'] == 0 else 'sound'} | {p['voice']} | {p['param']} | "
                f"{p['max_lsb']} | {p['first_time']} | {p['differing']} |"
            )
        if len(r.params) > PARAMS_PER_CASE:
            out.append(f"| … {len(r.params) - PARAMS_PER_CASE} more | | | | | | |")
        out.append("")
        for p in r.params:
            entry = by_param[(p["scope"], p["param"])]
            entry[0] += 1
            entry[1] = max(entry[1], p["max_lsb"])
    if by_param:
        out += [
            "### Across every render",
            "",
            "| scope | param | moved in | max LSB |",
            "|---|---|---|---|",
        ]
        for (scope, param), (count, lsb) in sorted(
            by_param.items(), key=lambda kv: -kv[1][0]
        ):
            out.append(
                f"| {'voice' if scope == 0 else 'sound'} | {param} | {count} | {lsb} |"
            )
    else:
        out.append("None.")
    out += ["", "## Voice lifetimes", ""]
    lives = [(r.name, v) for r in results for v in r.lifetimes]
    if lives:
        out += [
            "| render | sound | voice | ref first–last | new first–last |",
            "|---|---|---|---|---|",
        ]
        for name, v in lives:
            out.append(
                f"| {name} | {v['sound']} | {v['voice']} | {v['ref_first']}–{v['ref_last']} | "
                f"{v['new_first']}–{v['new_last']} |"
            )
    else:
        out.append("None.")
    missing = [r for r in results if r.missing is not None]
    out += ["", "## Missing", ""]
    out += [f"- {r.name}: no render in the {r.missing} set" for r in missing] or [
        "None."
    ]
    out += ["", "## Identical", ""]
    identical = [r.name for r in results if r.identical]
    out.append(", ".join(identical) if identical else "None.")
    return "\n".join(out) + "\n"


def write(results: list, ref: str, new: str, path_md: Path, path_json: Path) -> None:
    Path(path_md).write_text(render(results, ref, new))
    Path(path_json).write_text(
        json.dumps(
            {
                "reference": ref,
                "candidate": new,
                "renders": [dataclasses.asdict(r) for r in results],
            },
            indent=1,
        )
        + "\n"
    )
