#pragma once
#include "util/const_functions.h"
#include "util/misc.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>

/**
 * @brief This class represents the color format most used by the Deluge globally
 *
 */
class RGB {
public:
	/// The size of each color channel
	using channel_type = uint8_t;

	/// The maximum value each channel can hold.
	/// Useful for std::clamp<>(val, RGB::channel_min, RGB::channel_max)
	static constexpr auto channel_min = std::numeric_limits<channel_type>::min();

	/// The maximum value each channel can hold.
	/// Useful for std::clamp<>(val, RGB::channel_min, RGB::channel_max)
	static constexpr auto channel_max = std::numeric_limits<channel_type>::max();

	/// Red channel
	channel_type r = 0;

	/// Green channel
	channel_type g = 0;

	// Blue channel
	channel_type b = 0;

	/**
	* @brief Construct a monochrome (white) shade
	*
	* @param brightness The brightness level
	* @return RGB The constructed color
	*/
	static constexpr RGB monochrome(uint8_t brightness) { return RGB{brightness, brightness, brightness}; }

	/**
	 * @brief Construct a color from a hue
	 *
	 * @param hue The hue value
	 * @return RGB The color
	 */
	static RGB fromHue(int32_t hue);

	/**
	 * @brief Construct a color from a hue
	 *
	 * @param hue The hue value
	 * @return RGB A pastel color
	 */
	static RGB fromHuePastel(int32_t hue);

	/**
	 * @brief Create a derived color for tails (used by views)
	 *
	 * @return RGB The derived color
	 */
	[[nodiscard]] RGB forTail() const {
		uint32_t averageBrightness = ((uint32_t)r + g + b);
		return transform([averageBrightness](channel_type channel) { //<
			return (((int32_t)channel * 21 + averageBrightness) * 157) >> 14;
		});
	}

	/**
	 * @brief Create a derived color for blurs (used by views)
	 *
	 * @return RGB The derived color
	 */
	[[nodiscard]] RGB forBlur() const {
		uint32_t averageBrightness = (uint32_t)r * 5 + g * 9 + b * 9;
		return transform([averageBrightness](channel_type channel) { //<
			return ((uint32_t)channel * 5 + averageBrightness) >> 5;
		});
	}

	/**
	 * @brief Average two colors together
	 *
	 * @param colorA The first color
	 * @param colorB The second color
	 * @return RGB A composite average of the channel values of the component colors
	 */
	static RGB average(const RGB& colorA, const RGB& colorB) {
		return RGB::transform2(colorA, colorB, [](channel_type channelA, channel_type channelB) -> channel_type {
			auto average = (static_cast<uint32_t>(channelA) + static_cast<uint32_t>(channelB)) / 2;
			return std::clamp<uint32_t>(average, 0, channel_max);
		});
	}

	/**
	 * @brief Dim a color
	 *
	 * @param level How much to dim (up to 8)
	 * @return RGB The dimmed color
	 */
	[[nodiscard]] constexpr RGB dim(uint8_t level = 1) const {
		return RGB{
		    channel_type(r >> level),
		    channel_type(g >> level),
		    channel_type(b >> level),
		};
	}

	/**
	 * @brief Dull a color, clamping it to [5, 50]
	 *
	 * This is only used by the ArrangerView and its parent, View
	 * @see ArrangerView#drawMuteSquare
	 * @see View#getClipMuteSquareColor
	 *
	 * @return RGB the dulled color
	 */
	[[nodiscard]] RGB dull() const {
		return transform([](channel_type channel) -> channel_type { return std::clamp<channel_type>(channel, 5, 50); });
	}

	/**
	 * @brief Grey out a color
	 *
	 * @param proportion The amount to grey out
	 * @return RGB The resulting color
	 */
	[[nodiscard]] RGB greyOut(int32_t proportion) const {
		uint32_t totalRGB = (uint32_t)r + g + b; // max 765

		return transform([totalRGB, proportion](channel_type channel) -> uint8_t {
			uint32_t newRGB = rshift_round(
			    (uint32_t)channel * (uint32_t)(0x808080 - proportion) + (totalRGB * (proportion >> 5)), 23);
			return std::clamp<uint32_t>(newRGB, 0, channel_max);
		});
	}

