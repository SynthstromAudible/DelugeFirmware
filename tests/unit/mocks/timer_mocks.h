//
// Created by Mark Adams on 2024-05-10.
//

#ifndef DELUGE_TIMER_MOCKS_H
#define DELUGE_TIMER_MOCKS_H

#include <cstdint>

extern "C" {
void passMockTime(double seconds);
uint32_t getTimerValue(int timerNo);
}

#endif // DELUGE_TIMER_MOCKS_H
