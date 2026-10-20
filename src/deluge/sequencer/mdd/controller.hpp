#pragma once
#include "snake.hpp"
namespace snake {
enum class Control {
	None = -1,
	Notes,
	Gates,
	Velocity,
	Custom,
	Random,
	Reset,
	RandomAll,
	ResetAll,
	Root,
	Scale,
	Map,
	Write,
	Range,
	Settings
};
inline constexpr std::array<Control, 10> controlPads = {
    Control::Notes, Control::Gates, Control::Velocity, Control::Custom,   Control::Random,
    Control::Reset, Control::Root,  Control::Map,      Control::Settings, Control::Write};
constexpr int depthPercent(int16_t depth) {
	int magnitude = depth < 0 ? -int(depth) : int(depth);
	return (depth < 0 ? -1 : 1) * ((magnitude * 100 + 16383) / 32767);
}
constexpr int32_t depthQ30(int16_t depth) {
	return int64_t(depthPercent(depth)) * 1073741824 / 100;
}
struct Route {
	int target = -1;
	int16_t depth = 32767;
};
struct Controller {
	Core core;
	Gestures gestures;
	int selectedStep = 0; // Runtime editor cursor; the cell data lives in the clip.
	int lane = 0, custom = 0, routeSlot = 0;
	Control held = Control::None;
	std::array<std::array<Route, 4>, 4> routes{};
	std::array<uint16_t, 7> low{}, high{127, 65535, 127, 65535, 65535, 65535, 65535};
	uint32_t quarterTicks = 24;
	bool needsInitialSpeed = true;

	// Keep the first six indices compatible with the existing OLED menu.
	struct Speed {
		unsigned numerator, denominator;
	};
	static constexpr std::array<Speed, 15> speeds = {{{1, 1},
	                                                  {1, 2},
	                                                  {1, 4},
	                                                  {1, 8},
	                                                  {1, 12},
	                                                  {1, 16},
	                                                  {1, 3},
	                                                  {1, 6},
	                                                  {2, 3},
	                                                  {1, 24},
	                                                  {3, 2},
	                                                  {3, 4},
	                                                  {3, 8},
	                                                  {3, 16},
	                                                  {3, 32}}};
	static constexpr std::array<const char*, 15> speedNames = {"1/4",  "1/8",  "1/16",  "1/32",  "1/32T",
	                                                           "1/64", "1/8T", "1/16T", "1/4T",  "1/64T",
	                                                           "1/4D", "1/8D", "1/16D", "1/32D", "1/64D"};
	// Display slow to fast without changing stored timing or legacy speed indices.
	static constexpr std::array<int, 15> speedMenuOrder = {10, 0, 11, 8, 1, 12, 6, 2, 13, 7, 3, 14, 4, 5, 9};
	bool engineValues(int engineA, int engineB) const {
		if (lane < 3)
			return false;
		bool found = false;
		for (const auto& route : routes[lane - 3]) {
			if (route.target < 0)
				continue;
			if (route.target != engineA && route.target != engineB)
				return false;
			found = true;
		}
		return found;
	}
	void setSpeed(Lane& l, int i) {
		i = std::clamp<int>(i, 0, speeds.size() - 1);
		unsigned n = quarterTicks * speeds[i].numerator, d = speeds[i].denominator, a = n, b = d;
		while (b) {
			auto r = a % b;
			a = b;
			b = r;
		}
		l.ticks = n / std::max(a, 1u);
		l.tickDenominator = d / std::max(a, 1u);
	}
	int speedIndex(const Lane& l) const {
		for (unsigned i = 0; i < speeds.size(); ++i)
			if (uint64_t(l.ticks) * speeds[i].denominator
			    == uint64_t(quarterTicks) * speeds[i].numerator * l.tickDenominator)
				return i;
		return 2;
	}
	void activateClock(uint32_t ticks) {
		syncClock(ticks);
		if (needsInitialSpeed) {
			for (auto& l : core.lanes)
				setSpeed(l, 2);
			needsInitialSpeed = false;
		}
	}

	// Bind default 1/16 timing to the song clock, retaining each lane's selected musical speed.
	void syncClock(uint32_t ticks) {
		ticks = std::max<uint32_t>(1, ticks);
		if (ticks == quarterTicks)
			return;
		std::array<int, 7> speeds;
		for (int i = 0; i < 7; ++i)
			speeds[i] = speedIndex(core.lanes[i]);
		quarterTicks = ticks;
		for (int i = 0; i < 7; ++i)
			setSpeed(core.lanes[i], speeds[i]);
	}

	Controller() {
		for (auto& l : core.lanes)
			l.ticks = 6;
		gestures.select(core.lanes[0]);
	}
	Controller(const Controller&) = delete;
	Controller& operator=(const Controller&) = delete;
	const char* title() const {
		if (held != Control::None) {
			static constexpr const char* names[] = {"Notes", "Gates",      "Velocity",  "Custom",       "Random",
			                                        "Reset", "Random All", "Reset All", "Root / Scale", "Scale",
			                                        "Map",   "Write",      "Range",     "MDD Snake"};
			return names[int(held)];
		}
		static constexpr const char* names[] = {"Notes",    "Gates",    "Velocity", "Custom 1",
		                                        "Custom 2", "Custom 3", "Custom 4"};
		return names[lane];
	}
	void choose(int n) {
		lane = n;
		gestures.select(core.lanes[lane]);
	}
	void down(Control c) {
		if (c == Control::None)
			return;
		gestures.cancel();
		held = c;
		if (c == Control::Notes || c == Control::Gates || c == Control::Velocity) {
			choose(int(c));
		}
	}
	void up() { held = Control::None; }
	void reset(int i) {
		if (i == 1) {
			core.ties = 0;
			core.overlap = 0;
			core.legato = false;
		}
		auto& v = core.lanes[i].values;
		v.fill(i == 0 ? 60 : i == 1 ? 32768 : i == 2 ? 100 : 0);
	}
	bool map(int slot, int target, int16_t depth) {
		if (lane < 3 || slot < 0 || slot >= 4 || target < -1)
			return false;
		routes[lane - 3][slot] = {target, depth};
		return true;
	}
	void back() {
		gestures.cancel();
		held = Control::None;
	}
};
} // namespace snake
