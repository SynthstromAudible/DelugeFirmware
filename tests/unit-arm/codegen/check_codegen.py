#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Checks that every fixed-point operation compiles to the instruction it names.

Disassembles fixed_point_probe.cpp's objects (one ARM, one Thumb-2) and compares each probe's instructions, its final
return excluded, with EXACT, and the operand text with OPERANDS where an immediate matters. An operation with no single
A9 instruction is listed in BUDGET instead: it must make no call (a call or tail-call relocation in its body) and stay
within that many instructions. A lane-pack probe is listed in COUNTS: no call, and each named instruction exactly that
many times, one per lane. The loop_* and shape_* functions are REPORTED, not gated: they show which loop and lane-pack
shapes the compiler auto-vectorizes today.

Usage:
    check_codegen.py OBJDUMP ARM_OBJECT THUMB_OBJECT
"""

from __future__ import annotations

import re
import subprocess
import sys
from dataclasses import dataclass, field

EXACT = {
    "probe_add_wrap": ["add"],
    "probe_sub_wrap": ["sub"],
    "probe_neg_wrap": ["rsb"],
    "probe_add_sat": ["qadd"],
    "probe_sub_sat": ["qsub"],
    "probe_neg_sat": ["mov", "qsub"],  # qsub from a zeroed register
    "probe_add_double_sat": ["qdadd"],
    "probe_sub_double_sat": ["qdsub"],
    "probe_add_halves_sat": ["asr", "add", "ssat", "lsl"],
    "probe_eq": ["sub", "clz", "lsr"],
    "probe_clamp_high": ["cmp", "movge"],
    "probe_mul": ["smmul"],
    "probe_mul_r": ["smmulr"],
    "probe_mla": ["smmla"],
    "probe_mla_r": ["smmlar"],
    "probe_mls": ["smmls"],
    "probe_mls_r": ["smmlsr"],
    "probe_mul_wide": ["smull"],
    "probe_mla_wide": ["smlal"],
    "probe_mul_uq": ["umull"],
    "probe_mul_uq_q": ["umull", "asr", "mla"],
    "probe_mul_as_sat": ["smmul", "qadd"],
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
    "probe_as_clamp_then_shift_3": ["ssat", "lsl"],
    "probe_scale_shl_clamp_then_shift_3": ["ssat", "lsl"],
    "probe_scale_shr_round_3": ["ubfx", "add"],
    "probe_scale_shr_floor_runtime": ["asr"],
    "probe_scale_shl_wrap_runtime": ["lsl"],
    "probe_halve_toward_zero": ["add", "asr"],
    "probe_from_q16": ["lsl"],
    "probe_widen_9": ["asr", "lsl", "orr", "lsl"],
    "probe_narrow_floor_wrap": ["lsr", "orr"],
    "probe_narrow_round_wrap": ["lsr", "ubfx", "orr", "add"],
    "probe_mul_as_wrap": ["smmul", "lsl"],
    "probe_mul_r_as_wrap": ["smmulr", "lsl"],
    "probe_mul_q16": ["smulwb"],
    "probe_mul_bottom": ["smulwb"],
    "probe_mul_top": ["smulwt"],
    "probe_mla_q16": ["smlawb"],
    "probe_dot": ["smuad"],
    "probe_dot_diff": ["smusd"],
    "probe_dot_acc": ["smlad"],
    "probe_dot_wide_acc": ["smlald"],
    "probe_pack": ["pkhbt"],
    "probe_q16x2_add_sat": ["qadd16"],
    "probe_q16x2_sub_sat": ["qsub16"],
    "probe_q16x2_halving_add": ["shadd16"],
    "probe_from_float_31": ["vcvt.s32.f32", "vmov"],
    "probe_to_float_31": ["vmov", "vcvt.f32.s32"],
    "probe_vec_add_wrap": ["vadd.i32"],
    "probe_vec_sub_wrap": ["vsub.i32"],
    "probe_vec_add_wrap_phase": ["vadd.i32"],
    "probe_vec_add_wrap_q16": ["vadd.i16"],
    "probe_vec_mul_doubling_sat": ["vqdmulh.s32"],
    "probe_vec_mul_doubling_sat_scalar": ["vdup.32", "vqdmulh.s32"],
    "probe_vec_mul_doubling_sat_q16": ["vdup.16", "vqdmulh.s16"],
    "probe_vec_mul_doubling_r_sat": ["vqrdmulh.s32"],
    "probe_vec_mul_doubling_r_sat_scalar": ["vdup.32", "vqrdmulh.s32"],
    "probe_vec_to_float_24": ["vcvt.f32.s32"],
    "probe_vec_from_float_24": ["vcvt.s32.f32"],
    "probe_vec_scale_shl_wrap_3": ["vshl.i32"],
    "probe_vec_scale_shr_floor_3": ["vshr.s32"],
    "probe_vec_add_scale_shr_floor_4": ["vsra.s32"],
    "probe_vec_mul_doubling_sat_lane_1": ["vqdmulh.s32"],
    "probe_vec_mul_doubling_sat_lane_2": ["vqdmulh.s32"],
    "probe_vec_as_floor_3": ["vshr.s32"],
    "probe_vec_reinterpret_bits": [],
    "probe_vec_pair_lo": ["vmovn.i32"],
    "probe_vec_pair_hi": ["vshrn.i32"],
    "probe_vec_from_q16": ["vshll.i16"],
    "probe_vec_mul_doubling_wide_sat": ["vqdmull.s16"],
    "probe_vec_mla_doubling_wide_sat": ["vqdmlal.s16"],
    "probe_vec_mla_wide": ["vmlal.s16"],  # the high halves are d registers: no move
}

# Operand text that must appear in the body: the immediates a wrong width or shift would change.
OPERANDS = {
    "probe_vec_scale_shl_wrap_3": ["#0x3"],
    "probe_vec_to_float_24": ["#0x18"],
    "probe_vec_from_float_24": ["#0x18"],
    "probe_vec_scale_shr_floor_3": ["#0x3"],
    "probe_vec_as_floor_3": ["#0x3"],
    "probe_vec_add_scale_shr_floor_4": ["#0x4"],
    "probe_vec_pair_hi": ["#0x10"],
    "probe_vec_from_q16": ["#0x10"],
    # b arrives in q1 (d2, d3): lane 1 is d2[1], lane 2 is d3[0].
    "probe_vec_mul_doubling_sat_lane_1": ["d2[1]"],
    "probe_vec_mul_doubling_sat_lane_2": ["d3[0]"],
    "probe_clamp_bits_24": ["#0x18"],
    "probe_clamp_unsigned_16": ["#0x10"],
    "probe_clamp_after_shr": ["#0x18", "asr #4"],
    "probe_to_q16_sat": ["#0x10", "asr #16"],
    "probe_as_clamp_then_shift_3": ["#0x1d"],
    "probe_scale_shl_clamp_then_shift_3": ["#0x1d"],
    "probe_add_halves_sat": ["#0x1f"],
    "probe_from_float_31": ["#31"],
    "probe_to_float_31": ["#31"],
}

BUDGET = {
    "probe_vec_sum_wrap": 4,
    "probe_lt": 6,
    "probe_clamp_bits_runtime": 12,
    "probe_scale_shl_clamp_then_shift_runtime": 16,
    "probe_scale_pow2_sat": 18,
    "probe_narrow_floor_sat": 14,
    "probe_mul_doubling_sat": 10,
    "probe_mul_doubling_r_sat": 12,
    "probe_scale_shl_sat_3": 10,
    "probe_from_float_round_31": 34,
}

# Lane-pack probes: the scalar instruction once per lane, within an instruction budget (the ARM build's count: the
# lanes' loads and stores and a push/pop) and with nothing through the stack.
COUNTS = {
    "probe_lanes2_mul": ({"smmul": 2}, 7),
    "probe_lanes2_mla": ({"smmla": 2}, 9),
    "probe_lanes2_add_sat": ({"qadd": 2}, 8),
    "probe_lanes2_scale_shl_sat_1": ({"qadd": 2}, 5),
    "probe_lanes2_round_trip": ({}, 5),
    "probe_lanes2_raw_mul_r": ({"smmulr": 2}, 7),
}

# A loop shape that uses any of these was auto-vectorized into NEON.
VECTOR_MARKERS = (
    "vmull",
    "vmov",
    "vmovl",
    "vshrn",
    "vld1",
    "vst1",
    "vcgt",
    "vcge",
    "vceq",
    "vdup",
    "vqadd",
    "vqdmulh",
)

# Thumb-2's narrow flag-setting forms do the same arithmetic; ARM and Thumb then compare alike.
FLAG_SETTING_NARROW = re.compile(r"^(add|sub|rsb|mov|mvn|lsl|lsr|asr)s$")
IT_BLOCK = re.compile(r"^it[te]{0,3}$")
CALL_RELOCATION = re.compile(r"R_ARM_(THM_)?(CALL|JUMP24|JUMP19|PC22)\b")


@dataclass
class Body:
    mnemonics: list[str] = field(default_factory=list)
    text: list[str] = field(default_factory=list)
    calls: list[str] = field(default_factory=list)
    everything: list[str] = field(
        default_factory=list
    )  # every mnemonic, blocks laid out after a return included


def normalise(mnemonic: str, thumb: bool) -> str:
    base = mnemonic.removesuffix(".w").removesuffix(".n")
    if thumb:
        match = FLAG_SETTING_NARROW.match(base)
        if match:
            base = match.group(1)
    return base


def bodies(objdump: str, obj: str, thumb: bool) -> dict[str, Body]:
    out = subprocess.run(
        [objdump, "-dr", "--no-show-raw-insn", obj],
        check=True,
        capture_output=True,
        text=True,
    )
    found: dict[str, Body] = {}
    current: Body | None = None
    for line in out.stdout.splitlines():
        header = re.match(r"^[0-9a-f]+ <((?:probe|loop|shape)_\w+)>:$", line)
        if header:
            current = found.setdefault(header.group(1), Body())
            continue
        if current is None:
            continue
        if not line.strip():
            current = None
            continue
        relocation = CALL_RELOCATION.search(line)
        if relocation:
            current.calls.append(line.strip())
            continue
        insn = re.match(r"^\s+[0-9a-f]+:\s+([a-z][\w.]*)(.*)$", line)
        if insn:
            mnemonic = normalise(insn.group(1), thumb)
            if IT_BLOCK.match(mnemonic):
                continue
            current.mnemonics.append(mnemonic)
            current.text.append(f"{mnemonic}{insn.group(2)}")
            current.everything.append(mnemonic)
    for body in found.values():
        # Drop the final return and anything after it (literal pools, padding).
        if "bx" in body.mnemonics:
            end = len(body.mnemonics) - 1 - body.mnemonics[::-1].index("bx")
            body.mnemonics, body.text = body.mnemonics[:end], body.text[:end]
    return found


def check(found: dict[str, Body], mode: str) -> tuple[list[str], list[str]]:
    errors, report = [], []
    probes = {name for name in found if name.startswith("probe_")}
    for name in sorted(set(EXACT) | set(BUDGET) | set(COUNTS) | probes):
        body = found.get(name)
        if body is None:
            errors.append(f"[{mode}] {name}: missing from the probe")
            continue
        if body.calls:
            errors.append(f"[{mode}] {name}: makes a call: {body.calls}")
        if name in EXACT:
            if body.mnemonics != EXACT[name]:
                errors.append(
                    f"[{mode}] {name}: expected {EXACT[name]}, got {body.mnemonics}"
                )
            joined = " ".join(body.text)
            for needed in OPERANDS.get(name, []):
                if needed not in joined:
                    errors.append(
                        f"[{mode}] {name}: operand {needed!r} not in {body.text}"
                    )
        elif name in BUDGET:
            if len(body.mnemonics) > BUDGET[name]:
                errors.append(
                    f"[{mode}] {name}: {len(body.mnemonics)} instructions, budget {BUDGET[name]}: {body.mnemonics}"
                )
        elif name in COUNTS:
            counts, budget = COUNTS[name]
            for mnemonic, times in counts.items():
                if body.mnemonics.count(mnemonic) != times:
                    errors.append(
                        f"[{mode}] {name}: expected {times} {mnemonic}, got {body.mnemonics}"
                    )
            if len(body.mnemonics) > budget:
                errors.append(
                    f"[{mode}] {name}: {len(body.mnemonics)} instructions, budget {budget}: {body.mnemonics}"
                )
            if any(re.search(r"\[sp\b", text) for text in body.text):
                errors.append(f"[{mode}] {name}: goes through the stack: {body.text}")
            neon = [m for m in body.everything if m.startswith(VECTOR_MARKERS)]
            if neon:
                errors.append(f"[{mode}] {name}: uses NEON: {neon}")
        else:
            errors.append(
                f"[{mode}] {name}: in the probe but not in EXACT, BUDGET or COUNTS"
            )
    for name in sorted(n for n in found if n.startswith(("loop_", "shape_"))):
        neon = sorted(
            {m for m in found[name].everything if m.startswith(VECTOR_MARKERS)}
        )
        shape = f"auto-vectorized ({', '.join(neon)})" if neon else "scalar"
        report.append(f"[{mode}] {name}: {shape}")
    return errors, report


def main(argv: list[str]) -> int:
    if len(argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2
    objdump, arm_obj, thumb_obj = argv[1:]
    errors, report = check(bodies(objdump, arm_obj, thumb=False), "arm")
    thumb_errors, thumb_report = check(bodies(objdump, thumb_obj, thumb=True), "thumb")
    errors += thumb_errors
    for line in report + thumb_report:
        print(line)
    for error in errors:
        print(error)
    if not errors:
        print(
            f"fixed-point codegen: {len(EXACT)} exact, {len(BUDGET)} budgeted, {len(COUNTS)} lane-counted, "
            "ARM and Thumb-2: OK"
        )
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
