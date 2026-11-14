#pragma once

/// @file
/// @brief MIDI takeover: where a controller's value moves a knob, as a pure function of the knob, the value, the global
///        mode and what the controller last sent.

#include <cstdint>
#include <optional>

namespace deluge::controls {

/// @brief How a controller that does not match the knob's position takes it over.
enum class TakeoverMode : uint8_t {
	JUMP,     ///< the knob goes to the controller's value
	PICKUP,   ///< the knob stays until the controller crosses it
	SCALE,    ///< the knob moves toward its end as the controller moves toward its own
	RELATIVE, ///< the controller's value is an offset
};

/// @brief What a takeover reads besides the knob: the global mode, and what forces a jump or a relative reading.
struct TakeoverInputs {
	TakeoverMode mode{TakeoverMode::JUMP};
	bool cable_relative{false}; ///< the cable sends relative values
	bool knob_relative{false};  ///< the knob's own mapping is relative
	bool recording{false};      ///< a clock runs and the song records: jump
	bool step_editing{false};   ///< a step region is held: jump
};

/// @brief One controller's remembered position.
struct TakeoverState {
	std::optional<int32_t> previous; ///< the last position the controller sent, once it has sent one
	[[nodiscard]] bool operator==(const TakeoverState&) const = default;
};

/// @brief The knob position a controller value moves a knob at @p knob_pos to, updating @p state.
///
/// A relative reading (the cable's, the knob's or the global mode's) adds the value as a signed offset and keeps the
/// knob from falling below -64 or its current position, whichever is lower. A jump (the JUMP mode, a recording song, a
/// held step region) takes the controller's position. PICKUP and SCALE take it once the controller has met the knob's
/// position from either side; until then PICKUP holds still and SCALE moves the knob so both reach an end together.
/// The first value of a PICKUP or SCALE reading seeds @p state and takes the controller's position.
/// @param knob_pos The knob's position, -64..64.
/// @param value    The controller's value, 0..127; 127 reads as +64 where a position is wanted.
/// @param inputs   The mode and what forces a jump or a relative reading.
/// @param state    The controller's remembered position, updated.
/// @return The knob's new position, -64..64.
[[nodiscard]] int32_t knob_pos_after(int32_t knob_pos, uint8_t value, TakeoverInputs inputs, TakeoverState& state);

} // namespace deluge::controls
