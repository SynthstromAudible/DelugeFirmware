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

/// @file
/// @brief The DEVICE setting group: the device's own settings, which it keeps whatever song is loaded. Its rows are
///        the table `table_of(SettingGroupId::DEVICE)` gives; the host stores their values where the device keeps them
///        (the flash settings, the pad controller), and a menu reads and writes them through the scenes' device port.
///
/// A CHOICE row's value is its choice's index, so the scenes need no legacy type for it. The enums here exist only for
/// the rows whose value a scene branches on; each enumerator's value is its choice's index in the row.

#include "deluge-project/features.h"
#include "plugin-api/types/setting.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace deluge::project {

/// @brief The DEVICE group's rows.
/// @return Every row, in a table that lives for the program's life.
[[nodiscard]] std::span<const plugin::SettingInfo> device_table();

/// @brief Which of the two shortcut layouts the grid's parameter shortcuts resolve: the `pads/shortcutsVersion` row.
enum class ShortcutsVersion : uint8_t {
	V1 = 0, ///< The pre-3.0 layout.
	V3 = 1, ///< The current one, and the default.
};

/// @brief How a playhead is shown on the pads: the `pads/playCursor` row.
enum class FlashCursor : uint8_t {
	FAST = 0, ///< the pad controller flashes the pad; no pixel of the rendered image changes
	OFF = 1,  ///< no playhead
	SLOW = 2, ///< the factory default: the playhead's pixel is substituted in the rendered image
};

/// @brief Which favourites a browser shows beside its files: the `ui/favouritesLayout` row.
enum class FavouritesLayout : uint8_t {
	FAVOURITES = 0,           ///< the favourites row alone, and the default
	FAVOURITES_AND_BANKS = 1, ///< the favourites row and the bank row
	OFF = 2,                  ///< no favourites
};

/// @brief How often MIDI follow sends back the values a followed param's automation plays: the
///        `midiFollow/feedback/automation` row.
enum class FeedbackAutomation : uint8_t {
	DISABLED = 0, ///< never, and the default
	LOW = 1,      ///< the least often
	MEDIUM = 2,   ///< more often
	HIGH = 3,     ///< the most often
};

/// @brief A command the device takes from MIDI whatever song is loaded, once a message is learned for it.
enum class GlobalCommand : uint8_t {
	PLAYBACK_RESTART = 0,         ///< restart playback from the start
	PLAY = 1,                     ///< the PLAY button
	RECORD = 2,                   ///< the RECORD button
	TAP = 3,                      ///< tap tempo
	LOOP = 4,                     ///< loop record
	LOOP_CONTINUOUS_LAYERING = 5, ///< loop record, layering continuously
	UNDO = 6,                     ///< undo
	REDO = 7,                     ///< redo
	FILL = 8,                     ///< fill, held while the message is
	TRANSPOSE = 9,                ///< transpose the song
	NEXT_SONG = 10,               ///< load the next song
	SHIFT = 11,                   ///< the SHIFT button
};
/// @brief How many global commands there are.
inline constexpr size_t kGlobalCommandCount = std::to_underlying(GlobalCommand::SHIFT) + 1;

/// @brief An input MIDI follow listens on: the three follow channels, then one for each of the sixteen tracks.
enum class FollowInput : uint8_t {
	A = 0,         ///< follow channel A
	B = 1,         ///< follow channel B
	C = 2,         ///< follow channel C
	TRACK_1 = 3,   ///< track 1's input
	TRACK_2 = 4,   ///< track 2's input
	TRACK_3 = 5,   ///< track 3's input
	TRACK_4 = 6,   ///< track 4's input
	TRACK_5 = 7,   ///< track 5's input
	TRACK_6 = 8,   ///< track 6's input
	TRACK_7 = 9,   ///< track 7's input
	TRACK_8 = 10,  ///< track 8's input
	TRACK_9 = 11,  ///< track 9's input
	TRACK_10 = 12, ///< track 10's input
	TRACK_11 = 13, ///< track 11's input
	TRACK_12 = 14, ///< track 12's input
	TRACK_13 = 15, ///< track 13's input
	TRACK_14 = 16, ///< track 14's input
	TRACK_15 = 17, ///< track 15's input
	TRACK_16 = 18, ///< track 16's input
};
/// @brief How many follow inputs there are.
inline constexpr size_t kFollowInputCount = std::to_underlying(FollowInput::TRACK_16) + 1;

