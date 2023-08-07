#pragma once
#include "util/const_functions.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <stdint.h>

class Colour {
public:
	using channel_type = uint8_t;
	static constexpr auto channel_max = std::numeric_limits<channel_type>::max();

	channel_type r = 0;
	channel_type g = 0;
	channel_type b = 0;

	static constexpr Colour monochrome(uint8_t brightness) { return Colour{brightness, brightness, brightness}; }
	static Colour fromHue(int32_t hue);
	static Colour fromHuePastel(int32_t hue);

	[[nodiscard]] Colour forTail() const {
		uint32_t averageBrightness = ((uint32_t)r + g + b);
		return transform([averageBrightness](channel_type channel) { //<
			return (((int32_t)channel * 21 + averageBrightness) * 157) >> 14;
		});
	}

	[[nodiscard]] Colour forBlur() const {
		uint32_t averageBrightness = (uint32_t)r * 5 + g * 9 + b * 9;
		return transform([averageBrightness](channel_type channel) { //<
			return ((uint32_t)channel * 5 + averageBrightness) >> 5;
		});
	}

	static Colour average(const Colour& colourA, const Colour& colourB) {
		return Colour::transform2(colourA, colourB, [](channel_type channelA, channel_type channelB) -> channel_type {
			auto average = (static_cast<uint32_t>(channelA) + static_cast<uint32_t>(channelB)) / 2;
			return std::clamp<uint32_t>(average, 0, channel_max);
		});
	}

	[[nodiscard]] constexpr Colour dim(uint8_t level = 1) const {
		return Colour{
		    channel_type(r >> level),
		    channel_type(g >> level),
		    channel_type(b >> level),
		};
	}

	[[nodiscard]] Colour dull() const {
		return transform([](channel_type channel) {
			if (channel >= 64) {
				return 50;
			}
			return 5;
		});
	}

	[[nodiscard]] Colour greyOut(int32_t proportion) const {
		uint32_t totalColour = (uint32_t)r + g + b; // max 765

		return transform([totalColour, proportion](channel_type channel) -> uint8_t {
			uint32_t newColour = rshift_round(
			    (uint32_t)channel * (uint32_t)(0x808080 - proportion) + (totalColour * (proportion >> 5)), 23);
			return std::clamp<uint32_t>(newColour, 0, channel_max);
		});
	}

	/**
	 * @brief Generate a new Colour made from blending two source colours
	 *
	 * @param sourceA The first colour
	 * @param sourceB The second colour
	 * @param index The blend amount. You can think of this as a slider from one colour to the other
	 * @return Colour The new blended colour
	 */
	static Colour blend(const Colour& sourceA, const Colour& sourceB, uint16_t index) {
		return {
		    blendChannel(sourceA.r, sourceB.r, index),
		    blendChannel(sourceA.g, sourceB.g, index),
		    blendChannel(sourceA.b, sourceB.b, index),
		};
	}

	static Colour blend2(const Colour& sourceA, const Colour& sourceB, uint16_t indexA, uint16_t indexB) {
		return {
		    blendChannel2(sourceA.r, sourceB.r, indexA, indexB),
		    blendChannel2(sourceA.g, sourceB.g, indexA, indexB),
		    blendChannel2(sourceA.b, sourceB.b, indexA, indexB),
		};
	}

	uint8_t& operator[](size_t idx) {
		switch (idx) {
		case 0:
			return r;
		case 1:
			return g;
		case 2:
			return b;
		}
	}

	static constexpr size_t size() { return 3; }

	Colour transform(const std::function<channel_type(channel_type)>& transformFn) const {
		return Colour{transformFn(r), transformFn(g), transformFn(b)};
	}

	static Colour transform2(const Colour& colourA, const Colour& colourB,
	                         const std::function<channel_type(channel_type, channel_type)>& transformFn) {
		return Colour{transformFn(colourA.r, colourB.r), transformFn(colourA.g, colourB.g),
		              transformFn(colourA.b, colourB.b)};
	}

private:
	static channel_type blendChannel(uint32_t channelA, uint32_t channelB, uint16_t index) {
		return blendChannel2(channelA, channelB, index, (std::numeric_limits<uint16_t>::max() - index + 1));
	}

