#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
namespace snake {
enum class Direction { Forward, Reverse, Pendulum, Random };
enum class Shape { Rows, Columns, SnakeRows, SnakeColumns, Spiral };
struct Lane {
	std::array<uint16_t, 16> values{};
	uint16_t muted = 0;
	uint8_t length = 16, shift = 0;
	uint32_t ticks = 24, seed = 1;
	Direction direction = Direction::Forward;
	Shape shape = Shape::Rows;
	uint16_t held = 0;
	uint8_t tickDenominator = 1;
	uint8_t cell(uint64_t tick) const {
		static constexpr uint8_t paths[5][16] = {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
		                                         {0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15},
		                                         {0, 1, 2, 3, 7, 6, 5, 4, 8, 9, 10, 11, 15, 14, 13, 12},
		                                         {0, 4, 8, 12, 13, 9, 5, 1, 2, 6, 10, 14, 15, 11, 7, 3},
		                                         {0, 1, 2, 3, 7, 11, 15, 14, 13, 12, 8, 4, 5, 6, 10, 9}};
		auto n = std::clamp<int>(length, 1, 16);
		auto k = tick * std::max<unsigned>(tickDenominator, 1) / std::max<uint32_t>(ticks, 1);
		uint64_t p = k % n;
		if (direction == Direction::Reverse)
			p = n - 1 - p;
		if (direction == Direction::Pendulum && n > 1) {
			p = k % (2 * n - 2);
			if (p >= unsigned(n))
				p = 2 * n - 2 - p;
		}
		if (direction == Direction::Random) {
			auto h = k + seed;
			h ^= h >> 30;
			h *= 0xbf58476d1ce4e5b9ULL;
			h ^= h >> 27;
			h *= 0x94d049bb133111ebULL;
			h ^= h >> 31;
			p = h % n;
		}
		return paths[std::clamp<int>(int(shape), 0, 4)][(p + shift) % 16];
	}
	uint16_t sample(uint64_t tick) {
		auto c = cell(tick);
		if (!(muted & (1u << c)))
			held = values[c];
		return held;
	}
	void randomize(uint16_t lo, uint16_t hi) {
		if (lo > hi)
			std::swap(lo, hi);
		for (auto& v : values) {
			seed = seed * 1664525u + 1013904223u;
			v = lo + seed % (uint32_t(hi) - lo + 1);
		}
	}
};
// Internal unipolar storage; these helpers do not assume a Deluge parameter format.
constexpr uint16_t fromPercent(int percent) {
	return uint16_t((std::clamp(percent, 0, 100) * 65535u + 50) / 100);
}
constexpr int percent(uint16_t raw) {
	return (uint32_t(raw) * 100 + 32767) / 65535;
}
constexpr int32_t normalizedQ31(uint16_t raw) {
	return int32_t(uint64_t(raw) * 2147483647 / 65535);
}
enum class Scale { Chromatic, Major, Minor, MajorPentatonic };
inline uint8_t quantize(int note, int root, Scale scale) {
	note = std::clamp(note, 0, 127);
	root = std::clamp(root, 0, 11);
	static constexpr uint16_t masks[] = {0xfff, 0xab5, 0x5ad, 0x295};
	auto allowed = [&](int n) {
		return n >= 0 && n <= 127 && (masks[std::clamp(int(scale), 0, 3)] & (1u << ((n - root + 120) % 12)));
	};
	for (int distance = 0; distance < 12; ++distance) {
		if (allowed(note - distance))
			return note - distance;
		if (allowed(note + distance))
			return note + distance;
	}
	return note;
}
struct Frame {
	bool tie = false;
	uint32_t overlap = 0;
	bool trigger = false;
	uint8_t note = 60, velocity = 100;
	uint32_t duration = 1;
	std::array<uint16_t, 4> custom{};
};
struct Core {
	// 0 Notes, 1 Gates, 2 Velocity, 3..6 Custom 1..4
	std::array<Lane, 7> lanes{};
	uint16_t ties = 0;    // Gate-cell tie bits; no retrigger while a note is held.
	bool legato = false;  // Minimum one-tick overlap when percentage is zero.
	uint16_t overlap = 0; // Fraction of a Notes interval, 0..65535.
	int root = 0;
	Scale scale = Scale::Chromatic;
	Core() {
		lanes[0].values.fill(60);
		lanes[1].values.fill(32768);
		lanes[2].values.fill(100);
		for (int i = 0; i < 7; ++i)
			lanes[i].seed = i + 1;
	}
	Frame advance(uint64_t tick) {
		Frame f;
		auto& notes = lanes[0];
		auto nc = notes.cell(tick);
		auto gc = lanes[1].cell(tick);
		auto vc = lanes[2].cell(tick);
		f.note = quantize(notes.values[nc], root, scale);
		f.velocity = std::min<uint16_t>(127, lanes[2].values[vc]);
		const uint64_t denominator = std::max<unsigned>(1, notes.tickDenominator),
		               numerator = std::max<uint32_t>(1, notes.ticks);
		const uint64_t step = tick * denominator / numerator;
		const uint64_t nextBoundary = ((step + 1) * numerator + denominator - 1) / denominator;
		const uint64_t interval = std::max<uint64_t>(1, nextBoundary - tick);
		f.duration = std::max<uint32_t>(1, uint64_t(lanes[1].values[gc]) * interval / 65535);
		f.trigger = (tick == 0 || step != (tick - 1) * denominator / numerator) && !(notes.muted & (1u << nc))
		            && !(lanes[1].muted & (1u << gc)) && !(lanes[2].muted & (1u << vc)) && lanes[1].values[gc] > 0
		            && f.velocity > 0;
		f.tie = (ties & (1u << gc)) != 0;
		f.overlap = overlap ? std::max<uint64_t>(1, uint64_t(overlap) * interval / 65535) : legato ? 1 : 0;
		// Any nonzero Overlap holds through the following note boundary,
		// then overlaps by the chosen fraction, irrespective of a short gate.
		if (f.overlap)
			f.duration = interval;
		// Keep a note alive up to a following tied boundary, even at a short gate.
		const auto nextNc = notes.cell(nextBoundary);
		const auto nextGc = lanes[1].cell(nextBoundary);
		const auto nextVc = lanes[2].cell(nextBoundary);
		if ((ties & (1u << nextGc)) && !(notes.muted & (1u << nextNc)) && !(lanes[1].muted & (1u << nextGc))
		    && !(lanes[2].muted & (1u << nextVc)) && lanes[1].values[nextGc] && lanes[2].values[nextVc])
			f.duration = interval;
		for (int i = 0; i < 4; ++i) {
			f.custom[i] = lanes[i + 3].sample(tick);
		}
		return f;
	}
	void restart() {
		for (auto& lane : lanes)
			lane.held = 0;
	}
};
// Call once per transport tick. Stop/restart sends note-off before reset.
struct Player {
	Core core;
	bool sounding = false;
	uint8_t note = 0;
	uint64_t offAt = 0;
	template <class Sink>
	void stop(Sink& sink) {
		if (sounding)
			sink.noteOff(note);
		sounding = false;
		core.restart();
	}
	template <class Sink>
	void tick(uint64_t t, Sink& sink) {
		if (sounding && t >= offAt) {
			sink.noteOff(note);
			sounding = false;
		}
		auto f = core.advance(t);
		for (int i = 0; i < 4; ++i)
			sink.custom(i, f.custom[i]);
		if (f.trigger) {
			if (sounding)
				sink.noteOff(note);
			note = f.note;
			sink.noteOn(note, f.velocity);
			sounding = true;
			offAt = t + f.duration;
		}
	}
};
// UI-independent gesture interpreter. Monotonic milliseconds; one editing pad.
struct Gestures {
	Lane* lane = nullptr;
	int held = -1;
	uint64_t downAt = 0;
	bool consumed = false, lengthChord = false;
	static constexpr uint64_t holdMs = 350;
	void cancel() {
		held = -1;
		consumed = false;
		lengthChord = false;
	}
	void select(Lane& l) {
		cancel();
		lane = &l;
	}
	void down(int pad, uint64_t now) {
		if (!lane || pad < 0 || pad > 15)
			return;
		if (held == 0 && pad != 0) {
			lane->length = pad + 1;
			consumed = true;
			lengthChord = true;
			return;
		}
		if (held < 0) {
			held = pad;
			downAt = now;
			consumed = false;
			lengthChord = false;
		}
	}
	bool editing(uint64_t now) const { return held >= 0 && !lengthChord && now - downAt >= holdMs; }
	void edit(int delta, uint64_t now, uint16_t maximum) {
		if (!lane || !editing(now))
			return;
		lane->values[held] = std::clamp<int>(int(lane->values[held]) + delta, 0, maximum);
		consumed = true;
	}
	void up(int pad, uint64_t now) {
		if (!lane || pad != held)
			return;
		if (!consumed && !lengthChord && now - downAt < holdMs)
			lane->muted ^= 1u << pad;
		cancel();
	}
};
} // namespace snake