/// @name The pads' brightness: the `ui/padBrightness` row's levels, and how a menu shows them.
/// @{
inline constexpr int32_t kDimmestPadLevel = 1;    ///< the dimmest level a menu sets
inline constexpr int32_t kBrightestPadLevel = 25; ///< the brightest level, the row's greatest and its default
inline constexpr int32_t kPadLevelShown = 4;      ///< how many a menu shows for one level, and its step a detent
/// @}

/// @name The CV and gate outputs: how many there are, and which gates carry the clock and the run signal, whose mode
///       rows offer a third choice.
/// @{
inline constexpr size_t kCvOutputCount = 2;   ///< the CV outputs
inline constexpr size_t kGateOutputCount = 4; ///< the gate outputs
inline constexpr size_t kRunGateOutput = 2;   ///< the gate that can carry the run signal
inline constexpr size_t kClockGateOutput = 3; ///< the gate that can carry the clock
/// @}

struct device_path;

/// @brief The path of a DEVICE row the table holds. Only `device_path` makes one, from its own constants, so a path a
///        caller types cannot be one: a read that takes a DevicePath reads a row that exists.
class DevicePath {
public:
	/// @brief The row's path.
	[[nodiscard]] constexpr std::string_view text() const { return text_; }
	/// @brief The row's path, for the checked front (`DeviceSettings::read`, `write`) that takes any path.
	constexpr operator std::string_view() const { return text_; } // NOLINT(google-explicit-constructor)
	/// @brief Whether two handles name the same row.
	constexpr bool operator==(const DevicePath&) const = default;

private:
	friend struct device_path;
	constexpr explicit DevicePath(std::string_view text) : text_{text} {}
	std::string_view text_;
};

