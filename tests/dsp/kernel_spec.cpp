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
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mode.h"
#include "dsp-next/core/stereo_vector.h"
#include "dsp-next/core/types.h"
#include "util/fixedpoint.h"

#include "loop_state_kernels.h"

#include <array>
#include <concepts>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <tuple>

using namespace deluge::dsp;

namespace {

/// A processor kernel without modes or prepare: a q31 gain (Q1.31, scaled ×2 to reach unity).
struct Gain {
	using value_type = q31;
	using step_type = q31;
	struct Settings {
		q31 gain; ///< Q1.31.
	};
	struct Coefficients {
		q31 gain; ///< Q1.31.
	};
	struct State {};
	static Coefficients coefficients(const Settings& s) { return {.gain = s.gain}; }
	[[gnu::always_inline]] static q31 step(const Coefficients& c, const EmptyBlock& /*b*/, State& /*s*/, q31 x) {
		return multiply_32x32_rshift32(x, c.gain) << 1;
	}
};

/// A moded processor kernel: halves or doubles.
struct Shaped {
	using value_type = q31;
	using step_type = q31;
	enum class Mode : uint8_t { HALF, DOUBLE };
	using Modes = ModeList<Mode::HALF, Mode::DOUBLE>;
	struct Settings {
		Mode mode;
	};
	struct Coefficients {
		Mode mode;
	};
	struct State {};
	static Coefficients coefficients(const Settings& s) { return {.mode = s.mode}; }
	template <Mode M>
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/, q31 x) {
		if constexpr (M == Mode::HALF) {
			return x >> 1;
		}
		else {
			return x << 1;
		}
	}
};

/// A generator kernel: counts up from its state.
struct Counter {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {
		q31 n;
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s) {
		return ++s.n;
	}
};

/// A mixer kernel: sums its two inputs.
struct Add {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/, q31 a,
	                                       q31 b2) {
		return a + b2;
	}
};

/// A processor kernel with a prepare: the block's offset comes from State.
struct Prepared {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {
		q31 offset;
	};
	struct Block {
		q31 offset;
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static Block prepare(const Coefficients& /*c*/, State& s, std::size_t /*n*/) { return {.offset = s.offset}; }
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const Block& b, State& /*s*/, q31 x) {
		return x + b.offset;
	}
};

/// A vector processor kernel whose ramp persists across blocks, so its partial last vector advances by the count.
struct Ramp {
	using value_type = q31;
	using step_type = Argon<q31>;
	static constexpr bool kPartialStepAdvancesByCount = true;
	struct Settings {};
	struct Coefficients {};
	struct State {
		Argon<q31> value; ///< The ramp's value in each lane.
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static Argon<q31> step(const Coefficients& c, const EmptyBlock& b, State& s, Argon<q31> x) {
		return step(c, b, s, x, ActiveLanes{.n = Argon<q31>::lanes});
	}
	[[gnu::always_inline]] static Argon<q31> step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s,
	                                              Argon<q31> x, ActiveLanes active) {
		const Argon<q31> out = x + s.value;
		s.value = s.value + Argon<q31>{static_cast<q31>(active.n)};
		return out;
	}
};

/// A block-staged kernel: reverses its block. It reads each mirrored pair before writing either, so it gives the
/// same result in place as out of place.
struct Staged {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static void process(const Coefficients& /*c*/, State& /*s*/, Signal<q31> in, Buffer<q31> out) {
		const std::size_t n = in.size();
		for (std::size_t i = 0; i < (n + 1) / 2; ++i) {
			const q31 front = in[i];
			const q31 back = in[n - 1 - i];
			out[i] = back;
			out[n - 1 - i] = front;
		}
	}
};

/// A block-staged kernel declaring a prepare, which process has no Block to receive.
struct StagedPrepare {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static EmptyBlock prepare(const Coefficients& /*c*/, State& /*s*/, std::size_t /*n*/) { return {}; }
	static void process(const Coefficients& /*c*/, State& /*s*/, Signal<q31> in, Buffer<q31> out) {
		for (std::size_t i = 0; i < in.size(); ++i) {
			out[i] = in[i];
		}
	}
};

