#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Checks that every fixed-point operation compiles to the instruction it names.

Disassembles fixed_point_probe.cpp's objects (one ARM, one Thumb-2, both at the shipping image's codegen flags) and
compares each probe's instructions, its final return excluded, with EXACT. An operation with no single A9 instruction
is listed in BUDGET instead: it must make no call and stay within that many instructions.

Usage:
    check_codegen.py OBJDUMP ARM_OBJECT THUMB_OBJECT
"""

from __future__ import annotations

import re
import subprocess
import sys

EXACT = {
    "probe_add_wrap": ["add"],
    "probe_sub_wrap": ["sub"],
    "probe_add_sat": ["qadd"],
    "probe_sub_sat": ["qsub"],
    "probe_neg_sat": ["mov", "qsub"],  # qsub from a zeroed register
    "probe_add_double_sat": ["qdadd"],
    "probe_sub_double_sat": ["qdsub"],
    "probe_mul": ["smmul"],
    "probe_mul_r": ["smmulr"],
    "probe_mla": ["smmla"],
    "probe_mla_r": ["smmlar"],
    "probe_mls": ["smmls"],
    "probe_mls_r": ["smmlsr"],
    "probe_mul_wide": ["smull"],
    "probe_mla_wide": ["smlal"],
    "probe_mul_uq": ["umull"],
    "probe_clamp_bits_24": ["ssat"],
    "probe_clamp_unsigned_16": ["usat"],
    "probe_clamp_after_shr": ["ssat"],  # one instruction: ssat #24, r0, asr #4
    "probe_to_q16_sat": ["ssat", "strh"],  # ssat #16, r0, asr #16, then the store
    "probe_as_sat_1": ["qadd"],
    "probe_as_wrap_1": ["lsl"],
    "probe_as_floor_3": ["asr"],
    "probe_as_round_3": [
        "ubfx",
        "add",
    ],  # floor plus the rounding bit, in one shifted-operand add
    "probe_mul_as_wrap": ["smmul", "lsl"],
    "probe_mul_r_as_wrap": ["smmulr", "lsl"],
    "probe_mul_q16": ["smulwb"],
    "probe_mul_top": ["smulwt"],
    "probe_mla_q16": ["smlawb"],
    "probe_dot": ["smuad"],
    "probe_dot_diff": ["smusd"],
    "probe_dot_acc": ["smlad"],
    "probe_dot_wide_acc": ["smlald"],
    "probe_q16x2_add_sat": ["qadd16"],
    "probe_q16x2_sub_sat": ["qsub16"],
    "probe_q16x2_halving_add": ["shadd16"],
    "probe_from_float_31": ["vcvt.s32.f32", "vmov"],
    "probe_to_float_31": ["vmov", "vcvt.f32.s32"],
}

BUDGET = {
    "probe_clamp_bits_runtime": 26,
    "probe_narrow_floor_sat": 16,
    "probe_mul_doubling_sat": 10,
    "probe_scale_shl_sat_3": 10,
    "probe_from_float_round_31": 34,
}

# Thumb-2's narrow flag-setting forms do the same arithmetic; ARM and Thumb then compare alike.
FLAG_SETTING_NARROW = re.compile(r"^(add|sub|rsb|mov|mvn|lsl|lsr|asr)s$")
CALLS = {"bl", "blx", "b"}


def normalise(mnemonic: str, thumb: bool) -> str:
    base = mnemonic.removesuffix(".w").removesuffix(".n")
    if thumb:
        match = FLAG_SETTING_NARROW.match(base)
        if match:
            base = match.group(1)
    return base


def probe_bodies(objdump: str, obj: str, thumb: bool) -> dict[str, list[str]]:
    out = subprocess.run(
        [objdump, "-d", "--no-show-raw-insn", obj],
        check=True,
        capture_output=True,
        text=True,
    )
    bodies: dict[str, list[str]] = {}
    current: list[str] | None = None
    for line in out.stdout.splitlines():
        header = re.match(r"^[0-9a-f]+ <(probe_\w+)>:$", line)
        if header:
            current = bodies.setdefault(header.group(1), [])
            continue
        insn = re.match(r"^\s+[0-9a-f]+:\s+([a-z][\w.]*)", line)
        if insn and current is not None:
            current.append(normalise(insn.group(1), thumb))
        elif not line.strip():
            current = None
    for name, body in bodies.items():
        # Drop the return and anything after it (literal pools, padding).
        if "bx" in body:
            bodies[name] = body[: len(body) - 1 - body[::-1].index("bx")]
    return bodies


def check(bodies: dict[str, list[str]], mode: str) -> list[str]:
    errors = []
    for name in sorted(set(EXACT) | set(BUDGET) | set(bodies)):
        body = bodies.get(name)
        if body is None:
            errors.append(f"[{mode}] {name}: missing from the probe")
        elif name in EXACT:
            if body != EXACT[name]:
                errors.append(f"[{mode}] {name}: expected {EXACT[name]}, got {body}")
        elif name in BUDGET:
            calls = [m for m in body if m in CALLS]
            if calls:
                errors.append(f"[{mode}] {name}: makes a call: {body}")
            elif len(body) > BUDGET[name]:
                errors.append(
                    f"[{mode}] {name}: {len(body)} instructions, budget {BUDGET[name]}: {body}"
                )
        else:
            errors.append(f"[{mode}] {name}: in the probe but not in EXACT or BUDGET")
    return errors


def main(argv: list[str]) -> int:
    if len(argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2
    objdump, arm_obj, thumb_obj = argv[1:]
    errors = check(probe_bodies(objdump, arm_obj, thumb=False), "arm")
    errors += check(probe_bodies(objdump, thumb_obj, thumb=True), "thumb")
    for error in errors:
        print(error)
    if not errors:
        print(
            f"fixed-point codegen: {len(EXACT)} exact, {len(BUDGET)} budgeted, ARM and Thumb-2: OK"
        )
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