/// @brief The paths of the DEVICE rows the scenes name in code. `device_settings.cpp` checks, when the image is
///        built, that the table holds a row at each.
struct device_path {
	/// Whether a note's name uses a sharp or a flat: BOOL, 1 for sharps.
	static constexpr DevicePath kUseSharps{"defaults/useSharps"};
	/// The polarity a new patch cable takes: CHOICE over the patch cable's two polarities, unipolar then bipolar.
	static constexpr DevicePath kPatchCablePolarity{"defaults/patchCablePolarity"};
	/// The least tempo a new song draws its tempo from: INTEGER, beats a minute.
	static constexpr DevicePath kTempoLower{"defaults/tempo/lower"};
	/// The greatest tempo a new song draws its tempo from: INTEGER, beats a minute.
	static constexpr DevicePath kTempoUpper{"defaults/tempo/upper"};
	/// The least swing a new song draws its swing from: INTEGER, percent.
	static constexpr DevicePath kSwingLower{"defaults/swing/lower"};
	/// The greatest swing a new song draws its swing from: INTEGER, percent.
	static constexpr DevicePath kSwingUpper{"defaults/swing/upper"};
	/// The first key a new song draws its key from: INTEGER, semitones above C.
	static constexpr DevicePath kKeyLower{"defaults/key/lower"};
	/// The last key a new song draws its key from: INTEGER, semitones above C.
	static constexpr DevicePath kKeyUpper{"defaults/key/upper"};
	/// A new song's tick resolution: INTEGER, the power of two that multiplies 96 ticks a quarter note.
	static constexpr DevicePath kMagnitude{"defaults/magnitude"};
	/// A new song's swing interval: INTEGER, a sync level, named against the magnitude row.
	static constexpr DevicePath kSwingInterval{"defaults/swingInterval"};
	/// A new song's scale: CHOICE over the preset scales, then random, then none.
	static constexpr DevicePath kScale{"defaults/scale"};
	/// The velocity a new instrument plays at: INTEGER, 1 to 127.
	static constexpr DevicePath kVelocity{"defaults/velocity"};
	/// A new MIDI or CV instrument's pitch-bend range: INTEGER, semitones.
	static constexpr DevicePath kBendRange{"defaults/bendRange"};
	/// The metronome's volume: INTEGER, in the menu's steps.
	static constexpr DevicePath kMetronomeVolume{"defaults/metronomeVolume"};
	/// The song the device opens as it starts: CHOICE, blank, template, last opened, last saved.
	static constexpr DevicePath kStartupSong{"defaults/startupSong"};
	/// How a new slice plays its sample: CHOICE, cut, once, loop, stretch.
	static constexpr DevicePath kSliceMode{"defaults/sliceMode"};
	/// Whether a pad lights while the CPU is near its limit: BOOL.
	static constexpr DevicePath kCpuIndicator{"defaults/cpuIndicator"};
	/// How long a press is held to count as a hold: INTEGER, twentieths of a second.
	static constexpr DevicePath kHoldTime{"defaults/holdTime"};
	/// What a recording is quantised to: INTEGER, a sync level named against a magnitude of one.
	static constexpr DevicePath kRecordQuantize{"defaults/recordQuantize"};
	/// Whether an audio clip recorded from an outside input keeps margins past its loop's ends: BOOL.
	static constexpr DevicePath kRecordMargins{"defaults/recordMargins"};
	/// How loud a sound starts a threshold recording: CHOICE, off, low, medium, high.
	static constexpr DevicePath kThresholdRecording{"defaults/thresholdRecording"};
	/// The kind of clip a new clip is: CHOICE, synth, kit, MIDI, CV, audio.
	static constexpr DevicePath kNewClipType{"defaults/newClipType"};
	/// Whether a new clip takes the kind the last one had instead: BOOL.
	static constexpr DevicePath kUseLastClipType{"defaults/useLastClipType"};
	/// Whether the automation editor interpolates between its steps: BOOL.
	static constexpr DevicePath kAutomationInterpolate{"defaults/automation/interpolate"};
	/// Whether a clear outside the automation editor leaves a clip's automation, clearing its notes alone: BOOL.
	static constexpr DevicePath kAutomationClear{"defaults/automation/clear"};
	/// Whether a shift outside the automation editor leaves a clip's automation where it is: BOOL.
	static constexpr DevicePath kAutomationShift{"defaults/automation/shift"};
	/// Whether a note's nudge leaves the automation where it is: BOOL.
	static constexpr DevicePath kAutomationNudgeNote{"defaults/automation/nudgeNote"};
	/// Whether the automation editor's shortcuts are off while a pad auditions: BOOL.
	static constexpr DevicePath kAutomationNoAuditionShortcuts{"defaults/automation/noAuditionShortcuts"};
	/// The grid's shortcut layout: CHOICE over ShortcutsVersion.
	static constexpr DevicePath kShortcutsVersion{"pads/shortcutsVersion"};
	/// How a playhead is shown on the pads: CHOICE over FlashCursor.
	static constexpr DevicePath kPlayCursor{"pads/playCursor"};
	/// Which letter layout the text-entry keyboard on the pads uses: CHOICE, QWERTY, AZERTY, QWERTZ.
	static constexpr DevicePath kTextKeyboardLayout{"pads/textKeyboardLayout"};
	/// The colour a playing clip's pad shows: CHOICE over the nine pad colours, red, green, blue, yellow, cyan,
	/// magenta, amber, white, pink.
	static constexpr DevicePath kPadColourActive{"pads/colour/active"};
	/// The colour a stopped clip's pad shows: CHOICE over the nine pad colours.
	static constexpr DevicePath kPadColourStopped{"pads/colour/stopped"};
	/// The colour a muted track's pad shows: CHOICE over the nine pad colours.
	static constexpr DevicePath kPadColourMuted{"pads/colour/muted"};
	/// The colour a soloed track's pad shows: CHOICE over the nine pad colours.
	static constexpr DevicePath kPadColourSoloed{"pads/colour/soloed"};
	/// The colour a fill clip's pad shows: CHOICE over the nine pad colours.
	static constexpr DevicePath kPadColourFill{"pads/colour/fill"};
	/// The colour a clip set to play once shows: CHOICE over the nine pad colours.
	static constexpr DevicePath kPadColourOnce{"pads/colour/once"};
	/// Whether the accessibility shortcuts are on: BOOL.
	static constexpr DevicePath kAccessibilityShortcuts{"ui/accessibilityShortcuts"};
	/// How a menu marks its selected row: CHOICE, full inversion, partial inversion, no inversion.
	static constexpr DevicePath kMenuHighlighting{"ui/menuHighlighting"};
	/// How a new song's session view lays its clips out: CHOICE, rows, grid.
	static constexpr DevicePath kSessionLayout{"ui/sessionLayout"};
	/// The mode the grid view starts in: CHOICE, selection, green, blue.
	static constexpr DevicePath kGridActiveMode{"ui/grid/activeMode"};
	/// Whether the grid's green mode selects a clip too: BOOL.
	static constexpr DevicePath kGridAllowGreenSelection{"ui/grid/allowGreenSelection"};
	/// Whether pressing an empty grid pad unarms the clips: BOOL.
	static constexpr DevicePath kGridEmptyPadsUnarm{"ui/grid/emptyPadsUnarm"};
	/// Whether pressing an empty grid pad creates a clip and records into it: BOOL.
	static constexpr DevicePath kGridEmptyPadsCreateRec{"ui/grid/emptyPadsCreateRec"};
	/// The layout a new keyboard view's pads take: CHOICE, isomorphic, in key, piano.
	static constexpr DevicePath kKeyboardLayout{"ui/keyboardLayout"};
	/// Whether the keyboard view's velocity sidebar acts only while held: BOOL.
	static constexpr DevicePath kVelocityMomentary{"ui/keyboard/velocityMomentary"};
	/// Whether the keyboard view's mod-wheel sidebar acts only while held: BOOL.
	static constexpr DevicePath kModwheelMomentary{"ui/keyboard/modwheelMomentary"};
	/// Which favourites a browser shows: CHOICE over FavouritesLayout.
	static constexpr DevicePath kFavouritesLayout{"ui/favouritesLayout"};
	/// How bright the pads light: INTEGER, the level 0 to 25, which a menu shows four times over. A device with no
	/// saved settings holds 0, which no menu turn sets.
	static constexpr DevicePath kPadBrightness{"ui/padBrightness"};
	/// What the screensaver shows: CHOICE, off, blank, starscape, the Deluge's.
	static constexpr DevicePath kScreensaverMode{"ui/screensaver/mode"};
	/// How long the device waits for input before the screensaver shows: INTEGER, minutes.
	static constexpr DevicePath kScreensaverTimeout{"ui/screensaver/timeout"};
	/// When the sample browser previews a sample: CHOICE, never, while the song is not playing, always.
	static constexpr DevicePath kSamplePreview{"ui/samplePreview"};
	/// How many bars a recording counts in before it starts: INTEGER, 0 for none.
	static constexpr DevicePath kCountIn{"recording/countIn"};
	/// When an input being recorded is heard through the outputs: CHOICE, smart, on, off.
	static constexpr DevicePath kMonitorMode{"recording/monitorMode"};
	/// Whether the device offers itself as a USB audio interface, from its next start: BOOL.
	static constexpr DevicePath kUsbAudioEnabled{"usbAudio/enabled"};
	/// Whether audio from the USB host is mixed straight into the outputs: BOOL.
	static constexpr DevicePath kUsbDirectMonitoring{"usbAudio/directMonitoring"};
	/// What the device sends the USB host: CHOICE, the main mix or the line input.
	static constexpr DevicePath kUsbOutputSource{"usbAudio/outputSource"};
	/// How many trigger-clock input pulses make a quarter note: INTEGER.
	static constexpr DevicePath kTriggerInPpqn{"clock/triggerIn/ppqn"};
	/// Whether a trigger-clock input pulse starts playback: BOOL.
	static constexpr DevicePath kTriggerInAutoStart{"clock/triggerIn/autoStart"};
	/// How many trigger-clock output pulses make a quarter note: INTEGER.
	static constexpr DevicePath kTriggerOutPpqn{"clock/triggerOut/ppqn"};
	/// Whether the device sends MIDI clock: BOOL.
	static constexpr DevicePath kMidiClockOut{"clock/midiOut"};
	/// Whether the device follows an incoming MIDI clock: BOOL.
	static constexpr DevicePath kMidiClockIn{"clock/midiIn"};
	/// Whether an incoming clock's tempo is doubled or halved to sit near the song's: BOOL.
	static constexpr DevicePath kTempoMagnitudeMatching{"clock/tempoMagnitudeMatching"};
	/// Whether a stem export normalizes each stem but a drum's: BOOL.
	static constexpr DevicePath kStemNormalize{"stemExport/normalize"};
	/// Whether a stem export normalizes each drum's stem: BOOL.
	static constexpr DevicePath kStemNormalizeDrums{"stemExport/normalizeDrums"};
	/// Whether a stem export runs each stem on until it falls silent: BOOL.
	static constexpr DevicePath kStemToSilence{"stemExport/toSilence"};
	/// Whether a stem export renders the song's effects into each stem: BOOL.
	static constexpr DevicePath kStemSongFx{"stemExport/songFx"};
	/// Whether a stem export renders a kit's effects into each drum's stem: BOOL.
	static constexpr DevicePath kStemKitFx{"stemExport/kitFx"};
	/// Whether a stem export renders faster than real time, with no audio out: BOOL.
	static constexpr DevicePath kStemOffline{"stemExport/offline"};
	/// Whether a stem export renders the whole mix as one stem: BOOL.
	static constexpr DevicePath kStemMixdown{"stemExport/mixdown"};
	/// The note a kit's first row answers to under MIDI follow: INTEGER, a MIDI note 0 to 127.
	static constexpr DevicePath kFollowKitRootNote{"midiFollow/kitRootNote"};
	/// Which channels MIDI follow sends its feedback on: CHOICE, none, channel A, B or C, the track's channel, or the
	/// track's with channel A, B or C.
	static constexpr DevicePath kFollowFeedbackChannel{"midiFollow/feedback/channel"};
	/// How often MIDI follow sends the values automation plays: CHOICE over FeedbackAutomation.
	static constexpr DevicePath kFollowFeedbackAutomation{"midiFollow/feedback/automation"};
	/// Whether MIDI follow ignores a CC that arrives within a second of the feedback it sent for that CC: BOOL.
	static constexpr DevicePath kFollowFeedbackFilter{"midiFollow/feedback/filter"};
	/// Whether a followed CC shows the param it moves on the display: BOOL.
	static constexpr DevicePath kFollowDisplayParam{"midiFollow/displayParam"};
	/// Whether a note a kit receives selects the kit's row it plays: BOOL.
	static constexpr DevicePath kMidiSelectKitRow{"midi/selectKitRow"};
	/// Whether the MIDI input is sent on to the MIDI outputs: BOOL.
	static constexpr DevicePath kMidiThru{"midi/thru"};
	/// How an incoming note transposes the clips MIDI transposes: CHOICE, in key or chromatic. The device may hold a
	/// third method no menu offers, which reads clamped to chromatic.
	static constexpr DevicePath kMidiTranspose{"midi/transpose"};
	/// How a knob's incoming CC takes over its param: CHOICE, jump, pick up, scale, relative.
	static constexpr DevicePath kMidiTakeover{"midi/takeover"};
	/// Whether a learned MIDI input answers only the device it was learned from: BOOL.
	static constexpr DevicePath kDifferentiateInputs{"midi/differentiateInputs"};
	/// How long a gate output stays off between two notes at least, in tenths of a millisecond: INTEGER.
	static constexpr DevicePath kGateMinOffTime{"gate/minOffTime"};
	/// The pattern of each CV output's volts-per-octave rows: INTEGER, in hundredths of a volt, 0 for Hz/V. The `*` is
	/// the output's index.
	static constexpr std::string_view kCvVoltsPerOctave = "cv/*/voltsPerOctave";
	/// The pattern of each CV output's transpose rows: INTEGER, in cents (a hundred a semitone). The `*` is the
	/// output's index.
	static constexpr std::string_view kCvTranspose = "cv/*/transpose";
	/// The pattern of each gate output's mode rows: CHOICE, V-trigger, S-trigger, then the clock on the clock's gate
	/// and the run signal on the run's. The `*` is the output's index.
	static constexpr std::string_view kGateMode = "gate/*/mode";
	/// The first part of each preset scale's "disabled by default" path; the scale's index follows it.
	static constexpr std::string_view kDisabledScalePrefix = "defaults/scales/disabled/";
	/// The first part of each community feature's path; the name the feature file writes it under follows it.
	static constexpr std::string_view kFeaturePrefix = "features/";