/// A processor stepping stereo vectors over mono q31 blocks: no loop runs that pair.
struct StereoVectorOverQ31 {
	using value_type = q31;
	using step_type = StereoVector;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static StereoVector step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/,
	                                                StereoVector x) {
		return x;
	}
};

/// A processor stepping q31 vectors over stereo frames: no loop runs that pair.
struct VectorOverFrames {
	using value_type = StereoSample;
	using step_type = Argon<q31>;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static Argon<q31> step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/,
	                                              Argon<q31> x) {
		return x;
	}
};

/// A moded kernel whose default Coefficients name a mode it does not list: a default-constructed component would
/// dispatch on an unlisted mode.
struct UnlistedDefaultMode {
	using value_type = q31;
	using step_type = q31;
	enum class Mode : uint8_t { OFF, ON };
	using Modes = ModeList<Mode::ON>;
	struct Settings {};
	struct Coefficients {
		Mode mode;
	};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {.mode = Mode::ON}; }
	template <Mode M>
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/, q31 x) {
		return x;
	}
};

/// UnlistedDefaultMode with a default member initialiser naming its one listed mode.
struct ListedDefaultMode {
	using value_type = q31;
	using step_type = q31;
	enum class Mode : uint8_t { OFF, ON };
	using Modes = ModeList<Mode::ON>;
	struct Settings {};
	struct Coefficients {
		Mode mode = Mode::ON;
	};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	template <Mode M>
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/, q31 x) {
		return x;
	}
};

/// A component written with its own verbs instead of a kernel's.
struct CustomVerbs {
	using value_type = q31;
	q31 gain = 0;
	void configure(q31 g) { gain = g; }
	void renderMono(Buffer<q31> out) {
		for (q31& v : out) {
			v = gain;
		}
	}
};

/// A kernel that provides both a processor and a mixer step.
struct TwoArities {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/, q31 x) {
		return x;
	}
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/, q31 a,
	                                       q31 b2) {
		return a + b2;
	}
};

/// A kernel whose prepare has the wrong signature.
struct BadPrepare {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static int prepare(const Coefficients& /*c*/, State& /*s*/, std::size_t /*n*/) { return 0; }
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/, q31 x) {
		return x;
	}
};

/// A kernel whose State is not a plain value struct: copying it runs code.
struct NotPlain {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {
		State() = default;
		State(const State& /*other*/) {}
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/, q31 x) {
		return x;
	}
};

/// A moded kernel whose prepare is a template on the mode: not callable as prepare(c, s, n).
struct TemplatePrepare {
	using value_type = q31;
	using step_type = q31;
	enum class Mode : uint8_t { A, B };
	using Modes = ModeList<Mode::A, Mode::B>;
	struct Settings {};
	struct Coefficients {
		Mode mode;
	};
	struct State {};
	struct Block {
		q31 offset;
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	template <Mode M>
	static Block prepare(const Coefficients& /*c*/, State& /*s*/, std::size_t /*n*/) {
		return {};
	}
	template <Mode M>
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const Block& /*b*/, State& /*s*/, q31 x) {
		return x;
	}
};

/// A kernel whose prepare overloads include no prepare(c, s, n).
struct OverloadedPrepare {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	struct Block {
		q31 offset;
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static Block prepare(const Coefficients& /*c*/, State& /*s*/) { return {}; }
	static Block prepare(const Coefficients& /*c*/, const State& /*s*/, int* /*n*/) { return {}; }
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const Block& /*b*/, State& /*s*/, q31 x) {
		return x;
	}
};

/// A kernel without a Block whose prepare overloads include no prepare(c, s, n).
struct OverloadedPrepareNoBlock {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static EmptyBlock prepare(const Coefficients& /*c*/, State& /*s*/) { return {}; }
	static EmptyBlock prepare(const Coefficients& /*c*/, const State& /*s*/, int* /*n*/) { return {}; }
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/, q31 x) {
		return x;
	}
};

/// A kernel declaring a Block but no prepare to make one.
struct BlockNoPrepare {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	struct Block {
		q31 offset;
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const Block& /*b*/, State& /*s*/, q31 x) {
		return x;
	}
};

