/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "definitions_cxx.hpp"
#include "storage/audio/latest_sample_load.h"

#include <cstdint>
#include <optional>
#include <string_view>

class Sample;

#define MAX_MANUAL_SLICES 64

struct SliceItem {
	int32_t startPos;
	int32_t transpose;
};

namespace deluge::gui::ui {

/// @brief The slicer's work on the current kit, with no UI of its own: auditioning a stretch of the sample through
///        the kit's first drum, slicing that drum's file into one drum per slice, the voice walk that finds where
///        the audition is playing, and putting the first drum's range back.
///
/// The legacy `Slicer` and the ported slicer's host both drive one of these, so the two UIs slice the same way. Both
/// loads are asynchronous; the caller ticks them (`tick_audition`, `tick_slice`) while its slicer is up.
///
/// @pre Everything reads the current kit (`getCurrentKit()`), and the slice reads `soundEditor`'s current sound and
///      source: a caller that is not the sound editor resolves them first.
class SliceCore {
public:
	/// @brief One audition: play the first drum from @p startPoint to @p endPoint (-1 keeps the end), transposed.
	struct AuditionTarget {
		int64_t startPoint;
		int64_t endPoint;
		int32_t transpose;
		int32_t on; ///< Non-zero: the pad's velocity, a press; zero: the release.
	};

	/// @brief What to slice: region mode's equal slices, or manual mode's points.
	struct Target {
		bool isManual;
		int32_t sliceCount;
		SliceItem manualPoints[MAX_MANUAL_SLICES];
	};

	/// @brief How a slice ended.
	struct SliceEnd {
		bool landed; ///< The drums were made (and, for a manual slice, painted).
		Error error; ///< The reason it did not land, or a slice error; `NONE` for a landed slice.
	};

	/// @brief Press or release one audition of @p sample on the first drum.
	///
	/// A press points the first drum at @p sample's file, loads it if the drum holds nothing, and sounds once it has;
	/// a release cancels a held load and sounds the note-off at once.
	void audition(const Sample& sample, const AuditionTarget& target);
	/// @brief Forget a held audition, so a load still out sounds nothing when it lands.
	void cancel_audition() { audition_load_.cancel(); }
	/// @brief Finish a held audition whose load has landed.
	void tick_audition();
	/// @return Whether an audition's load is still out.
	[[nodiscard]] bool audition_pending() const { return audition_load_.pending(); }
	/// @return How many auditions gave up on a stalled load.
	[[nodiscard]] int32_t audition_give_ups() const { return give_ups_; }

	/// @brief Silence the first drum and let go of its file.
	void unbind_first_drum();
	/// @brief Silence the first drum.
	void kill_voices();
	/// @brief Silence the first drum and give it its whole file back, untransposed.
	/// @return False, changing nothing, when the first drum holds no file.
	bool restore_full_range();
	/// @return Where the first drum's newest voice on its file is playing, in samples, if one is.
	std::optional<int32_t> play_position();

	/// @brief Start slicing @p path into the first drum and as many new drums as @p target names.
	///
	/// Silences the first drum for a manual slice, stops any browser preview, cuts the sound and points its holder at
	/// @p path; the slice itself runs when the load lands (`tick_slice`). @p target is copied: later changes to the
	/// caller's own count or points do not reach it.
	void begin_slice(const Target& target, std::string_view path);
	/// @return Whether a slice's load is out.
	[[nodiscard]] bool slicing() const { return slicing_; }
	/// @brief Finish the slice whose load has landed.
	/// @return How it ended, once it has; nothing while its load is still out or when none is.
	std::optional<SliceEnd> tick_slice();

private:
	void sound_audition(const AuditionTarget& target);
	Error do_slice(int32_t sliceCount);

	deluge::storage::LatestSampleLoad<AuditionTarget> audition_load_{};
	int32_t give_ups_{0};
	deluge::storage::LatestSampleLoad<uint32_t> slice_load_{};
	bool slicing_{false};
	Target pending_{};
};

} // namespace deluge::gui::ui
