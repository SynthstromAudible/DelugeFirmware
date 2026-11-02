/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */
// One function per fixed-point operation, raw integers in and out, for check_codegen.py to disassemble. Built with
// the shipping image's codegen flags, once as ARM and once as Thumb-2 (the app's TUs are Thumb; dsp-next's are ARM).

#include "dsp-next/core/fixed_point.h"
#include <cstdint>

using namespace deluge::dsp;

namespace {
Q<31> q31(std::int32_t v) {
	return Q<31>::from_raw(v);
}
Q<30> q30(std::int32_t v) {
	return Q<30>::from_raw(v);
}
Q16x2<15> pair(std::uint32_t v) {
	return Q16x2<15>::from_raw(v);
}
} // namespace

extern "C" {
// NOLINTBEGIN(readability-identifier-naming): the names are the checker's keys.
std::int32_t probe_add_wrap(std::int32_t a, std::int32_t b) {
	return add_wrap(q31(a), q31(b)).raw();
}
std::int32_t probe_sub_wrap(std::int32_t a, std::int32_t b) {
	return sub_wrap(q31(a), q31(b)).raw();
}
std::int32_t probe_add_sat(std::int32_t a, std::int32_t b) {
	return add_sat(q31(a), q31(b)).raw();
}
std::int32_t probe_sub_sat(std::int32_t a, std::int32_t b) {
	return sub_sat(q31(a), q31(b)).raw();
}
std::int32_t probe_neg_sat(std::int32_t a) {
	return neg_sat(q31(a)).raw();
}
std::int32_t probe_add_double_sat(std::int32_t a, std::int32_t b) {
	return add_double_sat(q31(a), q31(b)).raw();
}
std::int32_t probe_sub_double_sat(std::int32_t a, std::int32_t b) {
	return sub_double_sat(q31(a), q31(b)).raw();
}
std::int32_t probe_mul(std::int32_t a, std::int32_t b) {
	return mul(q31(a), q31(b)).raw();
}
std::int32_t probe_mul_r(std::int32_t a, std::int32_t b) {
	return mul_r(q31(a), q31(b)).raw();
}
std::int32_t probe_mla(std::int32_t c, std::int32_t a, std::int32_t b) {
	return mla(q30(c), q31(a), q31(b)).raw();
}
std::int32_t probe_mla_r(std::int32_t c, std::int32_t a, std::int32_t b) {
	return mla_r(q30(c), q31(a), q31(b)).raw();
}
std::int32_t probe_mls(std::int32_t c, std::int32_t a, std::int32_t b) {
	return mls(q30(c), q31(a), q31(b)).raw();
}
std::int32_t probe_mls_r(std::int32_t c, std::int32_t a, std::int32_t b) {
	return mls_r(q30(c), q31(a), q31(b)).raw();
}
std::int64_t probe_mul_wide(std::int32_t a, std::int32_t b) {
	return mul_wide(q31(a), q31(b)).raw();
}
std::int64_t probe_mla_wide(std::int64_t c, std::int32_t a, std::int32_t b) {
	return mla_wide(Q64<62>::from_raw(c), q31(a), q31(b)).raw();
}
std::uint32_t probe_mul_uq(std::uint32_t a, std::uint32_t b) {
	return mul(UQ<32>::from_raw(a), UQ<32>::from_raw(b)).raw();
}
std::int32_t probe_clamp_bits_24(std::int32_t a) {
	return clamp_bits<24>(q31(a)).raw();
}
std::int32_t probe_clamp_unsigned_16(std::int32_t a) {
	return clamp_unsigned_bits<16>(q31(a)).raw();
}
std::int32_t probe_clamp_after_shr(std::int32_t a) {
	return clamp_bits<24>(scale_shr_floor<4>(q31(a))).raw();
}
void probe_to_q16_sat(std::int32_t a, std::int16_t* out) {
	*out = to_q16_sat(q31(a)).raw();
}
std::int32_t probe_as_sat_1(std::int32_t a) {
	return as_sat<Q<31>>(q30(a)).raw();
}
std::int32_t probe_as_wrap_1(std::int32_t a) {
	return as_wrap<Q<31>>(q30(a)).raw();
}
std::int32_t probe_as_floor_3(std::int32_t a) {
	return as_floor<Q<28>>(q31(a)).raw();
}
std::int32_t probe_mul_as_wrap(std::int32_t a, std::int32_t b) {
	return mul_as_wrap<Q<31>>(q31(a), q31(b)).raw();
}
std::int32_t probe_mul_r_as_wrap(std::int32_t a, std::int32_t b) {
	return mul_r_as_wrap<Q<31>>(q31(a), q31(b)).raw();
}
std::int32_t probe_mul_q16(std::int32_t a, std::int16_t b) {
	return mul(q31(a), Q16<15>::from_raw(b)).raw();
}
std::int32_t probe_mul_top(std::int32_t a, std::uint32_t b) {
	return mul_top(q31(a), pair(b)).raw();
}
std::int32_t probe_mla_q16(std::int32_t c, std::int32_t a, std::int16_t b) {
	return mla(q30(c), q31(a), Q16<15>::from_raw(b)).raw();
}
std::int32_t probe_dot(std::uint32_t x, std::uint32_t y) {
	return dot(pair(x), pair(y)).raw();
}
std::int32_t probe_dot_diff(std::uint32_t x, std::uint32_t y) {
	return dot_diff(pair(x), pair(y)).raw();
}
std::int32_t probe_dot_acc(std::int32_t c, std::uint32_t x, std::uint32_t y) {
	return dot_acc(q30(c), pair(x), pair(y)).raw();
}
std::int64_t probe_dot_wide_acc(std::int64_t c, std::uint32_t x, std::uint32_t y) {
	return dot_wide_acc(Q64<30>::from_raw(c), pair(x), pair(y)).raw();
}
std::uint32_t probe_q16x2_add_sat(std::uint32_t x, std::uint32_t y) {
	return add_sat(pair(x), pair(y)).raw();
}
std::uint32_t probe_q16x2_sub_sat(std::uint32_t x, std::uint32_t y) {
	return sub_sat(pair(x), pair(y)).raw();
}
std::uint32_t probe_q16x2_halving_add(std::uint32_t x, std::uint32_t y) {
	return halving_add(pair(x), pair(y)).raw();
}
std::int32_t probe_from_float_31(float v) {
	return from_float<31>(v).raw();
}
float probe_to_float_31(std::int32_t a) {
	return to_float(q31(a));
}
std::int32_t probe_clamp_bits_runtime(std::int32_t a, int n) {
	return clamp_bits(q31(a), n).raw();
}
std::int32_t probe_narrow_floor_sat(std::int64_t a) {
	return narrow_floor_sat<Q<31>>(Q64<62>::from_raw(a)).raw();
}
std::int32_t probe_mul_doubling_sat(std::int32_t a, std::int32_t b) {
	return mul_doubling_sat(q31(a), q31(b)).raw();
}
std::int32_t probe_scale_shl_sat_3(std::int32_t a) {
	return scale_shl_sat<3>(q31(a)).raw();
}
std::int32_t probe_as_round_3(std::int32_t a) {
	return as_round<Q<28>>(q31(a)).raw();
}
std::int32_t probe_from_float_round_31(float v) {
	return from_float_round<31>(v).raw();
}
// NOLINTEND(readability-identifier-naming)
}