/// A vector generator whose phase persists across blocks: the counted step is at the generator arity.
struct VecPhase {
	using value_type = q31;
	using step_type = Argon<q31>;
	static constexpr bool kPartialStepAdvancesByCount = true;
	struct Settings {};
	struct Coefficients {};
	struct State {
		Argon<q31> phase; ///< The phase in each lane.
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static Argon<q31> step(const Coefficients& c, const EmptyBlock& b, State& s) {
		return step(c, b, s, ActiveLanes{.n = Argon<q31>::lanes});
	}
	[[gnu::always_inline]] static Argon<q31> step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s,
	                                              ActiveLanes active) {
		const Argon<q31> out = s.phase;
		s.phase = s.phase + Argon<q31>{static_cast<q31>(active.n)};
		return out;
	}
};

/// A scalar processor declaring the counted partial step, which only a vector loop has. Its counted overload's tag
/// keeps it from reading as a mixer step.
struct ScalarCount {
	using value_type = q31;
	using step_type = q31;
	static constexpr bool kPartialStepAdvancesByCount = true;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/, q31 x) {
		return x;
	}
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/, q31 x,
	                                       ActiveLanes /*active*/) {
		return x;
	}
};

/// A moded block-staged kernel: process is a template on the mode, like step.
struct ModedStaged {
	using value_type = q31;
	using step_type = q31;
	enum class Mode : uint8_t { COPY, NEGATE };
	using Modes = ModeList<Mode::COPY, Mode::NEGATE>;
	struct Settings {
		Mode mode;
	};
	struct Coefficients {
		Mode mode;
	};
	struct State {};
	static Coefficients coefficients(const Settings& s) { return {.mode = s.mode}; }
	template <Mode M>
	static void process(const Coefficients& /*c*/, State& /*s*/, Signal<q31> in, Buffer<q31> out) {
		for (std::size_t i = 0; i < in.size(); ++i) {
			out[i] = M == Mode::COPY ? in[i] : -in[i];
		}
	}
};

/// A vector kernel declaring the counted partial step without providing it.
struct VecNoCount {
	using value_type = q31;
	using step_type = Argon<q31>;
	static constexpr bool kPartialStepAdvancesByCount = true;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static Argon<q31> step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/,
	                                              Argon<q31> x) {
		return x;
	}
};

/// A vector generator declaring the counted partial step without providing it.
struct VecGeneratorNoCount {
	using value_type = q31;
	using step_type = Argon<q31>;
	static constexpr bool kPartialStepAdvancesByCount = true;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static Argon<q31> step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/) {
		return Argon<q31>{0};
	}
};

/// A kernel whose Block is not a plain value struct.
struct NotPlainBlock {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	struct Block {
		Block() = default;
		Block(const Block& /*other*/) {}
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static Block prepare(const Coefficients& /*c*/, State& /*s*/, std::size_t /*n*/) { return {}; }
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const Block& /*b*/, State& /*s*/, q31 x) {
		return x;
	}
};

/// A kernel whose prepare is a non-static member.
struct MemberPrepare {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	struct Block {
		q31 offset;
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[nodiscard]] Block prepare(const Coefficients& /*c*/, State& /*s*/, std::size_t /*n*/) const { return {}; }
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const Block& /*b*/, State& /*s*/, q31 x) {
		return x;
	}
};

/// A kernel whose step returns something other than its step_type.
struct WrongStepReturn {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static int64_t step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/,
	                                           q31 x) {
		return x;
	}
};

/// A kernel whose coefficients() returns something other than its Coefficients.
struct WrongCoefficientsReturn {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static int coefficients(const Settings& /*s*/) { return 0; }
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/, q31 x) {
		return x;
	}
};

/// A kernel whose step is a non-static member.
struct MemberStep {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/, q31 x) const {
		return x;
	}
};

/// A moded kernel whose Coefficients::mode is of another enumeration than its Modes.
struct ModeMismatch {
	using value_type = q31;
	using step_type = q31;
	enum class Mode : uint8_t { A };
	enum class Other : uint8_t { X };
	using Modes = ModeList<Mode::A>;
	struct Settings {};
	struct Coefficients {
		Other mode;
	};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	template <Mode M>
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/, q31 x) {
		return x;
	}
};

