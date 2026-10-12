#pragma once
#include "held_notes.hpp"
#include "snake.hpp"
namespace snake {
// Warm up one cycle, capture the next, and drain through a third cycle. This
// merges ties crossing the loop boundary into a single wrapping stock note.
template <class Emit, class Service, class Quantize>
bool bakeHeld(Core& core, uint32_t length, Emit emit, Service service, Quantize quantize) {
	if (!length || uint64_t(length) * 3 > 1048576)
		return false;
	struct Pending {
		bool active = false;
		uint8_t pitch = 0;
		uint64_t start = 0;
		Frame frame{};
	};
	std::array<Pending, 16> pending{};
	HeldNotes held;
	bool ok = true;
	uint32_t emitted = 0;
	uint64_t tick = 0;
	Frame frame;
	auto off = [&](uint8_t pitch) {
		for (auto& p : pending)
			if (p.active && p.pitch == pitch) {
				if (p.start >= length && p.start < uint64_t(length) * 2) {
					ok = ok
					     && emit(uint32_t(p.start - length), uint32_t(std::min<uint64_t>(tick - p.start, length)),
					             p.frame);
					++emitted;
				}
				p.active = false;
				return;
			}
	};
	auto on = [&](uint8_t pitch, uint8_t) {
		for (auto& p : pending)
			if (!p.active) {
				p = {true, pitch, tick, frame};
				return;
			}
		ok = false;
	};
	for (tick = 0; tick < uint64_t(length) * 3 && ok; ++tick) {
		if (!(tick & 31u))
			service();
		const uint64_t local = tick % length;
		frame = core.advance(local);
		// WRITE freezes one clip cycle; the following boundary wraps to zero,
		// even if independent lane periods do not divide the clip length.
		const auto& notes = core.lanes[0];
		const uint64_t n = std::max<uint32_t>(1, notes.ticks), d = std::max<unsigned>(1, notes.tickDenominator);
		uint64_t next = std::min<uint64_t>(length, ((local * d / n + 1) * n + d - 1) / d);
		const uint64_t wrapped = next == length ? 0 : next;
		const auto nc = notes.cell(wrapped), gc = core.lanes[1].cell(wrapped), vc = core.lanes[2].cell(wrapped);
		if ((core.ties & (1u << gc)) && !(notes.muted & (1u << nc)) && !(core.lanes[1].muted & (1u << gc))
		    && !(core.lanes[2].muted & (1u << vc)) && core.lanes[1].values[gc] && core.lanes[2].values[vc])
			frame.duration = std::max<uint32_t>(frame.duration, next - local);
		frame.note = quantize(frame.note);
		held.advance(frame, tick, on, off);
	}
	if (!ok)
		return false;
	// An all-tie cycle never retriggers during capture: write one sustained note.
	if (!emitted)
		for (auto& p : pending)
			if (p.active && p.start < length && p.start + length <= tick) {
				ok = emit(uint32_t(p.start % length), length, p.frame);
				++emitted;
				break;
			}
	held.stop(off);
	return ok;
}

// Render one stock clip loop at native tick resolution without touching live lane state.
// Emit monophonic notes, cutting the previous gate at the next trigger and loop end.
template <class Emit, class Service = decltype([] {}), class Quantize = decltype([](uint8_t n) { return n; })>
bool bake(Core core, uint32_t length, Emit emit, Service service = {}, Quantize quantize = {}) {
	if (core.ties || core.overlap || core.legato)
		return bakeHeld(core, length, emit, service, quantize);
	if (!length)
		return false;
	const auto& lane = core.lanes[0];
	const uint64_t numerator = std::max<uint32_t>(1, lane.ticks);
	const uint64_t denominator = std::max<unsigned>(1, lane.tickDenominator);
	bool pending = false;
	Frame previous;
	uint32_t start = 0;
	uint64_t tick = 0, step = 0;
	uint32_t iterations = 0;
	while (tick < length) {
		// Bound work even when every step is muted and emit is never called.
		if (iterations >= 1048576)
			return false;
		// Let the firmware service audio during an expensive offline WRITE.
		if ((iterations++ & 31u) == 0)
			service();
		auto frame = core.advance(tick);
		if (frame.trigger) {
			if (pending && !emit(start, std::min<uint32_t>(previous.duration, tick - start), previous))
				return false;
			previous = frame;
			start = tick;
			pending = true;
		}
		step = tick * denominator / numerator + 1;
		tick = std::max<uint64_t>(tick + 1, (step * numerator + denominator - 1) / denominator);
	}
	return !pending || emit(start, std::min(previous.duration, length - start), previous);
}
} // namespace snake
