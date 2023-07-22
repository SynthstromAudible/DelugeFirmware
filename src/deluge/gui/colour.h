#pragma once
#include <cstdint>

struct Colour {
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

constexpr Colour disabledColour = {255, 0, 0};
constexpr Colour groupEnabledColour = {0, 255, 0};
constexpr Colour enabledColour = {0, 255, 6};
constexpr Colour mutedColour = {255, 160, 0};
constexpr Colour midiCommandColour = {255, 80, 120};
constexpr Colour midiNoCommandColour = {50, 50, 50};
constexpr Colour selectedDrumColour = {30, 30, 10};