/// A generator whose reset quirk has the stated shape: static void reset(State&).
struct ResetQuirk {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {
		q31 n;
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static void reset(State& s) { s.n = 100; }
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s) {
		return ++s.n;
	}
};

/// A generator whose reset is a non-static member.
struct MemberReset {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {
		q31 n;
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	void reset(State& s) const { s.n = 100; }
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s) {
		return ++s.n;
	}
};

/// A generator whose reset returns the reset State instead of setting one.
struct ValueReset {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {
		q31 n;
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static State reset() { return {.n = 100}; }
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s) {
		return ++s.n;
	}
};

/// A control kernel: a block-rate level falling by its rate per sample, saturating at 0.
struct Fall {
	using value_type = q31;
	using step_type = q31;
	struct Settings {
		q31 rate; ///< Fall per sample, Q1.31.
	};
	struct Coefficients {
		q31 rate; ///< Fall per sample, Q1.31.
	};
	struct State {
		q31 level = ONE_Q31; ///< Q1.31.
	};
	static Coefficients coefficients(const Settings& s) { return {.rate = s.rate}; }
	static q31 advance(const Coefficients& c, State& s, std::size_t n) {
		const int64_t next = int64_t{s.level} - (int64_t{c.rate} * static_cast<int64_t>(n));
		s.level = next < 0 ? 0 : static_cast<q31>(next);
		return s.level;
	}
};

/// A moded control kernel: holds or clears its level, with `advance` a template on the mode.
struct ModedFall {
	using value_type = q31;
	using step_type = q31;
	enum class Mode : uint8_t { HOLD, CLEAR };
	using Modes = ModeList<Mode::HOLD, Mode::CLEAR>;
	struct Settings {
		Mode mode;
	};
	struct Coefficients {
		Mode mode;
	};
	struct State {
		q31 level;
	};
	static Coefficients coefficients(const Settings& s) { return {.mode = s.mode}; }
	template <Mode M>
	static q31 advance(const Coefficients& /*c*/, State& s, std::size_t /*n*/) {
		if constexpr (M == Mode::CLEAR) {
			s.level = 0;
		}
		return s.level;
	}
};

/// A class with both a control kernel's `advance` and a generator `step`: two forms.
struct AdvanceAndStep {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static q31 advance(const Coefficients& /*c*/, State& /*s*/, std::size_t /*n*/) { return 0; }
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/) {
		return 0;
	}
};

/// A control kernel declaring a well-formed prepare, which a control kernel has no use for: its block is its step.
struct AdvancePrepare {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static EmptyBlock prepare(const Coefficients& /*c*/, State& /*s*/, std::size_t /*n*/) { return {}; }
	static q31 advance(const Coefficients& /*c*/, State& /*s*/, std::size_t /*n*/) { return 0; }
};

/// A control kernel declaring the counted partial step, which a block-rate kernel has no vector to need.
struct AdvancePartialCount {
	using value_type = q31;
	using step_type = q31;
	static constexpr bool kPartialStepAdvancesByCount = true;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static q31 advance(const Coefficients& /*c*/, State& /*s*/, std::size_t /*n*/) { return 0; }
};

/// A control kernel whose step type is a vector over its q31 value type.
struct AdvanceVectorStep {
	using value_type = q31;
	using step_type = Argon<q31>;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static q31 advance(const Coefficients& /*c*/, State& /*s*/, std::size_t /*n*/) { return 0; }
};

/// A control kernel whose advance takes its count as an int rather than a std::size_t.
struct AdvanceIntCount {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static q31 advance(const Coefficients& /*c*/, State& /*s*/, int /*n*/) { return 0; }
};

/// A control kernel whose advance takes its count as a uint32_t, which is std::size_t on a 32-bit Linux target.
struct AdvanceU32Count {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static q31 advance(const Coefficients& /*c*/, State& /*s*/, uint32_t /*n*/) { return 0; }
};

/// A control kernel whose advance is a template on its count's type: its address converts to the exact signature,
/// with the count deduced as std::size_t.
struct AdvanceDeducedCount {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	template <typename N>
	static q31 advance(const Coefficients& /*c*/, State& /*s*/, N /*n*/) {
		return 0;
	}
};

