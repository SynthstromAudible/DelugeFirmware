#pragma once
#include "controller.hpp"
#include <string_view>
namespace snake {
class Codec {
public:
	static constexpr size_t bytes = 1 + 8 + 3 + 7 * 53 + 16 * 4;
	using Text = std::array<char, bytes * 2 + 1>;
	static Text encode(const Controller& ui, bool enabled, uint64_t tick) {
		Text out{};
		size_t pos = 0;
		auto put = [&](uint64_t v, int n) {
			const char* hex = "0123456789abcdef";
			while (n--) {
				auto b = v & 255;
				out[pos++] = hex[b >> 4];
				out[pos++] = hex[b & 15];
				v >>= 8;
			}
		};
		put(enabled, 1);
		put(tick, 8);
		put(ui.lane, 1);
		put(ui.custom, 1);
		// Reuse the reserved byte for native clock resolution (3 * 2^m ticks/quarter).
		uint32_t quarter = ui.quarterTicks;
		unsigned magnitude = 0;
		while (quarter > 3 && !(quarter & 1)) {
			quarter >>= 1;
			++magnitude;
		}
		put(quarter == 3 && magnitude < 16 ? magnitude + 1 : 0, 1);
		for (int i = 0; i < 7; ++i) {
			const auto& l = ui.core.lanes[i];
			for (auto v : l.values)
				put(v, 2);
			put(l.muted, 2);
			put(l.length, 1);
			put(l.shift, 1);
			put(l.ticks, 4);
			put(l.seed, 4);
			put(int(l.direction), 1);
			put(int(l.shape), 1);
			put(l.held, 2);
			put(ui.low[i], 2);
			put(ui.high[i], 2);
			put(l.tickDenominator, 1);
		}
		for (auto& list : ui.routes)
			for (auto& r : list) {
				put(r.target + 1, 2);
				put(uint16_t(r.depth), 2);
			}
		return out;
	}
	static bool decode(std::string_view text, Controller& ui, bool& enabled, uint64_t& tick, int targetLimit) {
		if (text.size() != bytes * 2)
			return false;
		for (char c : text)
			if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
				return false;
		size_t pos = 0;
		auto get = [&](int n) {
			uint64_t v = 0;
			for (int i = 0; i < n; ++i) {
				auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
				auto b = (digit(text[pos]) << 4) | digit(text[pos + 1]);
				pos += 2;
				v |= uint64_t(b) << (i * 8);
			}
			return v;
		};
		Controller candidate;
		auto mode = get(1);
		auto clock = get(8);
		int lane = get(1), custom = get(1);
		auto resolution = get(1);
		if (resolution > 16)
			return false;
		if (mode > 1 || lane > 6 || custom > 3 || clock > (UINT64_MAX / 32))
			return false;
		for (int i = 0; i < 7; ++i) {
			auto& l = candidate.core.lanes[i];
			for (auto& v : l.values) {
				v = get(2);
				if ((i == 0 || i == 2) && v > 127)
					return false;
			}
			l.muted = get(2);
			l.length = get(1);
			l.shift = get(1);
			l.ticks = get(4);
			l.seed = get(4);
			l.direction = Direction(get(1));
			l.shape = Shape(get(1));
			l.held = get(2);
			candidate.low[i] = get(2);
			candidate.high[i] = get(2);
			l.tickDenominator = std::max<uint64_t>(1, get(1));
			if (l.length < 1 || l.length > 16 || l.shift > 15 || !l.ticks || l.ticks > 65535 || l.tickDenominator > 32
			    || int(l.direction) > 3 || int(l.shape) > 4 || candidate.low[i] > candidate.high[i]
			    || ((i == 0 || i == 2) && candidate.high[i] > 127))
				return false;
		}
		for (auto& list : candidate.routes)
			for (auto& r : list) {
				r.target = int(get(2)) - 1;
				r.depth = int16_t(get(2));
				if (r.target >= targetLimit || r.depth == -32768)
					return false;
			}
		ui.quarterTicks = resolution ? 3u << (resolution - 1) : ui.quarterTicks;
		ui.core = candidate.core;
		ui.low = candidate.low;
		ui.high = candidate.high;
		ui.routes = candidate.routes;
		ui.custom = custom;
		ui.choose(lane);
		ui.back();
		enabled = mode;
		tick = clock;
		return true;
	}
};
} // namespace snake
