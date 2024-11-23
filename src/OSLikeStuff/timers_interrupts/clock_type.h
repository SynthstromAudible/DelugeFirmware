//
// Created by Mark Adams on 2024-11-23.
//
#ifdef __cplusplus
extern "C" {
#endif

#include <math.h>
#define DELUGE_CLOCKS_PER 33330000
#define DELUGE_CLOCKS_PERf 33330000.
#define ONE_OVER_CLOCK (1 / DELUGE_CLOCKS_PERf)

typedef int64_t dTime;
#ifdef __cplusplus
} // end of extern C section

constexpr double rollTime = ((double)(UINT32_MAX) / DELUGE_CLOCKS_PERf);
class dClock {
	dTime time{0};

public:
	/// create a clock from seconds
	constexpr dClock(double t) { time = t * DELUGE_CLOCKS_PER; }
	constexpr dClock(dTime t) { time = t; }
	constexpr dClock(int t) { time = t; }
	constexpr dClock(float t) { time = t * DELUGE_CLOCKS_PER; }
	dClock() = default;
	constexpr dClock(uint32_t rolls, uint32_t ticks) { time = rolls * UINT32_MAX + ticks; }

	dClock average(const dClock& r) { return (time + r.time) / 2; }

	/// returns seconds
	constexpr operator double() { return (time * ONE_OVER_CLOCK); }
	/// returns ticks
	constexpr operator dTime() { return time; }
	constexpr dClock operator+(dClock r) { return time + r.time; }
	constexpr dClock operator-(dClock r) { return time - r.time; }
	constexpr dClock operator*(int r) { return time * r; }
	auto operator<=>(const dClock&) const = default;
	constexpr dClock& operator+=(const dClock& r) {
		time = time + r.time;
		return *this;
	}
};

#endif
