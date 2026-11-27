#include "gui/ui/shell_host/shell_host.h"

#include "definitions_cxx.hpp"
#include "gui/ui/browser/dx_cartridge_bank.h"
#include "hid/display/display.h"
#include "instruments/classic/state_access.h"
#include "io/midi/device_registry.h"
#include "model/edits/attach_recorded_sample.h"
#include "model/edits/edit.h"
#include "model/edits/set_engine_setting.h"
#include "model/edits/set_source_ranges.h"
#include "model/edits/set_velocity_to_level.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/kit.h"
#include "model/output.h"
#include "model/project_bridge/setting_apply.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "serialization/value.h"
#include "storage/DX7Cartridge.h"

#include <span>
#include <string>
#include <vector>

namespace deluge::gui::shell_host {
namespace {

/// The sound edits, applied to the current song.
class HostSoundWrites final : public deluge::ui::scenes::bind::SoundWrites {
public:
	bool write_ranges(project::SlotAddress slot, uint8_t source, std::span<const synth::SourceRange> ranges,
	                  deluge::ui::scenes::bind::RangesCause cause) override {
		if (currentSong == nullptr) {
			return false;
		}
		edits::SetSourceRanges edit{slot, source, {ranges.begin(), ranges.end()}};
		if (!applied(edit)) {
			return false;
		}
		if (cause.kind == deluge::ui::scenes::bind::RangesCause::Kind::MARKER) {
			// A marker moved: the sounding voices follow the zone, and the instrument counts as edited.
			::Sound* sound = bridge::soundAt(*currentSong, slot);
			bridge::zoneMoved(*sound, source, static_cast<MarkerType>(std::to_underlying(cause.marker)), *currentSong);
			Instrument* instrument = sound->isDrum() ? static_cast<Instrument*>(static_cast<SoundDrum*>(sound)->kit)
			                                         : static_cast<Instrument*>(static_cast<SoundInstrument*>(sound));
			if (instrument != nullptr) {
				instrument->beenEdited(true);
			}
		}
		return true;
	}

	void set_default_range(project::SlotAddress slot, uint8_t source, uint16_t range) override {
		::Sound* sound = sound_at(slot);
		if (sound != nullptr && source < kNumSources) {
			sound->sourceResources[source].defaultRangeI = range;
		}
	}

	bool insert_range(project::SlotAddress slot, uint8_t source, uint16_t at, int16_t top_note,
	                  std::optional<int16_t> previous_top) override {
		::Sound* sound = sound_at(slot);
		if (sound == nullptr || source >= kNumSources) {
			return false;
		}
		// `MultiRange::selectEncoderAction`'s insert (`origin/main:src/deluge/gui/menu_item/multi_range.cpp`): a
		// range with no file at @p at, the one below it narrowed
		const auto id = static_cast<instruments::classic::SourceId>(source);
		std::vector<synth::SourceRange> ranges = instruments::classic::source_ranges(sound->state(), id);
		if (at > ranges.size()) {
			return false;
		}
		// A new sample range's end is unset until a file gives it one, as a runtime range starts; a wavetable range
		// has no zone
		const bool sample = instruments::classic::source_type(sound->state(), id) == OscType::SAMPLE;
		ranges.insert(
		    ranges.begin() + at,
		    synth::SourceRange{.top_note = top_note, .zone = {.end = sample ? instruments::classic::kUnsetEndPos : 0}});
		if (previous_top && at > 0) {
			ranges[at - 1].top_note = *previous_top;
		}
		const Error error = edits::rangesFor(*sound, source, std::move(ranges));
		if (error == Error::INSUFFICIENT_RAM) {
			display->displayError(Error::INSUFFICIENT_RAM);
		}
		return error == Error::NONE;
	}

	bool delete_range(project::SlotAddress slot, uint8_t source, uint16_t index,
	                  std::optional<int16_t> lower_top) override {
		::Sound* sound = sound_at(slot);
		if (sound == nullptr || source >= kNumSources) {
			return false;
		}
		// `MultiRange::deletePress` (`origin/main:src/deluge/gui/menu_item/multi_range.cpp`): the range goes, the one
		// below it widened
		std::vector<synth::SourceRange> ranges =
		    instruments::classic::source_ranges(sound->state(), static_cast<instruments::classic::SourceId>(source));
		if (index >= ranges.size()) {
			return false;
		}
		ranges.erase(ranges.begin() + index);
		if (lower_top && index > 0) {
			ranges[index - 1].top_note = *lower_top;
		}
		return edits::rangesFor(*sound, source, std::move(ranges)) == Error::NONE;
	}