	static channel_type blendChannel2(uint32_t channelA, uint32_t channelB, uint16_t indexA, uint16_t indexB) {
		uint32_t newColour = rshift_round(channelA * indexA, 16) + rshift_round(channelB * indexB, 16);
		return std::clamp<uint32_t>(newColour, 0, channel_max);
	}
};

namespace colours {

// Standard pallette
constexpr Colour black = {0, 0, 0};
constexpr Colour grey = {7, 7, 7};
constexpr Colour light_grey = {160, 160, 160};
constexpr Colour white_full = {255, 255, 255};
constexpr Colour white = {128, 128, 128};
constexpr Colour red = {255, 0, 0};
constexpr Colour red_alt = {255, 1, 0};
constexpr Colour red_dim = {128, 0, 0};
constexpr Colour red_dull = {60, 15, 15};
constexpr Colour orange = {255, 128, 0};
constexpr Colour yellow_full = {255, 255, 0};
constexpr Colour yellow = {255, 160, 0};
constexpr Colour lime = {128, 255, 0};
constexpr Colour green = {0, 255, 0};
constexpr Colour turquoise = {0, 255, 128};
constexpr Colour cyan = {0, 128, 128};
constexpr Colour darkblue = {0, 128, 255};
constexpr Colour blue = {0, 0, 255};
constexpr Colour purple = {128, 0, 255};
constexpr Colour magenta = {128, 0, 128};
constexpr Colour magenta_dull = {60, 15, 60};
constexpr Colour pink_full = {255, 128, 128};
constexpr Colour pink = {255, 44, 50};
constexpr Colour amber = {255, 48, 0};

namespace kelly {
constexpr Colour vivid_yellow = {255, 179, 0};
constexpr Colour strong_purple = {128, 62, 117};
constexpr Colour vivid_orange = {255, 104, 0};
constexpr Colour very_light_blue = {166, 189, 215};
constexpr Colour vivid_red = {193, 0, 32};
constexpr Colour grayish_yellow = {206, 162, 98};
constexpr Colour medium_gray = {129, 112, 102};

// these aren't good for people with defective colour vision:
constexpr Colour vivid_green = {0, 125, 52};
constexpr Colour strong_purplish_pink = {246, 118, 142};
constexpr Colour strong_blue = {0, 83, 138};
constexpr Colour strong_yellowish_pink = {255, 122, 92};
constexpr Colour strong_violet = {83, 55, 122};
constexpr Colour vivid_orange_yellow = {255, 142, 0};
constexpr Colour strong_purplish_red = {179, 40, 81};
constexpr Colour vivid_greenish_yellow = {244, 200, 0};
constexpr Colour strong_reddish_brown = {127, 24, 13};
constexpr Colour vivid_yellowish_green = {147, 170, 0};
constexpr Colour deep_yellowish_brown = {89, 51, 21};
constexpr Colour vivid_reddish_orange = {241, 58, 19};
constexpr Colour dark_olive_green = {35, 44, 22};
} // namespace kelly

namespace wad {
constexpr Colour black = {0, 0, 0};
constexpr Colour dark_gray = {87, 87, 87};
constexpr Colour red = {173, 35, 35};
constexpr Colour blue = {42, 75, 215};
constexpr Colour green = {29, 105, 20};
constexpr Colour brown = {129, 74, 25};
constexpr Colour purple = {129, 38, 192};
constexpr Colour light_gray = {160, 160, 160};
constexpr Colour light_green = {129, 197, 122};
constexpr Colour light_blue = {157, 175, 255};
constexpr Colour cyan = {41, 208, 208};
constexpr Colour orange = {255, 146, 51};
constexpr Colour yellow = {255, 238, 51};
constexpr Colour tan = {233, 222, 187};
constexpr Colour pink = {255, 205, 243};
constexpr Colour white = {255, 255, 255};

} // namespace wad

// Custom
constexpr Colour disabled = colours::red;
constexpr Colour group_enabled = colours::green;
constexpr Colour enabled = {0, 255, 6};
constexpr Colour muted = colours::yellow;
constexpr Colour midi_command = {255, 80, 120};
constexpr Colour midi_no_command = {50, 50, 50};
constexpr Colour selected_drum = {30, 30, 10};
} // namespace colours
