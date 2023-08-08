#pragma once
#include <array>
#include <cstdint>

struct Colour {
	uint8_t r;
	uint8_t g;
	uint8_t b;

	static constexpr Colour fromArray(const uint8_t colour[3]) {
		return Colour{
		    colour[0],
		    colour[1],
		    colour[2],
		};
	}

	constexpr std::array<uint8_t, 3> toArray() { return {r, g, b}; }
};

constexpr Colour disabledColour = {255, 0, 0};
constexpr Colour groupEnabledColour = {0, 255, 0};
constexpr Colour enabledColour = {0, 255, 6};
constexpr Colour mutedColour = {255, 160, 0};
constexpr Colour midiCommandColour = {255, 80, 120};
constexpr Colour midiNoCommandColour = {50, 50, 50};
constexpr Colour selectedDrumColour = {30, 30, 10};
