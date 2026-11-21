#pragma once

/// @file
/// @brief `publish`: write a timeline's scroll, zoom and triplets view into a song's clip navigation slot, the reverse
///        of `TimelineState::adopt`, for the specs that compare a port's view with the song legacy reads.

#include "definitions_cxx.hpp"
#include "model/song/song.h"
#include "scenes/timeline/timeline.h"

namespace deluge::scenes::spec {

/// @brief Write @p s's scroll, zoom and triplets settings into @p song's clip navigation slot.
/// @param s    The timeline to write.
/// @param song The song to write it into; its arrangement slot is left as it is.
inline void publish(const scenes::timeline::TimelineState& s, ::Song& song) {
	song.persistentView.xScroll[NAVIGATION_CLIP] = s.x_scroll;
	song.persistentView.xZoom[NAVIGATION_CLIP] = s.x_zoom;
	song.persistentView.tripletsOn = s.triplets_on;
	song.persistentView.tripletsLevel = s.triplets_level;
}

} // namespace deluge::scenes::spec