	/**
	 * @brief Generate a new color made from blending two source colors
	 *
	 * @param sourceA The first color
	 * @param sourceB The second color
	 * @param index The blend amount. You can think of this as a slider from one RGB to the other
	 * @return RGB The new blended color
	 */
	static RGB blend(const RGB& sourceA, const RGB& sourceB, uint16_t index) {
		return transform2(sourceA, sourceB, [index](auto channelA, auto channelB) { //<
			return blendChannel(channelA, channelB, index);
		});
	}

	/**
	 * @brief Generate a new color made from blending two source colors with individual proportions
	 *
	 * @param sourceA The first color
	 * @param sourceB The second color
	 * @param indexA The blend amount for sourceA
	 * @param indexB The blend amount for sourceB
	 * @return RGB The new blended color
	 */
	static RGB blend2(const RGB& sourceA, const RGB& sourceB, uint16_t indexA, uint16_t indexB) {
		return transform2(sourceA, sourceB, [indexA, indexB](auto channelA, auto channelB) {
			return blendChannel2(channelA, channelB, indexA, indexB);
		});
	}

	/**
	 * @brief Legacy access to the color internals for ease of use
	 *
	 * @param idx The channel to access (order R G B)
	 * @return channel_type& The channel
	 */
	constexpr channel_type& operator[](size_t idx) {
		switch (idx) {
		case 0:
			return r;
		case 1:
			return g;
		case 2:
			return b;
		}
	}

	/**
	 * @brief Used in combination with operator[] and begin end
	 *
	 * @return constexpr size_t The size of this 'container'
	 */
	static constexpr size_t size() { return 3; }

	/**
	 * @brief Iterator constructor pointing to the beginning of this structure
	 *
	 * @warning Strongly relies on both the sizeof() and byte layout of this object (i.e. that it is a POD and 3 bytes)
	 *
	 * @return constexpr channel_type* The first channel
	 */
	constexpr channel_type* begin() { return &this->r; }

	/**
	 * @brief Iterator constructor for the end sentinel of this structure
	 *
	 * @warning Strongly relies on both the sizeof() and byte layout of this object (i.e. that it is a POD and 3 bytes)
	 *
	 * @return constexpr channel_type* One byte past the last channel
	 */
	constexpr channel_type* end() { return (&this->b + 1); }

	/**
	 * @brief Create a new color by transforming each channel of a color
	 *
	 * @param transformFn The function to apply to each channel, taking a channel_type value and returning a channel_type value
	 * @return RGB The new color
	 */
	RGB transform(const std::function<channel_type(channel_type)>& transformFn) const {
		return RGB{transformFn(r), transformFn(g), transformFn(b)};
	}

	/**
	 * @brief Create a new color by transforming the channels of two colors
	 *
	 * @param colorA The first color
	 * @param colorB The second color
	 * @param transformFn The transformer function, taking two channel_type values and returning a channel_type value
	 * @return RGB The new color
	 */
	static RGB transform2(const RGB& colorA, const RGB& colorB,
	                      const std::function<channel_type(channel_type, channel_type)>& transformFn) {
		return RGB{transformFn(colorA.r, colorB.r), transformFn(colorA.g, colorB.g), transformFn(colorA.b, colorB.b)};
	}

private:
	/**
	* @brief Blend a channel in equal proportions
	* @return The blended channel value
	*/
	static channel_type blendChannel(uint32_t channelA, uint32_t channelB, uint16_t index) {
		return blendChannel2(channelA, channelB, index, (std::numeric_limits<uint16_t>::max() - index + 1));
	}

	/**
	* @brief Blend two channels in differing proportions
	* @return The blended channel value
	*/
	static channel_type blendChannel2(uint32_t channelA, uint32_t channelB, uint16_t indexA, uint16_t indexB) {
		uint32_t newRGB = rshift_round(channelA * indexA, 16) + rshift_round(channelB * indexB, 16);
		return std::clamp<uint32_t>(newRGB, 0, channel_max);
	}
};
using RGB = RGB;
