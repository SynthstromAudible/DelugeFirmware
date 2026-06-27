//
// Created by Mark Adams on 2024-05-10.
//

#ifndef DELUGE_TIMER_MOCKS_H
#define DELUGE_TIMER_MOCKS_H

#include <cstdint>

extern "C" {
void passMockTime(double seconds);
// Mocked free-running tick counter (DELUGE_CLOCKS_PER rate). Each read auto-advances mock time a hair so
// scheduler idle-polling / yield loops make progress. deluge_clock_monotonic() is backed by this.
uint32_t getTimerValue(int timerNo);
}

#endif // DELUGE_TIMER_MOCKS_H