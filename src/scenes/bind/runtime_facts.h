#pragma once

/// @file
/// @brief The facts a menu needs that the model does not hold.

#include "controls/facts.h"
#include "deluge-project/instrument_slot.h"
#include "deluge-project/param_address.h"
#include "deluge-project/setting_group.h"

#include <cstdint>

namespace deluge::scenes::bind {

/// @brief The facts a menu needs that are not settings, each answered by the host. Nothing else a binding reads comes
/// from outside the model. The knobs' reads (the param's live value, the stutter, note tails) are @ref
/// controls::Facts, which the host answers with the rest.
class RuntimeFacts : public controls::Facts {
public:
	virtual ~RuntimeFacts() = default;
	/// @brief The Deluge instrument's row for a sound's params, the global effectable's for a kit's, an audio track's
	/// and the song's: the rows every Deluge host's owners have.
	[[nodiscard]] const plugin::ParamInfo* param_info(plugin::PluginId plugin,
	                                                  const project::ParamTarget& target) const override;
};

} // namespace deluge::scenes::bind
