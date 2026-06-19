/*
 * Copyright © 2024 Synthstrom Audible Limited
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
#include "storage/flash_storage.h"
#include <cstdint>

class Clip;

/// View/UI state that the Song carries and which **is** serialized to the song file (today inline
/// in the song XML). This is the "persistent view state" bucket of the project-model separation
/// (docs/dev/project_model_extraction.md): where the user was looking / how they were editing,
/// saved with the document but distinct from project data. Serialization keeps the same XML tags
/// (see Song::writeToFile / readFromFile), so the on-disk format is unchanged.
struct PersistentViewState {
	uint32_t xZoom[2]{};  // per-navigation zoom (CLIP / ARRANGEMENT); ctor sets the max-zoom-out default
	int32_t xScroll[2]{}; // per-navigation scroll (signed)
	int32_t xScrollForReturnToSongView{0};
	int32_t xZoomForReturnToSongView{0};
	bool tripletsOn{false};
	uint32_t tripletsLevel{0}; // The number of ticks in one of the three triplets
	bool affectEntire{false};
	SessionLayoutType sessionLayout = FlashStorage::defaultSessionLayout;
	int32_t songGridScrollX{0};
	int32_t songGridScrollY{0};
	int32_t songViewYScroll{0};
	int32_t arrangementYScroll{0};
	int32_t lastClipInstanceEnteredStartPos{-1}; // -1 means we are not "inside" an arrangement
	bool arrangerAutoScrollModeActive{false};
};

/// View/UI state that the Song carries but which is **never** serialized to the song file.
/// This is the "runtime view state" bucket of the project-model separation
/// (docs/dev/project_model_extraction.md): selection and transient edit/load flags that exist
/// only while a song is loaded. Distinct from project data and from the saved (persistent) view
/// state. Reached via Song for now; a later phase may relocate it into the UI layer.
struct RuntimeViewState {
	/// The clip currently being edited / focused. Persisted indirectly via the per-clip
	/// "selected"/"beingEdited" attributes, not as a song-level field.
	Clip* currentClip = nullptr;
	/// Previously-current clip (for future use, e.g. finding an instrument clip).
	Clip* previousClip = nullptr;
	/// Whether the UI is in automation-edit mode (influences which override values are written
	/// on save, but is not itself saved).
	bool paramsInAutomationMode = false;
	/// Whether fill mode is currently engaged.
	bool fillModeActive = false;
	/// Temp flag, only valid while loading a Song.
	bool inClipMinderViewOnLoad = false;
};
