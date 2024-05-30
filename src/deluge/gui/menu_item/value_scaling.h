#pragma once

#include <cstdint>

// These are in non-member functions for ease of testing. The param
// classes pulls in a lot of stuff...

int32_t computeCurrentValueForUnpatched(int32_t value);
int32_t computeFinalValueForUnpatched(int32_t value);

int32_t computeCurrentValueForPan(int32_t value);
int32_t computeFinalValueForPan(int32_t value);

int32_t computeCurrentValueForCompressor(int32_t value);
int32_t computeFinalValueForCompressor(int32_t value);

int32_t computeCurrentValueForInteger(int32_t value);
int32_t computeFinalValueForInteger(int32_t value);

int32_t computeCurrentValueForArpGate(int32_t value);
int32_t computeFinalValueForArpGate(int32_t value);

int32_t computeCurrentValueForArpRatchet(int32_t value);
int32_t computeFinalValueForArpRatchet(int32_t value);

int32_t computeCurrentValueForArpRatchetProbability(int32_t value);
int32_t computeFinalValueForArpRatchetProbability(int32_t value);

int32_t computeCurrentValueForArpRhythm(int32_t value);
int32_t computeFinalValueForArpRhythm(int32_t value);

int32_t computeCurrentValueForArpSeqLength(int32_t value);
int32_t computeFinalValueForArpSeqLength(int32_t value);
