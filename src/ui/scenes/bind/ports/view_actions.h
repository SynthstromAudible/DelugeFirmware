#pragma once

/// @file
/// @brief What a scene asks of the root view beneath it.

#include <cstdint>

namespace deluge::ui::scenes::bind {

/// @brief How an opener ends a held audition, each as its legacy opener does.
enum class AuditionEnd : uint8_t {
	ClipViewWhenAuditioning,   ///< `AudioRecorder::opened`, `SampleBrowser::opened`
	ClipViewForInstrumentClip, ///< `EditName::beginSession`
	KeyboardWhenAuditioning,   ///< `LoopPoint::beginSession`, `FileSelector::beginSession`
};

/// @brief The root view's actions, which the host performs as the legacy openers do.
class ViewActions {
public:
	virtual ~ViewActions() = default;
	/// @brief End a held audition the way @p how names, before a scene opens over it: the scene claims every pad, so
	///        the held pad's release never reaches the view that started the note.
	/// @param how Which opener's test and call end it.
	virtual void end_audition(AuditionEnd how) = 0;
};

} // namespace deluge::ui::scenes::bind
