#pragma once
#include "util/const_functions.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>

class RGB {
public:
	using channel_type = uint8_t;
	static constexpr auto channel_max = std::numeric_limits<channel_type>::max();

	channel_type r = 0;
	channel_type g = 0;
	channel_type b = 0;

	static constexpr RGB monochrome(uint8_t lightness) { return RGB{lightness, lightness, lightness}; }
	static RGB fromHue(int32_t hue);
	static RGB fromHuePastel(int32_t hue);

	[[nodiscard]] RGB forTail() const {
		uint32_t averageBrightness = ((uint32_t)r + g + b);
		return transform([averageBrightness](channel_type channel) { //<
			return (((int32_t)channel * 21 + averageBrightness) * 157) >> 14;
		});
	}

	[[nodiscard]] RGB forBlur() const {
		uint32_t averageBrightness = (uint32_t)r * 5 + g * 9 + b * 9;
		return transform([averageBrightness](channel_type channel) { //<
			return ((uint32_t)channel * 5 + averageBrightness) >> 5;
		});
	}

	static RGB average(const RGB& RGBA, const RGB& RGBB) {
		return RGB::transform2(RGBA, RGBB, [](channel_type channelA, channel_type channelB) -> channel_type {
			auto average = (static_cast<uint32_t>(channelA) + static_cast<uint32_t>(channelB)) / 2;
			return std::clamp<uint32_t>(average, 0, channel_max);
		});
	}

	[[nodiscard]] constexpr RGB dim(uint8_t level = 1) const {
		return RGB{
		    channel_type(r >> level),
		    channel_type(g >> level),
		    channel_type(b >> level),
		};
	}

	[[nodiscard]] RGB dull() const {
		return transform([](channel_type channel) {
			if (channel >= 64) {
				return 50;
			}
			return 5;
		});
	}

	[[nodiscard]] RGB greyOut(int32_t proportion) const {
		uint32_t totalRGB = (uint32_t)r + g + b; // max 765

		return transform([totalRGB, proportion](channel_type channel) -> uint8_t {
			uint32_t newRGB = rshift_round((uint32_t)channel * (uint32_t)(0x808080 - proportion) + (totalRGB * (proportion >> 5)), 23);
			return std::clamp<uint32_t>(newRGB, 0, channel_max);
		});
	}

	/**
	 * @brief Generate a new RGB made from blending two source RGBs
	 *
	 * @param sourceA The first RGB
	 * @param sourceB The second RGB
	 * @param index The blend amount. You can think of this as a slider from one RGB to the other
	 * @return RGB The new blended RGB
	 */
	static RGB blend(const RGB& sourceA, const RGB& sourceB, uint16_t index) {
		return {
		    blendChannel(sourceA.r, sourceB.r, index),
		    blendChannel(sourceA.g, sourceB.g, index),
		    blendChannel(sourceA.b, sourceB.b, index),
		};
	}

	static RGB blend2(const RGB& sourceA, const RGB& sourceB, uint16_t indexA, uint16_t indexB) {
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

	RGB transform(const std::function<channel_type(channel_type)>& transformFn) const {
		return RGB{transformFn(r), transformFn(g), transformFn(b)};
	}

	static RGB transform2(const RGB& RGBA, const RGB& RGBB,
	                         const std::function<channel_type(channel_type, channel_type)>& transformFn) {
		return RGB{transformFn(RGBA.r, RGBB.r), transformFn(RGBA.g, RGBB.g),
		              transformFn(RGBA.b, RGBB.b)};
	}

private:
	static channel_type blendChannel(uint32_t channelA, uint32_t channelB, uint16_t index) {
		return blendChannel2(channelA, channelB, index, (std::numeric_limits<uint16_t>::max() - index + 1));
	}

	static channel_type blendChannel2(uint32_t channelA, uint32_t channelB, uint16_t indexA, uint16_t indexB) {
		uint32_t newRGB = rshift_round(channelA * indexA, 16) + rshift_round(channelB * indexB, 16);
		return std::clamp<uint32_t>(newRGB, 0, channel_max);
	}
};
using RGB = RGB;
