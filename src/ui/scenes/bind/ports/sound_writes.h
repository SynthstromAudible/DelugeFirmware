#pragma once

/// @file
/// @brief Where a scene's edits of a sound's sources go: its ranges, the take a range records, its DX7 patch and its
///        transpose.

#include "instruments/classic/sound_state.h"
#include "midi/device_id.h"
#include "project/instrument_slot.h"
#include "project/sample_marker.h"
#include "ui/scenes/bind/target.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace deluge::ui::scenes::bind {

/// @brief Why a source's ranges are written: a plain edit, or a marker the marker editor moved (which a sounding voice
///        follows, and which counts as editing the instrument).
struct RangesCause {
	/// @brief The reason a source's ranges are written.
	enum class Kind : uint8_t {
		EDIT,   ///< a plain edit
		MARKER, ///< a marker the marker editor moved
	};
	Kind kind{Kind::EDIT};                                      ///< why the ranges are written
	project::SampleMarker marker{project::SampleMarker::START}; ///< the moved marker, for a MARKER cause
	bool operator==(const RangesCause&) const = default;
};

/// @brief Takes a scene's sound edits. The app applies each; a spec records them.
class SoundWrites {
public:
	virtual ~SoundWrites() = default;
	/// @brief Write source @p source's ranges of the sound at @p slot.
	/// @return Whether it was taken.
	virtual bool write_ranges(project::SlotAddress slot, uint8_t source, std::span<const synth::SourceRange> ranges,
	                          RangesCause cause) = 0;
	/// @brief Make @p range the range source @p source of the sound at @p slot remembers
	/// (`SourceResources::defaultRangeI`): runtime state, as legacy's picker writes it, not an edit.
	virtual void set_default_range(project::SlotAddress slot, uint8_t source, uint16_t range) = 0;
	/// @brief Insert a range at @p at into source @p source of the sound at @p slot, with top note @p top_note, and
	/// give the range below it @p previous_top when set: `MultiRange::selectEncoderAction`'s insert, the voices killed
	/// and the audio routine locked around the insert, the samples the other ranges hold kept.
	/// @return Whether it was taken: false on a RAM failure, which the host reports.
	virtual bool insert_range(project::SlotAddress slot, uint8_t source, uint16_t at, int16_t top_note,
	                          std::optional<int16_t> previous_top) = 0;
	/// @brief Delete range @p index of source @p source of the sound at @p slot, and give the range below it
	/// @p lower_top when set: `MultiRange::deletePress` (`origin/main:src/deluge/gui/menu_item/multi_range.cpp`), the
	/// range's voices unassigned.
	/// @return Whether it was taken.
	virtual bool delete_range(project::SlotAddress slot, uint8_t source, uint16_t index,
	                          std::optional<int16_t> lower_top) = 0;
	/// @brief Attach the recorded take at @p path (a sample in memory, never read from the card) to range @p range of
	/// source @p source of the sound at @p slot, which becomes a sample source.
	/// @return Whether it was taken: false where no take at @p path is in memory or the address does not resolve.
	virtual bool attach_recording(project::SlotAddress slot, uint8_t source, uint16_t range, std::string_view path) = 0;
	/// @brief Make @p patch the DX7 patch of source @p source of the sound at @p slot: its voice bytes, engine mode and
	/// random detune, as the DX7 menu pages write them. Not undoable, as legacy's DX7 edits are not.
	/// @return Whether it was taken: false where the address does not resolve or the source is not a DX7 one.
	virtual bool write_dx_patch(project::SlotAddress slot, uint8_t source,
	                            const instruments::classic::DxPatchState& patch) = 0;
	/// @brief Load program @p program of the DX7 bank the last `.syx` read loaded into source @p source of the sound at
	/// @p slot, as the cartridge page's turn does: the patch unpacked, the voices cut, and an unsaved synth renamed to
	/// the program. Not undoable, as legacy's is not.
	/// @return Whether it was taken: false where no bank is loaded, @p program is not one of its programs, the address
	/// does not resolve or the source is not a DX7 one.
	virtual bool load_dx_program(project::SlotAddress slot, uint8_t source, int32_t program) = 0;
	/// @brief Transpose source @p source of the sound @p where names -- every sound row of the kit under affect-entire
	/// -- by @p menu_value, in cents. A sample source with ranges (not FM) takes it in its range: @p range, or range 0
	/// on a whole kit; any other source in its own transpose and cents.
	/// @return Whether it was taken.
	virtual bool write_source_transpose(const Addresses& where, uint8_t source, std::optional<uint16_t> range,
	                                    int32_t menu_value) = 0;
	/// @brief Give every synth and sound kit row whose input is @p device its velocity-to-level default, in every
	///        clip: the device menu's "for everything", a default set up for the user with no undo step.
	/// @param device The device whose inputs take its default.
	/// @return Whether it was taken: false where @p device names no cable or no song is loaded.
	virtual bool velocity_to_level_everywhere(midi::DeviceId device) = 0;
};

} // namespace deluge::ui::scenes::bind
