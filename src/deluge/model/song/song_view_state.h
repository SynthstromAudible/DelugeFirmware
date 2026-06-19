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

class Clip;

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