	bool attach_recording(project::SlotAddress slot, uint8_t source, uint16_t range, std::string_view path) override {
		if (currentSong == nullptr) {
			return false;
		}
		edits::AttachRecordedSample edit{slot, source, range, std::string{path}};
		return applied(edit);
	}

	bool write_dx_patch(project::SlotAddress slot, uint8_t source,
	                    const instruments::classic::DxPatchState& patch) override {
		::Sound* sound = sound_at(slot);
		if (sound == nullptr || source >= kNumSources || sound->oscType(source) != OscType::DX7) {
			return false;
		}
		// The `SetDxPatch` Edit legacy's DX7 pages write through (`DxParam::setValue`,
		// `origin/main:src/deluge/gui/menu_item/dx/param.cpp`), which records no undo.
		edits::dxPatchFor(*sound, source, patch);
		return true;
	}

	bool load_dx_program(project::SlotAddress slot, uint8_t source, int32_t program) override {
		::Sound* sound = sound_at(slot);
		DX7Cartridge* bank = dxCartridgeBank();
		if (sound == nullptr || bank == nullptr || source >= kNumSources || sound->oscType(source) != OscType::DX7
		    || program < 0 || program >= bank->numPatches()) {
			return false;
		}
		loadDxProgram(*sound, source, *bank, program);
		return true;
	}

	bool write_source_transpose(const deluge::ui::scenes::bind::Addresses& where, uint8_t source,
	                            std::optional<uint16_t> range, int32_t menu_value) override {
		if (currentSong == nullptr || source >= kNumSources) {
			return false;
		}
		// `computeFinalValuesForTranspose` (`util/value_scaling.cpp`).
		const int32_t current = menu_value + 25600;
		const int32_t semitones = (current + 50) / 100;
		const int32_t cents = current - semitones * 100;
		const int32_t transpose = semitones - 256;
		// One sound: a sample source with ranges (not FM) in its range, any other source in its own setting.
		const auto one = [&](project::SlotAddress slot, std::optional<uint16_t> in_range) {
			::Sound* sound = sound_at(slot);
			if (sound == nullptr) {
				return false;
			}
			const instruments::classic::SoundState& state = sound->state();
			const auto id = static_cast<instruments::classic::SourceId>(source);
			const std::vector<synth::SourceRange> held = instruments::classic::source_ranges(state, id);
			if (!held.empty() && instruments::classic::synth_mode(state) != instruments::classic::SynthMode::FM
			    && instruments::classic::source_type(state, id) == instruments::classic::OscType::SAMPLE) {
				std::vector<synth::SourceRange> ranges{held.begin(), held.end()};
				const size_t r = in_range && *in_range < ranges.size() ? *in_range : 0;
				ranges[r].tuning.transpose = static_cast<int16_t>(transpose);
				ranges[r].tuning.cents = static_cast<int8_t>(cents);
				edits::SetSourceRanges edit{slot, source, std::move(ranges)};
				return applied(edit);
			}
			const std::string osc = "osc" + std::to_string(source + 1) + "/";
			edits::SetEngineSetting semis{slot, osc + "transpose", serialization::Value::makeNumber(transpose)};
			edits::SetEngineSetting fine{slot, osc + "cents", serialization::Value::makeNumber(cents)};
			const bool took = applied(semis);
			return applied(fine) && took;
		};
		if (where.affect_entire && where.kit_output) {
			// Every sound row of the kit, each in its range 0
			// (`origin/main:src/deluge/gui/menu_item/sample/transpose.h`'s kit sweep).
			::Output* output = currentSong->outputForId(*where.kit_output);
			if (output == nullptr || output->type != OutputType::KIT) {
				return false;
			}
			bool took = true;
			for (::Drum* drum = static_cast<::Kit*>(output)->firstDrum; drum != nullptr; drum = drum->next) {
				if (drum->type == DrumType::SOUND && drum->id()) {
					took = one({project::SlotAddress::Kind::DRUM, *drum->id()}, std::nullopt) && took;
				}
			}
			return took;
		}
		return where.slot && one(*where.slot, range);
	}

	bool velocity_to_level_everywhere(midi::DeviceId device) override {
		::MIDICable* const cable = midi::cable_of(device);
		if (currentSong == nullptr || cable == nullptr) {
			return false;
		}
		edits::SetVelocityToLevelEverywhere edit{*cable};
		return applied(edit);
	}

private:
	/// The sound @p slot addresses in the current song, or null.
	static ::Sound* sound_at(project::SlotAddress slot) {
		return currentSong != nullptr ? bridge::soundAt(*currentSong, slot) : nullptr;
	}
};

} // namespace

deluge::ui::scenes::bind::SoundWrites& sound_writes() {
	static HostSoundWrites writes;
	return writes;
}

} // namespace deluge::gui::shell_host
