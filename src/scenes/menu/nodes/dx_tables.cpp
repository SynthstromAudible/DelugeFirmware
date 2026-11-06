#include "scenes/menu/nodes/dx_tables.h"

#include <algorithm>
#include <cstring>

namespace deluge::scenes::menu::dx {

namespace {

// `upper_limit_op_param` and `upper_limit_global_param` (`param.cpp:73-93`), inclusive.
constexpr std::array<uint8_t, kOperatorParams> kOperatorLimits{
    99, 99, 99, 99, 99, 99, 99, 99, // 0-7: envelope rates and levels
    99, 99, 99, 3,  3,              // 8-12: breakpoint, depths, curves
    7,  3,  7,                      // 13-15: rate scale, amp mod, velocity sensitivity
    99,                             // 16: level
    1,                              // 17: mode, ratio or fixed
    31, 99, 14,                     // 18-20: coarse, fine, detune
};
constexpr std::array<uint8_t, 18> kGlobalLimits{
    99, 99, 99, 99, 99, 99, 99, 99, // 126-133: pitch envelope
    31,                             // 134: algorithm
    7,                              // 135: feedback
    1,                              // 136: oscillator sync
    99, 99, 99, 99,                 // 137-140: LFO rate, delay, pitch depth, amp depth
    1,                              // 141: LFO sync
    4,                              // 142: LFO waveform
    7,                              // 143: pitch sensitivity
};

// `desc_global_long` (`param.cpp:274-277`).
constexpr std::array<std::string_view, 18> kGlobalTitles{
    "DX7 pitch R1",  "DX7 pitch R2",  "DX7 pitch R3", "DX7 pitch R4", "DX7 pitch l1",     "DX7 pitch l2",
    "DX7 pitch l3",  "DX7 pitch l4",  "algorithm",    "DX7 feedback", "DX7 osc Sync",     "DX7 LFO rate",
    "DX7 LFO delay", "DX7 LFO pitch", "DX7 LFO amp",  "DX7 LFO sync", "DX7 LFO waveform", "DX7 pitch sens",
};

struct Row {
	std::string_view name;
	int16_t index;
};

// `global_params.cpp:46-51`: a param index, or -N for operator N.
constexpr std::array<Row, kGlobalRows> kGlobalRowTable{{
    {"algorithm", 134},
    {"feedback", 135},
    {"sync", 136},
    {"operator 1", -1},
    {"operator 2", -2},
    {"operator 3", -3},
    {"operator 4", -4},
    {"operator 5", -5},
    {"operator 6", -6},
    {"pitch envelope", 6 * 21},
    {"lfo", 137},
}};

// `operator_params.cpp:45-53`: the param within the operator.
constexpr std::array<Row, kOperatorRows> kOperatorRowTable{{
    {"pitch", 18},
    {"level", 16},
    {"envelope", 0},
    {"velocity sense", 15},
    {"note scaling", 8},
    {"rate scaling", 13},
    {"ampmod", 14},
}};

constexpr std::array<std::string_view, 5> kCurves{"lin-", "exp-", "exp+", "lin+", "????"};
constexpr std::array<std::string_view, 6> kShapes{"tri", "saw down", "saw up", "square", "sin", "s-hold"};

size_t copy_out(std::string_view text, std::span<char> out) {
	const size_t n = std::min(text.size(), out.size());
	std::copy_n(text.begin(), n, out.begin());
	return n;
}

} // namespace

uint8_t upper_limit(int16_t param) {
	if (param >= 0 && param < kFirstGlobal) {
		return kOperatorLimits[param % kOperatorParams];
	}
	if (param >= kFirstGlobal && param - kFirstGlobal < std::ssize(kGlobalLimits)) {
		return kGlobalLimits[param - kFirstGlobal];
	}
	return 99;
}

size_t title(int16_t param, std::span<char> out) {
	if (param < 0) {
		return copy_out("random detune", out);
	}
	if (param < kFirstGlobal) {
		// "op" and the DX7's operator number, `6 - op`, then the group the param is in.
		const int32_t idx = param % kOperatorParams;
		const std::string_view group = idx < 8 ? "envelope" : idx < 13 ? "scaling" : idx < 16 ? "params" : "tune/level";
		char head[4]{'o', 'p', static_cast<char>('6' - param / kOperatorParams), ' '};
		size_t n = copy_out({head, sizeof head}, out);
		return n + copy_out(group, out.subspan(n));
	}
	if (param < kFirstGlobal + 8) {
		return copy_out("dx7 pitch env", out);
	}
	if (param < kFirstGlobal + std::ssize(kGlobalTitles)) {
		return copy_out(kGlobalTitles[param - kFirstGlobal], out);
	}
	return copy_out("DX7 PARAM", out);
}

std::string_view global_row(int32_t row) {
	return row >= 0 && row < kGlobalRows ? kGlobalRowTable[row].name : std::string_view{};
}

std::string_view operator_row(int32_t row) {
	return row >= 0 && row < kOperatorRows ? kOperatorRowTable[row].name : std::string_view{};
}

int16_t global_row_param(int32_t row) {
	return kGlobalRowTable[std::clamp<int32_t>(row, 0, kGlobalRows - 1)].index;
}

int16_t operator_row_param(int32_t op, int32_t row) {
	return static_cast<int16_t>(op * kOperatorParams
	                            + kOperatorRowTable[std::clamp<int32_t>(row, 0, kOperatorRows - 1)].index);
}

std::string_view curve_name(uint8_t value) {
	return kCurves[std::min<size_t>(value, kCurves.size() - 1)];
}

std::string_view lfo_shape(uint8_t value) {
	return kShapes[std::min<size_t>(value, kShapes.size() - 1)];
}

int16_t step_param(int16_t param, int32_t offset) {
	const int32_t next = param + offset;
	if (next < kRandomDetune) {
		return kLastParam;
	}
	if (next > kLastParam) {
		return kRandomDetune;
	}
	return static_cast<int16_t>(next);
}

std::optional<int16_t> step_operator(int16_t param, int32_t offset, std::span<const uint8_t, 156> params) {
	if (param < 0 || param > kFirstGlobal) {
		return std::nullopt;
	}
	const int32_t current = param / kOperatorParams;
	const auto on = [&](int32_t op) { return ((params[kOperatorSwitch] >> op) & 1) != 0; };
	// Legacy's `do ... while` over the operators. After its first step the operator is always one of 0-5, so a search
	// that has not stopped within seven steps is repeating itself and never will.
	int32_t next = current;
	for (int32_t steps = 0; steps < 7; steps++) {
		next += offset;
		if (next >= 6) {
			next = 0;
		}
		else if (next < 0) {
			next = 5;
		}
		if (on(next) || next == current) {
			return static_cast<int16_t>(param % kOperatorParams + kOperatorParams * next);
		}
	}
	return std::nullopt;
}

std::optional<Pad> param_pad(int16_t param) {
	int32_t x = -1;
	int32_t y = -1;
	if (param >= 0 && param < kFirstGlobal) {
		const int32_t op = param / kOperatorParams;
		const int32_t idx = param % kOperatorParams;
		y = 7 - op;
		if (idx < 8) {
			x = idx; // envelope rates and levels
		}
		else if (idx >= 16) {
			x = idx - 16 + 8; // level, mode, coarse, fine, detune
		}
		else if (idx >= 13) {
			x = 28 - idx; // velocity sensitivity, amp mod, rate scale, in that order across
		}
	}
	else if (param >= kFirstGlobal && param < kFirstGlobal + 11) {
		y = 1; // pitch envelope, algorithm, feedback, sync
		x = param - kFirstGlobal;
	}
	else if (param >= kFirstGlobal + 11 && param < kFirstGlobal + 18) {
		y = 0; // LFO rate, delay, pitch depth, amp depth, sync, waveform, pitch sensitivity
		x = param - kFirstGlobal - 11;
	}
	else if (param == kRandomDetune) {
		y = 0;
		x = 7;
	}
	if (x < 0 || y < 0) {
		return std::nullopt;
	}
	return Pad{static_cast<uint8_t>(x), static_cast<uint8_t>(y)};
}

int16_t column_param(int16_t current, int32_t op, bool on_page) {
	const int32_t level = op < 6 ? op * kOperatorParams + 16 : kFirstGlobal + 8; // output level, or the algorithm
	if (!on_page) {
		return static_cast<int16_t>(level);
	}
	if (current == level) {
		return static_cast<int16_t>(op < 6 ? op * kOperatorParams + 18 : kFirstGlobal + 9); // coarse, or feedback
	}
	constexpr int32_t kCarriesBelow = kFirstGlobal + 8; // the operators' params and the pitch envelope
	if (current >= 0 && current < kCarriesBelow) {
		const int32_t carried = op * kOperatorParams + current % kOperatorParams;
		if (carried != current && carried < kCarriesBelow) {
			return static_cast<int16_t>(carried);
		}
	}
	return static_cast<int16_t>(level);
}

std::optional<int16_t> pad_param(uint8_t x, uint8_t y) {
	constexpr uint8_t kMainColumns = 16;
	if (x >= kMainColumns) {
		return std::nullopt; // the sidebar
	}
	if (y > 1) {
		const int32_t op = 8 - y - 1;
		int32_t idx = 28 - x; // velocity sensitivity, amp mod, rate scale, from the right
		if (x < 8) {
			idx = x; // envelope rates and levels
		}
		else if (x < 13) {
			idx = x - 8 + 16; // level, mode, coarse, fine, detune
		}
		return static_cast<int16_t>(op * kOperatorParams + idx);
	}
	if (y == 1) {
		return x < 11 ? std::optional{static_cast<int16_t>(kFirstGlobal + x)} : std::nullopt;
	}
	if (x < 7) {
		return static_cast<int16_t>(kFirstGlobal + 11 + x);
	}
	return x == 7 ? std::optional{static_cast<int16_t>(kRandomDetune)} : std::nullopt;
}

} // namespace deluge::scenes::menu::dx
