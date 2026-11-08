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

#pragma once

#include <cstdint>

#if defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
#endif

/// @file
/// @brief FlushToZero: flush-to-zero for a scope, so scalar float matches NEON on every target.
namespace deluge::dsp::fp {

/// @brief Sets flush-to-zero (and denormals-are-zero where the target separates them) for its lifetime and restores
/// the previous control word after.
/// @details ARMv7 NEON always flushes denormals; scalar VFP, AArch64 NEON and x86 SSE do only when told. A float
/// kernel's scalar reference and its lanes agree bit for bit only when both flush.
class FlushToZero {
public:
	FlushToZero() : saved_{read()} { write(saved_ | kBits); }
	~FlushToZero() { write(saved_); }
	FlushToZero(const FlushToZero&) = delete;
	FlushToZero& operator=(const FlushToZero&) = delete;
	FlushToZero(FlushToZero&&) = delete;
	FlushToZero& operator=(FlushToZero&&) = delete;

private:
#ifdef __aarch64__
	static constexpr uint64_t kBits = uint64_t{1} << 24; ///< FPCR.FZ
	using Word = uint64_t;
	static Word read() { return __builtin_aarch64_get_fpcr64(); }
	static void write(Word w) { __builtin_aarch64_set_fpcr64(w); }
#elifdef __arm__
	static constexpr uint32_t kBits = uint32_t{1} << 24; ///< FPSCR.FZ
	using Word = uint32_t;
	static Word read() { return __builtin_arm_get_fpscr(); }
	static void write(Word w) { __builtin_arm_set_fpscr(w); }
#else
	static constexpr uint32_t kBits = 0x8040; ///< MXCSR.FTZ | MXCSR.DAZ
	using Word = uint32_t;
	static Word read() { return _mm_getcsr(); }
	static void write(Word w) { _mm_setcsr(w); }
#endif
	Word saved_;
};

} // namespace deluge::dsp::fp