/// A control kernel whose advance returns something other than its value_type.
struct AdvanceWrongReturn {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static int64_t advance(const Coefficients& /*c*/, State& /*s*/, std::size_t /*n*/) { return 0; }
};

/// A moded control kernel whose advance exists at its first mode only.
struct AdvanceFirstModeOnly {
	using value_type = q31;
	using step_type = q31;
	enum class Mode : uint8_t { A, B };
	using Modes = ModeList<Mode::A, Mode::B>;
	struct Settings {};
	struct Coefficients {
		Mode mode;
	};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	template <Mode M>
	requires(M == Mode::A)
	static q31 advance(const Coefficients& /*c*/, State& /*s*/, std::size_t /*n*/) {
		return 0;
	}
};

/// A moded processor kernel whose mode is a structural class: a scale and a sign, compared member by member.
struct Voiced {
	using value_type = q31;
	using step_type = q31;
	enum class Scale : uint8_t { HALF, DOUBLE };
	/// @brief The mode: a scale and a sign.
	struct Mode {
		Scale scale = Scale::DOUBLE; ///< Listed first, so the default.
		bool negate = false;         ///< Whether the scaled sample is negated.
		constexpr bool operator==(const Mode&) const = default;
	};
	using Modes = ModeList<Mode{Scale::DOUBLE, false}, Mode{Scale::HALF, false}, Mode{Scale::HALF, true}>;
	struct Settings {
		Mode mode;
	};
	struct Coefficients {
		Mode mode;
	};
	struct State {};
	static constexpr Coefficients coefficients(const Settings& s) { return {.mode = s.mode}; }
	template <Mode M>
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/, q31 x) {
		const q31 scaled = M.scale == Scale::HALF ? x >> 1 : x << 1;
		return M.negate ? -scaled : scaled;
	}
};

static_assert(moded_kernel<Voiced> && processor_kernel<Voiced> && detail::default_mode_listed<Voiced>);
static_assert(detail::first_mode_v<Voiced::Modes> == Voiced::Mode{});

static_assert(processor_kernel<Gain>);
static_assert(moded_kernel<Shaped>);
static_assert(processor_kernel<Shaped>);
static_assert(generator_kernel<Counter>);
static_assert(mixer_kernel<Add>);
static_assert(processor_kernel<Prepared>);
static_assert(processor_kernel<Ramp>);
static_assert(staged_kernel<Staged>);
static_assert(!kernel<CustomVerbs>);
static_assert(!moded_kernel<Gain>);
static_assert(std::same_as<kernel_block_t<Gain>, EmptyBlock>);
static_assert(std::same_as<kernel_block_t<Prepared>, Prepared::Block>);
static_assert(partial_step_advances_by_count<Ramp>);
static_assert(!partial_step_advances_by_count<Gain>);

// One arity per kernel: each reference kernel satisfies exactly one of the four.
static_assert(!generator_kernel<Gain> && !mixer_kernel<Gain> && !staged_kernel<Gain>);
static_assert(!processor_kernel<Counter> && !mixer_kernel<Counter> && !staged_kernel<Counter>);
static_assert(!generator_kernel<Add> && !processor_kernel<Add> && !staged_kernel<Add>);
static_assert(!generator_kernel<Staged> && !processor_kernel<Staged> && !mixer_kernel<Staged>);
static_assert(!mixer_kernel<Ramp>);
static_assert(kernel_base<TwoArities> && !kernel<TwoArities>);
static_assert(!kernel_base<BadPrepare> && !kernel<BadPrepare>);
static_assert(!kernel_base<NotPlain> && !kernel<NotPlain>);
static_assert(!kernel<TemplatePrepare>);
static_assert(!kernel<OverloadedPrepare>);
static_assert(!kernel<BlockNoPrepare>);
static_assert(!kernel<OverloadedPrepareNoBlock>);
static_assert(detail::declares_prepare<TemplatePrepare> && detail::declares_prepare<OverloadedPrepare>
              && detail::declares_prepare<MemberPrepare> && detail::declares_prepare<OverloadedPrepareNoBlock>);
