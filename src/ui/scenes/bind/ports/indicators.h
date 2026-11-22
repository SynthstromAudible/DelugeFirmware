#pragma once

/// @file
/// @brief The pads' and buttons' indicators a scene asks the host to change outside its own frames.

namespace deluge::ui::scenes::bind {

/// @brief The host's indicators.
class Indicators {
public:
	virtual ~Indicators() = default;
	/// @brief Clear the tick squares the views' playheads drew, at once.
	virtual void clear_tick_squares() = 0;
};

} // namespace deluge::ui::scenes::bind
