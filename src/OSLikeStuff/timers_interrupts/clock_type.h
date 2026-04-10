//
// Created by Mark Adams on 2024-11-23.
//
#ifdef __cplusplus
extern "C" {
#endif

#include <math.h>
#include <stdint.h>
#define DELUGE_CLOCKS_PER 33330000
#define DELUGE_CLOCKS_PERf 33330000.
#define ONE_OVER_CLOCK (1 / DELUGE_CLOCKS_PERf)

typedef int64_t dTime;
#ifdef __cplusplus
} // end of extern C section

constexpr double rollTime = ((double)(UINT32_MAX) / DELUGE_CLOCKS_PERf);
class Time {
	dTime time{0};

public:
	/// create a clock from seconds
	constexpr Time(double t) { time = t * DELUGE_CLOCKS_PER; }
	constexpr Time(dTime t) { time = t; }
	constexpr Time(int t) { time = t; }
	constexpr Time(float t) { time = t * DELUGE_CLOCKS_PER; }
	Time() = default;
	constexpr Time(uint32_t rolls, uint32_t ticks) { time = rolls * UINT32_MAX + ticks; }

	Time average(const Time& r) { return (time + r.time) / 2; }

	/// returns seconds
	constexpr operator double() { return (time * ONE_OVER_CLOCK); }
	/// returns ticks
	constexpr operator dTime() { return time; }
	constexpr Time operator+(Time r) { return time + r.time; }
	constexpr Time operator-(Time r) { return time - r.time; }
	constexpr Time operator/(Time r) { return time / r.time; }

	constexpr Time operator*(int r) { return time * r; }
	constexpr Time operator*(double r) { return time * r; }
	constexpr Time operator*(float r) { return time * r; }
	auto operator<=>(const Time&) const = default;
	constexpr Time& operator+=(const Time& r) {
		time = time + r.time;
		return *this;
	}
	constexpr Time& operator*=(double r) {
		time = time * r;
		return *this;
	}
};

#endif
