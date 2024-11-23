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

typedef struct dTime {
	uint16_t rolls; // the number of full rollovers (roughly 128 seconds)
	uint32_t ticks; // the number of ticks (1/330_000 seconds)
} dTime;
#ifdef __cplusplus
} // end of extern C section

constexpr double rollTime = ((double)(UINT32_MAX) / DELUGE_CLOCKS_PERf);
class dClock {
	dTime time{0, 0};

public:
	/// create a clock from seconds
	constexpr dClock(double t) {
		float rolls = floor(t / rollTime);
		float remainder = t - rolls * rollTime;
		uint32_t ticks = (uint32_t)((remainder)*DELUGE_CLOCKS_PERf);
		time = {(uint16_t)rolls, ticks};
	}
	constexpr dClock(uint16_t rolls, uint32_t ticks) { time = {rolls, ticks}; }

	/// returns seconds
	constexpr operator double() { return (time.rolls * rollTime + time.ticks * ONE_OVER_CLOCK); }

	constexpr dClock operator+(dClock r) {
		uint32_t ticks = time.ticks + r.time.ticks;
		uint16_t rolls = time.rolls + r.time.rolls;
		if (ticks < time.ticks) {
			rolls += 1;
		}
		return {rolls, ticks};
	}

	constexpr dTime toTime() { return time; }
};

#endif
