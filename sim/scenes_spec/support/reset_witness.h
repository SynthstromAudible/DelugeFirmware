#pragma once

/// @file
/// @brief What the five settings resets are told apart by, and the panel globals their confirmation window is drawn
///        under.
///
/// One global per subsystem, chosen so that each reset moves its own and leaves the others alone:
///  - `midi_thru` / `analog_in_ppqn` / `use_sharps`: `FlashStorage::resetSettings`, and nothing else writes them;
///  - `follow_root_note` / `follow_display_param`: `MidiFollow::factoryReset`, which `resetSettings` does not touch;
///  - `drum_randomizer`: `RuntimeFeatureSettings::factoryReset`.
/// Every reset is run from `kPerturbed`, where each field is off the value its own reset writes, so "the reset ran" and
/// "the reset did not run" are always distinguishable. Swapping two resets' hooks changes the witness of both.

#include "gui/l10n/l10n.h"
#include "hid/display/oled_canvas/canvas.h"
#include "io/midi/midi_engine.h"
#include "model/settings/runtime_feature_settings.h"
#include "playback/playback_handler.h"
#include "storage/flash_storage.h"

#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>
#include <system_error>

namespace deluge::scenes::spec::reset {

/// @brief The six globals a reset is identified by.
struct ResetWitness {
	bool midi_thru;
	uint32_t analog_in_ppqn;
	bool use_sharps;
	uint8_t follow_root_note;
	bool follow_display_param;
	uint32_t drum_randomizer;
	bool operator==(const ResetWitness&) const = default;
};

/// @brief The six globals as they are now.
inline ResetWitness witness() {
	return ResetWitness{midiEngine.midiThru,
	                    playbackHandler.analogInTicksPPQN,
	                    FlashStorage::defaultUseSharps,
	                    midiEngine.midiFollowKitRootNote,
	                    midiEngine.midiFollowDisplayParam,
	                    runtimeFeatureSettings.get(RuntimeFeatureSettingType::DrumRandomizer)};
}

/// @brief Put the six globals back to @p w.
inline void restore(const ResetWitness& w) {
	midiEngine.midiThru = w.midi_thru;
	playbackHandler.analogInTicksPPQN = w.analog_in_ppqn;
	FlashStorage::defaultUseSharps = w.use_sharps;
	midiEngine.midiFollowKitRootNote = w.follow_root_note;
	midiEngine.midiFollowDisplayParam = w.follow_display_param;
	runtimeFeatureSettings.set(RuntimeFeatureSettingType::DrumRandomizer, w.drum_randomizer);
}

/// @brief The state every driven reset starts from: every field off the value its own reset writes.
inline constexpr ResetWitness kPerturbed{
    /*midi_thru=*/true,                                                        // resetSettings writes false
    /*analog_in_ppqn=*/99,                                                     // ...24
    /*use_sharps=*/false,                                                      // ...true
    /*follow_root_note=*/99,                                                   // MidiFollow::factoryReset writes 36
    /*follow_display_param=*/true,                                             // ...false
    /*drum_randomizer=*/static_cast<uint32_t>(RuntimeFeatureStateToggle::Off), // ...On
};

/// @brief The reference table's column for each witness field, in `witness_values` order.
inline constexpr std::array<std::string_view, 6> kWitnessColumns{
    "midi_thru", "analog_in_ppqn", "use_sharps", "follow_root_note", "follow_display_param", "drum_randomizer"};

/// @brief @p w as the table's numbers, in `kWitnessColumns` order; a flag is 0 or 1.
inline std::array<uint32_t, kWitnessColumns.size()> witness_values(const ResetWitness& w) {
	return {w.midi_thru ? 1u : 0u,
	        w.analog_in_ppqn,
	        w.use_sharps ? 1u : 0u,
	        w.follow_root_note,
	        w.follow_display_param ? 1u : 0u,
	        w.drum_randomizer};
}

/// @brief The witness the table's numbers @p v stand for, in `kWitnessColumns` order.
inline ResetWitness witness_of(const std::array<uint32_t, kWitnessColumns.size()>& v) {
	return ResetWitness{v[0] != 0, v[1], v[2] != 0, static_cast<uint8_t>(v[3]), v[4] != 0, v[5]};
}

/// @brief @p text as a whole unsigned number; nothing when any of it is not one.
inline std::optional<uint32_t> number_of(std::string_view text) {
	uint32_t v = 0;
	const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), v);
	if (ec != std::errc{} || end != text.data() + text.size() || text.empty()) {
		return std::nullopt;
	}
	return v;
}

/// @brief The globals the confirmation window's drawing reads, pinned to their defaults: the language, the menu
///        highlighting the label's x and the inversion follow, and rounded corners.
inline void pin_panel_globals() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	FlashStorage::accessibilityMenuHighlighting = MenuHighlighting::FULL_INVERSION;
	deluge::hid::display::oled_canvas::Canvas::menuHighlightingInverts = true;
	deluge::hid::display::oled_canvas::Canvas::roundedCornersEnabled = true;
}

} // namespace deluge::scenes::spec::reset