	/// @brief The BOOL row that says whether preset scale @p scale is disabled by default.
	/// @param scale A preset scale's index.
	/// @return The row's path, `defaults/scales/disabled/<scale>`, valid for the program's life; nullopt for an index
	///         past the preset scales.
	[[nodiscard]] static std::optional<DevicePath> disabled_scale(size_t scale);

	/// @brief CV output @p output's volts-per-octave row.
	/// @param output A CV output's index.
	/// @return The row's path, `cv/<output>/voltsPerOctave`, valid for the program's life; nullopt for an index past
	///         the CV outputs.
	[[nodiscard]] static std::optional<DevicePath> cv_volts_per_octave(size_t output);

	/// @brief CV output @p output's transpose row.
	/// @param output A CV output's index.
	/// @return The row's path, `cv/<output>/transpose`, valid for the program's life; nullopt for an index past the CV
	///         outputs.
	[[nodiscard]] static std::optional<DevicePath> cv_transpose(size_t output);

	/// @brief Gate output @p output's mode row.
	/// @param output A gate output's index.
	/// @return The row's path, `gate/<output>/mode`, valid for the program's life; nullopt for an index past the gate
	///         outputs.
	[[nodiscard]] static std::optional<DevicePath> gate_mode(size_t output);

	/// @brief The DEVICE row of community feature @p f: BOOL for an on/off feature, else CHOICE over its words.
	/// @param f A feature.
	/// @return The row's path, `features/` then the name the feature file writes it under.
	[[nodiscard]] static constexpr DevicePath feature(Feature f);
};

