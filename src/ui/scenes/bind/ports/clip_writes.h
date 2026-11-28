#pragma once

/// @file
/// @brief Where a scene's edits of a clip go: requests the host applies as undoable steps where the edit is one.

#include "project/clip_address.h"

#include <cstdint>
#include <string_view>

namespace deluge::ui::scenes::bind {

/// @brief How an audio clip's length gesture resizes it.
enum class ResizeKind : uint8_t {
	END,              ///< the sample's end marker dragged to a new clip length
	START,            ///< the sample's start marker dragged to a new start tick
	STRETCH,          ///< the clip's length changed, the sample timestretched to fill it
	NO_STRETCH,       ///< the clip's length changed with the sample's end marker, keeping its speed
	TO_SAMPLE_LENGTH, ///< the clip's length set to the sample's
};

/// @brief Takes a scene's clip edits. The app applies each; a spec records them.
class ClipWrites {
public:
	virtual ~ClipWrites() = default;
	/// @brief Move @p clip's colour by @p offset. Not undoable, as legacy's recolour is not.
	/// @return Whether it was taken.
	virtual bool recolour_clip(project::ClipAddress clip, int32_t offset) = 0;
	/// @brief Clear @p clip, as one undo step.
	/// @return Whether it was taken.
	virtual bool clear_clip(project::ClipAddress clip, bool clear_automation, bool clear_sequence_and_mpe) = 0;
	/// @brief Move an audio clip's marker (its AUDIO_CLIP row @p path) to @p value, a playing voice reseated on the
	/// moved sample.
	/// @return Whether it was taken.
	virtual bool move_audio_clip_marker(project::ClipAddress clip, std::string_view path, int64_t value) = 0;
	/// @brief Resize the audio clip @p clip, as one undo step: its length, and the sample's marker where @p kind moves
	/// one. A lengthen right after a shorten of the same clip undoes the shorten.
	/// @param clip       The clip.
	/// @param kind       What the gesture moves.
	/// @param new_length The new length in ticks (START: the new start tick; TO_SAMPLE_LENGTH: not read).
	/// @param old_length The length the gesture started from, in ticks.
	/// @return Whether it was taken: false where the clip does not resolve or has no sample to measure.
	virtual bool resize_audio_clip(project::ClipAddress clip, ResizeKind kind, int32_t new_length,
	                               int32_t old_length) = 0;
	/// @brief Record @p x_scroll as the view a redo of @p clip's last length change returns to: the view scrolls after
	/// the change, so the step learns it after.
	virtual void set_undo_scroll_after(project::ClipAddress clip, int32_t x_scroll) = 0;
	/// @brief Keep @p x_scroll and @p x_zoom as the song's clip navigation, the view every clip view opens on and the
	/// song file saves.
	/// @param x_scroll The first tick shown.
	/// @param x_zoom   Ticks per square.
	virtual void set_clip_navigation(int32_t x_scroll, uint32_t x_zoom) = 0;
	/// @brief Shift the clip's contents by @p amount ticks, as a Y_ENC turn in a clip view does, with its undo step:
	///        the automation too unless the device's "shift automation only in the automation view" setting is on.
	/// @param clip The clip. @param amount Signed ticks, a whole number of squares.
	/// @return Whether anything moved.
	virtual bool shift_clip_horizontally(project::ClipAddress clip, int32_t amount) = 0;
};

} // namespace deluge::ui::scenes::bind
