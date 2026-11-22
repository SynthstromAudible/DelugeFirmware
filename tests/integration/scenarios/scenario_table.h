/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include <cstdint>

/// @file
/// @brief The scenarios the Embassy host harness can run, found by name, each with the card it boots and its pacing.
///
/// Every scenario is stepped, driven one tick at a time by golden_vt_render's generic driver: `begin` once, then
/// `dispatchStep` and `stepDone` until `complete`, then `failures`. Adding one means writing its five functions and a
/// row in scenario_table.cpp, which also names its card, pacing, boot and drive; the harness needs no change. A row
/// takes `ScenarioBoot::NoSettingsPrelude` when the scenario needs the byte channel free from the first tick and reads
/// no settings, and `ScenarioDrive::OwnClock` when it drives the app's machines as a BSP task does rather than as
/// interaction-tier entries.
extern "C" {

/// How the harness paces a scenario's audio, set before the render task is spawned.
enum class ScenarioPacing : uint8_t {
	/// Virtual time: the render runs only as the harness drives it.
	Virtual,
	/// Every block rendered on the audio tier after boot, and the sample clock follows the render.
	Realtime,
	/// Realtime at the device's period, one 128-frame block per 2,902 virtual microseconds, so timing checks compare
	/// against the device's audio period.
	DevicePeriod,
};

/// What boot runs before a scenario starts, set before deluge_app_init.
enum class ScenarioBoot : uint8_t {
	/// The full boot, settings prelude included: the prelude is itself exercised.
	Full,
	/// No startup settings prelude (deluge_startup_skip_settings_prelude): its reads, and its write when
	/// MIDI_FOLLOW.XML is absent, would occupy the single-slot byte channel a scenario that needs no settings uses.
	NoSettingsPrelude,
};

/// How a scenario's step functions map onto virtual time.
enum class ScenarioDrive : uint8_t {
	/// `dispatchStep` is an interaction-tier entry (the render's requests are applied first) and runs one tick of
	/// the scenario; the runner waits a virtual millisecond before `stepDone` and another after it.
	Interaction,
	/// The scenario keeps its own clock of one tick per virtual millisecond: `dispatchStep` and `stepDone` each run
	/// one tick and `stepDone` always answers true, so ticks fall on consecutive milliseconds. Neither is an
	/// interaction-tier entry: the scenario drives the app's machines the way a BSP task does.
	OwnClock,
};

/// The memory card a scenario boots with. The harness installs it before any task is spawned (the host SD opens its
/// disk once, on first access), which is why a scenario cannot build its card in `onAppInit`.
struct ScenarioCard {
	enum class Kind : uint8_t {
		/// A freshly formatted card holding nothing.
		Empty,
		/// The song-corpus fixture GOLDEN_FIXTURE names (default cordae), rebuilt from the maintainer's card backup.
		CorpusFixture,
		/// The directory or card manifest (`.card`) the environment variable `arg` names ("none" for an empty card).
		/// `arg` may add, after a '|', what to use when the variable is unset: a repo-relative directory or manifest,
		/// or `corpus` for the corpus fixture. Without one the variable is required.
		FromEnv,
		/// The card manifest at the repo-relative path `arg` (format:
		/// src/platform/device/src/storage/card_manifest.rs).
		Manifest,
		/// golden_vt_render's Rust card builder named `arg`, for a card whose contents depend on the run's
		/// environment.
		Builder,
	};
	Kind kind = Kind::Empty;
	const char* arg = nullptr;
};

struct DelugeScenario {
	/// The name GOLDEN_SCENARIO selects it by.
	const char* name;
	/// Called right after deluge_app_init(), before boot finishes and before the render has run. Null for most.
	void (*onAppInit)(void);
	void (*begin)(void);
	bool (*complete)(void);
	void (*dispatchStep)(void);
	bool (*stepDone)(void);
	int32_t (*failures)(void);
	ScenarioPacing pacing = ScenarioPacing::Virtual;
	ScenarioBoot boot = ScenarioBoot::Full;
	ScenarioDrive drive = ScenarioDrive::Interaction;
	ScenarioCard card{};
};

/// @brief The scenario named @p name, or null when there is none.
const DelugeScenario* deluge_scenario_find(const char* name);

} // extern "C"