static_assert(detail::declares_prepare<Prepared> && !detail::declares_prepare<Gain>);
static_assert(generator_kernel<VecPhase> && partial_step_advances_by_count<VecPhase>);
static_assert(!kernel<ScalarCount> && detail::steps_as_processor<ScalarCount> && !detail::steps_as_mixer<ScalarCount>);
static_assert(!kernel<StagedPrepare> && detail::processes_blocks<StagedPrepare>);
static_assert(!kernel<StereoVectorOverQ31> && detail::steps_as_processor<StereoVectorOverQ31>);
static_assert(!kernel<VectorOverFrames> && detail::steps_as_processor<VectorOverFrames>);
static_assert(!kernel<UnlistedDefaultMode> && detail::has_modes<UnlistedDefaultMode>);
static_assert(moded_kernel<ListedDefaultMode>);
static_assert(staged_kernel<ModedStaged> && moded_kernel<ModedStaged>);
static_assert(!generator_kernel<ModedStaged> && !processor_kernel<ModedStaged> && !mixer_kernel<ModedStaged>);
static_assert(!kernel<VecNoCount>);
static_assert(!kernel<VecGeneratorNoCount>);
static_assert(!kernel<NotPlainBlock>);
static_assert(!kernel<MemberPrepare>);
static_assert(!kernel<WrongStepReturn>);
static_assert(!kernel<WrongCoefficientsReturn>);
static_assert(!kernel<MemberStep>);
static_assert(!kernel<ModeMismatch> && !moded_kernel<ModeMismatch>);
static_assert(generator_kernel<ResetQuirk> && detail::declares_reset<ResetQuirk>);
static_assert(detail::declares_reset<MemberReset> && !kernel<MemberReset>);
static_assert(detail::declares_reset<ValueReset> && !kernel<ValueReset>);
static_assert(!detail::declares_reset<Counter>);

// The control form: a block-rate advance, and no other form.
static_assert(control_kernel<Fall> && kernel<Fall> && !moded_kernel<Fall>);
static_assert(control_kernel<ModedFall> && moded_kernel<ModedFall>);
static_assert(!generator_kernel<Fall> && !processor_kernel<Fall> && !mixer_kernel<Fall> && !staged_kernel<Fall>);
static_assert(!control_kernel<Counter> && !control_kernel<Gain> && !control_kernel<Add> && !control_kernel<Staged>);
static_assert(kernel_base<AdvanceAndStep> && !control_kernel<AdvanceAndStep> && !kernel<AdvanceAndStep>);
static_assert(kernel_base<AdvancePrepare> && !control_kernel<AdvancePrepare> && !kernel<AdvancePrepare>);
static_assert(kernel_base<AdvancePartialCount> && !control_kernel<AdvancePartialCount> && !kernel<AdvancePartialCount>);
static_assert(kernel_base<AdvanceVectorStep> && !control_kernel<AdvanceVectorStep> && !kernel<AdvanceVectorStep>);
static_assert(kernel_base<AdvanceIntCount> && !control_kernel<AdvanceIntCount> && !kernel<AdvanceIntCount>);
// A uint32_t count is the exact signature where uint32_t is std::size_t, and rejected wherever it is not.
static_assert(std::same_as<uint32_t, std::size_t> == control_kernel<AdvanceU32Count>);
// A template whose count deduces to std::size_t has the exact signature; nothing is converted.
static_assert(control_kernel<AdvanceDeducedCount>);
static_assert(kernel_base<AdvanceWrongReturn> && !control_kernel<AdvanceWrongReturn> && !kernel<AdvanceWrongReturn>);
static_assert(kernel_base<AdvanceFirstModeOnly> && !control_kernel<AdvanceFirstModeOnly>
              && !kernel<AdvanceFirstModeOnly>);

// The loop-state trait: a pointer to a data member of State, which a stepping kernel's step takes.
namespace lst = loop_state_test;

/// Declares kLoopState as an int, not a pointer to a member.
struct LoopStateNotAPointer : lst::SplitLoop {
	static constexpr int kLoopState = 0;
};

/// Declares kLoopState naming a member of another struct.
struct LoopStateOfAnotherStruct : lst::SplitLoop {
	static constexpr auto kLoopState = &lst::Loop::acc;
};

