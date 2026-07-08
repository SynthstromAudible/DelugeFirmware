#pragma once

#include "model/edits/edit.h"
#include <cstdint>

namespace deluge::edits {

/// Set the song-global swing amount (the reification of PlaybackHandler::commandEditSwingAmount). apply() records the
/// before→after into a coalescing SWING_CHANGE Action (via ActionLogger::recordSwingChange) and writes the new value
/// into the DOM groove. Song-global, so no address. The view keeps the clamp, the changed-guard, and the display.
class SetSwing final : public Edit {
public:
	explicit SetSwing(int8_t newSwing) : newSwing_(newSwing) {}

	Error apply(Context& ctx) override;

private:
	int8_t newSwing_;
};

} // namespace deluge::edits
