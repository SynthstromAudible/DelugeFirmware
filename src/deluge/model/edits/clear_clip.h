#pragma once

#include "deluge-project/clip_address.h"
#include "model/edits/edit.h"

namespace deluge::edits {

/// @brief Clear a whole clip's contents (the CLIP_CLEAR gesture), for instrument and audio clips alike (clear() is
///        virtual).
///
/// apply() opens the Action and calls clip->clear(), which self-records the undo. The view decides the
/// clearAutomation/clearSequenceAndMPE flags and handles the popup and the render.
class ClearClip final : public Edit {
public:
	/// @param address             The clip to clear.
	/// @param clearAutomation     Whether its param automation goes too.
	/// @param clearSequenceAndMPE Whether its notes and MPE (an audio clip: its sample) go too.
	ClearClip(project::ClipAddress address, bool clearAutomation, bool clearSequenceAndMPE)
	    : address_(address), clearAutomation_(clearAutomation), clearSequenceAndMPE_(clearSequenceAndMPE) {}
	/// @copydoc Edit::apply
	/// @return Error::UNSPECIFIED where the song holds no such clip.
	Error apply(Context& ctx) override;

private:
	project::ClipAddress address_;
	bool clearAutomation_;
	bool clearSequenceAndMPE_;
};

} // namespace deluge::edits