/// Declares kLoopState but steps the whole State.
struct WholeStepWithLoopState : lst::SplitWhole {
	static constexpr auto kLoopState = &State::loop;
};

/// A staged kernel declaring kLoopState: it has no step to give the loop state to.
struct StagedWithLoopState {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {
		q31 kept;    ///< Outside the loop.
		q31 carried; ///< Named as the loop's part.
	};
	static constexpr auto kLoopState = &State::carried;
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static void process(const Coefficients& /*c*/, State& /*s*/, Signal<q31> in, Buffer<q31> out) {
		for (std::size_t i = 0; i < out.size(); ++i) {
			out[i] = in[i];
		}
	}
};

static_assert(processor_kernel<lst::SplitWhole> && moded_kernel<lst::SplitWhole>);
static_assert(processor_kernel<lst::SplitLoop> && moded_kernel<lst::SplitLoop>);
static_assert(detail::declares_loop_state<lst::SplitLoop> && !detail::declares_loop_state<lst::SplitWhole>);
static_assert(std::same_as<loop_state_t<lst::SplitLoop>, lst::Loop>);
static_assert(std::same_as<loop_state_t<lst::SplitWhole>, lst::SplitCore::State>);
static_assert(!kernel<LoopStateNotAPointer>);
static_assert(!kernel<LoopStateOfAnotherStruct>);
static_assert(!kernel<WholeStepWithLoopState> && detail::steps_as_processor<lst::SplitWhole>);
static_assert(!kernel<StagedWithLoopState> && detail::processes_blocks<StagedWithLoopState>);

} // namespace