inline namespace menu_tables {

/// @brief A row path built when the image is, as text the table's views point into.
/// @tparam N The most characters the path can have.
template <size_t N>
struct FixedPath {
	std::array<char, N> text{}; ///< the path's characters, then unused room
	size_t size{0};             ///< how many characters of `text` it is

	/// @brief Adds @p c at the end; there must be room left.
	constexpr void append(char c) { text[size++] = c; }
	/// @brief Adds @p part at the end; it must fit in the room left.
	constexpr void append(std::string_view part) {
		for (const char c : part) {
			append(c);
		}
	}
	/// @brief The path, as a view of this one's text.
	[[nodiscard]] constexpr std::string_view view() const { return {text.data(), size}; }
};

} // namespace menu_tables

/// The menu-time tables: read when a menu looks a setting up, never by the audio tier or an interrupt. The device
/// image places this namespace's data in SDRAM (linker/sdram_sections.x matches its name).
namespace feature_paths::inline menu_tables {

/// @brief How long the longest feature's path is.
inline constexpr size_t kLongest = [] {
	size_t longest = 0;
	for (const FeatureInfo& info : kFeatures) {
		longest = std::max(longest, device_path::kFeaturePrefix.size() + info.path.size());
	}
	return longest;
}();

/// @brief Every feature's path, `features/<name>`, indexed by its id.
inline constexpr std::array<FixedPath<kLongest>, kFeatureCount> kPaths = [] {
	std::array<FixedPath<kLongest>, kFeatureCount> paths{};
	for (const FeatureInfo& info : kFeatures) {
		FixedPath<kLongest>& path = paths[std::to_underlying(info.id)];
		path.append(device_path::kFeaturePrefix);
		path.append(info.path);
	}
	return paths;
}();

} // namespace feature_paths::inline menu_tables

constexpr DevicePath device_path::feature(Feature f) {
	// Indexed through `feature_info`, so an id with no entry stops there rather than reading past the paths.
	return DevicePath{feature_paths::kPaths[std::to_underlying(feature_info(f).id)].view()};
}

/// @brief The community feature whose DEVICE row is at @p path.
/// @param path A DEVICE row's path.
/// @return The feature's entry, or null for a path no feature's row has.
[[nodiscard]] constexpr const FeatureInfo* feature_at(std::string_view path) {
	if (!path.starts_with(device_path::kFeaturePrefix)) {
		return nullptr;
	}
	return find_feature(path.substr(device_path::kFeaturePrefix.size()));
}

} // namespace deluge::project
