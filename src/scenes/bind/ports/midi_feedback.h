#pragma once

/// @file
/// @brief MIDI follow's feedback: the CCs the device sends back to a controller as the followed params move.

#include "deluge-project/owner_plugin.h"
#include "deluge-project/param_address.h"

#include <cstdint>
#include <optional>

namespace deluge::scenes::bind {

/// @brief MIDI follow's feedback, read and sent by the host as the follow engine does.
class MidiFeedback {
public:
	virtual ~MidiFeedback() = default;
	/// @brief Whether feedback is on: a feedback channel is set.
	[[nodiscard]] virtual bool enabled() const = 0;
	/// @brief Whether the clip MIDI follow acts on is a global effectable's (an audio clip, or a kit affecting the
	///        whole kit), whose own params it follows; otherwise a sound's.
	[[nodiscard]] virtual bool global_effectable_context() const = 0;
	/// @brief The CC that follows @p target for an owner of @p owner's plugin.
	/// @param owner  The owner's plugin.
	/// @param target The param.
	/// @return The CC, or nullopt when no CC follows it.
	[[nodiscard]] virtual std::optional<uint8_t> cc_of(project::OwnerPlugin owner,
	                                                   project::ParamTarget target) const = 0;
	/// @brief Send @p cc with the knob position @p knob_pos to the feedback channels.
	/// @param cc       The CC.
	/// @param knob_pos The followed param's knob position, -64 to 64. The CC's value is `knob_pos + 64`, held to 0–127,
	///                 so 64 sends 127 as 63 does.
	virtual void send(uint8_t cc, int32_t knob_pos) = 0;
	/// @brief Send every followed param's CC for the selected or active clip.
	/// @param automation Only the automated params, as the automation feedback's timer sends them.
	virtual void send_all(bool automation) = 0;
};

} // namespace deluge::scenes::bind