// clang-format off
describe kernel_concepts("kernel concepts", ${
	it("steps each reference kernel directly", _ {
		const EmptyBlock none{};

		Gain::State gain_state{};
		const Gain::Coefficients half = Gain::coefficients({.gain = 0x40000000}); // 0.5 in Q1.31
		expect(Gain::step(half, none, gain_state, 1000)).to_equal(500);

		Shaped::State shaped_state{};
		const Shaped::Coefficients shaped = Shaped::coefficients({.mode = Shaped::Mode::DOUBLE});
		expect(shaped.mode).to_equal(Shaped::Mode::DOUBLE);
		expect(Shaped::step<Shaped::Mode::HALF>(shaped, none, shaped_state, 100)).to_equal(50);
		expect(Shaped::step<Shaped::Mode::DOUBLE>(shaped, none, shaped_state, 100)).to_equal(200);

		Counter::State counter_state{};
		std::ignore = Counter::step({}, none, counter_state);
		expect(Counter::step({}, none, counter_state)).to_equal(2);

		Add::State add_state{};
		expect(Add::step({}, none, add_state, 3, 4)).to_equal(7);

		Prepared::State prepared_state{.offset = 10};
		const Prepared::Block block = Prepared::prepare({}, prepared_state, 4);
		expect(Prepared::step({}, block, prepared_state, 5)).to_equal(15);

		Ramp::State ramp_state{.value = Argon<q31>{0}};
		std::ignore = Ramp::step({}, none, ramp_state, Argon<q31>{100});
		std::ignore = Ramp::step({}, none, ramp_state, Argon<q31>{100}, ActiveLanes{.n = 3});
		std::array<q31, 4> lanes{};
		ramp_state.value.StoreTo(lanes.data());
		expect(lanes[0]).to_equal(7);
		expect(lanes[3]).to_equal(7);

		Staged::State staged_state{};
		const std::array<q31, 3> in{1, 2, 3};
		std::array<q31, 3> out{};
		Staged::process({}, staged_state, in, out);
		expect(out[0]).to_equal(3);
		expect(out[2]).to_equal(1);

		ModedStaged::State moded_staged_state{};
		ModedStaged::process<ModedStaged::Mode::NEGATE>({}, moded_staged_state, in, out);
		expect(out[0]).to_equal(-1);
		expect(out[2]).to_equal(-3);

		VecPhase::State phase_state{.phase = Argon<q31>{0}};
		std::ignore = VecPhase::step({}, none, phase_state);
		std::ignore = VecPhase::step({}, none, phase_state, ActiveLanes{.n = 2});
		phase_state.phase.StoreTo(lanes.data());
		expect(lanes[0]).to_equal(6);

		Fall::State fall_state{};
		expect(Fall::advance({.rate = 10}, fall_state, 3)).to_equal(ONE_Q31 - 30);
		expect(Fall::advance({.rate = ONE_Q31}, fall_state, 2)).to_equal(0);

		ModedFall::State moded_fall_state{.level = 5};
		expect(ModedFall::advance<ModedFall::Mode::HOLD>({}, moded_fall_state, 4)).to_equal(5);
		expect(ModedFall::advance<ModedFall::Mode::CLEAR>({}, moded_fall_state, 4)).to_equal(0);
	});

	it("dispatches a kernel with a structural mode to the step at that mode", _ {
		const std::array<Voiced::Mode, 3> modes{
		    {{Voiced::Scale::DOUBLE, false}, {Voiced::Scale::HALF, false}, {Voiced::Scale::HALF, true}}};
		const std::array<q31, 3> want{200, 50, -50};
		for (std::size_t i = 0; i < modes.size(); ++i) {
			const Voiced::Coefficients c = Voiced::coefficients({.mode = modes[i]});
			Voiced::State s{};
			q31 got = 0;
			kernel_ops::dispatch_mode<Voiced>(c, [&]<auto M>() {
				got = kernel_ops::step_at<Voiced, M>(c, EmptyBlock{}, s, 100);
			});
			expect(got).to_equal(want[i]);
		}
	});

	it("advances a control kernel and processes a staged kernel through kernel_ops at a mode", _ {
		Fall::State fall_state{};
		expect(kernel_ops::advance_at<Fall, kernel_ops::NoMode::NONE>({.rate = 10}, fall_state, 3))
		    .to_equal(ONE_Q31 - 30);

		ModedFall::State moded_fall_state{.level = 5};
		expect(kernel_ops::advance_at<ModedFall, ModedFall::Mode::HOLD>({}, moded_fall_state, 4)).to_equal(5);
		expect(kernel_ops::advance_at<ModedFall, ModedFall::Mode::CLEAR>({}, moded_fall_state, 4)).to_equal(0);

		const std::array<q31, 3> in{1, 2, 3};
		std::array<q31, 3> out{};
		Staged::State staged_state{};
		kernel_ops::process_at<Staged, kernel_ops::NoMode::NONE>({}, staged_state, in, out);
		expect(out[0]).to_equal(3);
		expect(out[2]).to_equal(1);

		ModedStaged::State moded_staged_state{};
		kernel_ops::process_at<ModedStaged, ModedStaged::Mode::NEGATE>({}, moded_staged_state, in, out);
		expect(out[0]).to_equal(-1);
		expect(out[2]).to_equal(-3);
	});

	it("steps a kernel declaring kLoopState on its State's loop member, through kernel_ops", _ {
		lst::SplitCore::State whole{};
		lst::SplitCore::State split{};
		expect(&kernel_ops::loop_state<lst::SplitLoop>(split)).to_equal(&split.loop);
		expect(&kernel_ops::loop_state<lst::SplitWhole>(whole)).to_equal(&whole);
		const auto c = lst::SplitLoop::coefficients({.mode = lst::Mode::XOR});
		const auto bw = kernel_ops::prepare_block<lst::SplitWhole>(c, whole, 4);
		const auto bs = kernel_ops::prepare_block<lst::SplitLoop>(c, split, 4);
		for (const q31 x : {1, -7, 1 << 30, 12345}) {
			const q31 want = kernel_ops::step_at<lst::SplitWhole, lst::Mode::XOR>(
			    c, bw, kernel_ops::loop_state<lst::SplitWhole>(whole), x);
			expect(kernel_ops::step_at<lst::SplitLoop, lst::Mode::XOR>(c, bs, kernel_ops::loop_state<lst::SplitLoop>(split),
			                                                           x))
			    .to_equal(want);
		}
		expect(std::memcmp(&split, &whole, sizeof whole)).to_equal(0);
	});
});

CPPSPEC_MAIN(kernel_concepts);
