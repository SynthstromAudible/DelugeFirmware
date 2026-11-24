#include "board_controls.hpp"
#include "definitions_cxx.hpp"
#include "effects/settings.h"
#include "gui/ui/shell_host/shell_host.h"
#include "gui/ui/ui.h"
#include "gui/views/automation_view.h"
#include "gui/views/performance_view.h"
#include "gui/views/view.h"
#include "hid/display/oled.h"
#include "instruments/classic/cables.h"
#include "instruments/classic/settings.h"
#include "instruments/classic/words.h"
#include "io/debug/log.h"
#include "model/action/action.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/drum/drum.h"
#include "model/edits/delete_param_automation.h"
#include "model/edits/edit.h"
#include "model/edits/set_cable_polarity.h"
#include "model/edits/set_chain_view.h"
#include "model/edits/set_controls.h"
#include "model/edits/set_effect_setting.h"
#include "model/edits/set_engine_setting.h"
#include "model/edits/set_kit_lane_setting.h"
#include "model/edits/set_kit_param_value.h"
#include "model/edits/set_kit_setting.h"
#include "model/edits/set_mod_knob_mode.h"
#include "model/edits/set_param_value.h"
#include "model/edits/set_patch_cable.h"
#include "model/edits/set_setting.h"
#include "model/instrument/kit.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/mapping_bridge.h"
#include "model/project_bridge/param_stack.h"
#include "model/project_bridge/setting_projection.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_set.h"
#include "plugin/types/setting.h"
#include "processing/engines/audio_engine.h"
#include "project/setting_group.h"
#include "ui/scenes/bind/dispatch.h"
#include "ui/scenes/bind/knob_assignment.h"
#include "ui/scenes/bind/knob_focus.h"

#include <array>
#include <span>
#include <string>
#include <utility>
#include <variant>

// The input a new audio track starts on: unsaved song state (`model/song/song.cpp`), set by the audio-source menu.
extern AudioInputChannel defaultAudioOutputInputChannel;

namespace deluge::gui::shell_host {

namespace {

using deluge::ui::scenes::bind::Addresses;
using deluge::ui::scenes::bind::WriteRequest;

/// @brief The file form of a stored value: a CHOICE row's word, any other row's number.
/// @param row    The row the value is of.
/// @param stored The value, as the row stores it.
/// @return The file value; none for a CHOICE value the row has no word for.
std::optional<serialization::Value> value_of(const plugin::SettingInfo& row, int32_t stored) {
	if (row.kind == plugin::SettingKind::CHOICE) {
		std::array<char, 1> unused{};
		const std::optional<std::string_view> word = plugin::stored_text(row, stored, unused);
		return word ? std::optional{serialization::Value::makeScalar(std::string(*word))} : std::nullopt;
	}
	return serialization::Value::makeNumber(stored);
}

/// @brief Whether writing a value ends a kit row's notes: a sample played once (`Repeat::writeCurrentValue` in
///        `origin/main:src/deluge/gui/menu_item/sample/repeat.h`: its whole-kit arm and its one-sound arm), whose notes
///        the kit's arpeggiator would otherwise hold.
/// @param target The target written through.
/// @param row    The row written.
/// @param stored The value written, as the row stores it.
/// @return True for a sound's loop mode set to play once.
bool silences_kit_arp(const deluge::ui::scenes::bind::Target& target, const plugin::SettingInfo& row, int32_t stored) {
	if (target.kind != deluge::ui::scenes::bind::Target::Kind::SOUND || !row.path.ends_with("/loopMode")) {
		return false;
	}
	return stored
	       == std::to_underlying(instruments::classic::SampleRepeatMode::ONCE); // a loop mode is saved as its index
}

/// @brief Whether @p kit has a sound drum: legacy's loop-mode note-off is sent from inside its loop over them.
/// @param kit The kit.
/// @return True when one of its drums is a sound.
bool has_sound_drum(const Kit& kit) {
	for (const Drum* drum = kit.firstDrum; drum != nullptr; drum = drum->next) {
		if (drum->type == DrumType::SOUND) {
			return true;
		}
	}
	return false;
}

/// @brief Send @p kit's selected drum's note-off through the kit's arpeggiator, as
///        `origin/main:src/deluge/gui/menu_item/sample/repeat.h`'s sendNoteOffForKitArpeggiator does.
/// @param kit The kit, or null for none.
void kit_arp_note_off(Kit* kit) {
	if (kit == nullptr || kit->selectedDrum == nullptr) {
		return;
	}
	// The clip the kit plays, else the one the menu was opened over, as legacy's current clip.
	Clip* clip = kit->getActiveClip();
	if (clip == nullptr && currentSong->getCurrentClip() != nullptr && currentSong->getCurrentClip()->output == kit) {
		clip = currentSong->getCurrentClip();
	}
	if (clip == nullptr || clip->type != ClipType::INSTRUMENT) {
		return;
	}
	int32_t note_row_index = 0;
	NoteRow* note_row = static_cast<InstrumentClip*>(clip)->getNoteRowForDrum(kit->selectedDrum, &note_row_index);
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	auto* stack = reinterpret_cast<ModelStack*>(memory);
	ModControllable* mod = kit->selectedDrum->toModControllable();
	ParamManager* params = note_row != nullptr ? &note_row->paramManager : nullptr;
	kit->noteOffPreKitArp(
	    stack->addTimelineCounter(clip)->addNoteRow(note_row_index, note_row)->addOtherTwoThings(mod, params),
	    kit->selectedDrum);
}

/// @brief Whether another of @p owner's siblings already has @p name: another clip of the clip's track, another audio
///        track, another drum of the lane's kit, as legacy's rename editors refuse a name (`RenameClipUI::trySetName`
///        in `rename_clip_ui.cpp`, `RenameOutputUI::trySetName` in `rename_output_ui.cpp`, main's `RenameDrumUI`,
///        `origin/main:src/deluge/gui/ui/rename/rename_drum_ui.cpp`). Letter case does not count.
/// @param owner The owner the name is for.
/// @param name  The name.
/// @return True when a sibling has it.
bool name_taken(const project::SettingAddress& owner, std::string_view name) {
	if (const auto* clip_at = std::get_if<project::ClipAddress>(&owner.owner)) {
		::Clip* clip = currentSong->clipForId(clip_at->clip_id);
		::Clip* other = clip != nullptr && clip->output != nullptr ? clip->output->getClipFromName(name) : nullptr;
		return other != nullptr && other != clip;
	}
	if (const auto* track = std::get_if<project::TrackAddress>(&owner.owner)) {
		void* other = currentSong->getAudioOutputFromName(name);
		return other != nullptr && other != currentSong->outputForId(track->output_id);
	}
	if (const auto* lane = std::get_if<project::LaneAddress>(&owner.owner)) {
		auto* kit = static_cast<::Kit*>(currentSong->outputForId(lane->kit_id));
		::Drum* drum = bridge::drumWithId(*currentSong, lane->drum_id);
		::Drum* other = kit != nullptr && kit->type == OutputType::KIT ? kit->getDrumFromName(name) : nullptr;
		return other != nullptr && other != drum;
	}
	return false;
}

/// @brief The instrument a focus's knobs edit: a synth, a kit row's kit, a kit's own chain; none for an audio track, a
///        MIDI track (whose edited flag is no instrument's) or the song.
/// @param song  The song.
/// @param focus The knobs' focus.
/// @return The instrument, or null.
Instrument* knob_instrument_of(::Song& song, const deluge::ui::controls::KnobFocus& focus) {
	namespace bind = deluge::ui::scenes::bind;
	std::optional<uint32_t> id;
	if (const auto* sound = std::get_if<bind::SoundFocus>(&focus.owner)) {
		id = sound->kit ? sound->kit : std::optional{sound->slot.id};
	}
	else if (const auto* chain = std::get_if<bind::ChainFocus>(&focus.owner);
	         chain && chain->owner == bind::ChainOwner::KIT) {
		id = chain->chain.id;
	}
	Output* output = id ? bridge::outputWithId(song, *id) : nullptr;
	return output != nullptr && output->type != OutputType::AUDIO ? static_cast<Instrument*>(output) : nullptr;
}

/// @brief The lane of @p clip that a MIDI track's knob sending @p cc moves. A CC 1 moves CC lane 1, as legacy's
///        `getParamToControlFromInputMIDIChannel` does, though `knob_push` names it the mod wheel.
/// @param clip The MIDI clip.
/// @param cc   What the knob sends.
/// @return The lane; none for a knob that sends nothing.
std::optional<project::ParamAddress> midi_knob_lane(project::ClipAddress clip, const project::ModKnobCc& cc) {
	using Kind = project::ModKnobCc::Kind;
	using project::ParamSpace;
	const auto lane = [clip](ParamSpace space, uint32_t id) {
		return project::ParamAddress::of(project::OwnerId::clip(clip.clip_id), {space, plugin::ParamId{id}});
	};
	switch (cc.kind) {
	case Kind::BEND:
		return lane(ParamSpace::EXPRESSION, 0);
	case Kind::AFTERTOUCH:
		return lane(ParamSpace::EXPRESSION, 2);
	case Kind::NUMBER:
		return cc.number == project::ModKnobCc::kYAxisNumber ? lane(ParamSpace::EXPRESSION, 1)
		                                                     : lane(ParamSpace::MIDI_CC, cc.number);
	case Kind::NONE:
		break;
	}
	return std::nullopt;
}

} // namespace

HostEditSink& edit_sink() {
	static HostEditSink sink;
	return sink;
}

uint32_t edit_refusals() {
	return edit_sink().refusals();
}

void HostEditSink::write_param_target(const WriteRequest& request) {
	const std::optional<project::ParamAddress> address =
	    deluge::ui::scenes::bind::param_address(request.target, request.where);
	if (!address) {
		refusals_++;
		D_PRINTLN("menu write refused: param %u has no owner", static_cast<unsigned>(request.target.param.value));
		return;
	}
	edits::Context ctx{*currentSong, currentSong->project};
	Error error = Error::NONE;
	const Addresses& where = request.where;
	if (where.affect_entire && where.kit_output && request.target.whole_kit && where.clip && where.slot
	    && where.slot->kind == project::SlotAddress::Kind::DRUM) {
		// Legacy's affect-entire loop: every sound drum of the kit with a row in the clip, as one Edit.
		edits::SetKitParamValue edit{*where.kit_output, where.clip->clip_id, address->id, request.param_value};
		error = edits::apply(ctx, edit);
	}
	else {
		edits::SetParamValue edit{*address, request.param_value};
		error = edits::apply(ctx, edit);
	}
	if (error != Error::NONE) {
		refusals_++;
		D_PRINTLN("menu write refused: param %u", static_cast<unsigned>(address->id.value));
		return;
	}
	// MIDI follow's feedback for the param written, as legacy's param items send it after every write.
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	if (ModelStackWithAutoParam* stack = bridge::paramStackAt(*currentSong, *address, memory)) {
		view.sendMidiFollowFeedback(stack, stack->paramCollection->paramValueToKnobPos(request.param_value, stack));
	}
}

void HostEditSink::write_cable_target(const WriteRequest& request) {
	const std::optional<project::ParamAddress> address =
	    deluge::ui::scenes::bind::param_address(request.target, request.where);
	if (!address) {
		refusals_++;
		D_PRINTLN("menu write refused: cable %x has no owner", static_cast<unsigned>(request.target.param.value));
		return;
	}
	const instruments::classic::Polarity polarity = request.polarity;
	if (request.polarity_only) {
		edits::SetCablePolarity edit{*address, polarity};
		counted_apply(edit);
		return;
	}
	// No MIDI-follow feedback: legacy's strength page sends none, and no CC follows a cable.
	edits::SetPatchCable edit{*address, request.param_value, polarity};
	counted_apply(edit);
}

bool HostEditSink::counted_apply(edits::Edit& edit) {
	if (!applied(edit)) {
		refusals_++;
		return false;
	}
	return true;
}

bool HostEditSink::write_param(const project::ParamAddress& address, int32_t value, int32_t pos, int32_t length,
                               deluge::ui::controls::Resolution resolution) {
	if (currentSong == nullptr) {
		return false;
	}
	// What the knob turns is made and a take recording the clip is written; a gold knob's write also marks the
	// instrument edited, a learned knob's does not.
	using Resolve = edits::SetParamValue::Resolve;
	edits::SetParamValue edit{address, value, pos, length,
	                          resolution == deluge::ui::controls::Resolution::LEARNED ? Resolve::LEARNED
	                                                                                  : Resolve::WRITE};
	return counted_apply(edit);
}

void HostEditSink::learned_written(const project::ParamAddress& address, int32_t knob_pos) {
	RootUI* root_ui = getRootUI();
	const bool on_automation = root_ui == &automationView;
	if (currentSong == nullptr || (!on_automation && root_ui != &performanceView)) {
		return;
	}
	const bool of_song = address.owner.kind == project::OwnerId::Kind::SONG;
	// A clip's knob refreshes only the automation editor, and only on the clip it shows.
	if (!of_song && (!on_automation || automationView.onArrangerView)) {
		return;
	}
	const std::optional<uint32_t> clip_id =
	    address.owner.kind == project::OwnerId::Kind::CLIP ? std::optional{address.owner.id} : address.clip_id;
	::Clip* clip = clip_id ? currentSong->clipForId(*clip_id) : nullptr;
	if (!of_song && (clip == nullptr || clip != getCurrentClip())) {
		return;
	}
	const std::optional<bridge::ParamLane> lane = bridge::paramLaneFollowingTake(*currentSong, address);
	if (!lane) {
		return;
	}
	if (on_automation) {
		automationView.possiblyRefreshAutomationEditorGrid(of_song ? nullptr : clip, lane->kind, lane->id);
	}
	else {
		performanceView.possiblyRefreshPerformanceViewDisplay(lane->kind, lane->id, knob_pos);
	}
}

std::optional<uint32_t> HostEditSink::take_for(uint32_t output_id) {
	return currentSong != nullptr ? bridge::takeFor(*currentSong, output_id) : std::nullopt;
}

bool HostEditSink::make_param(const project::ParamAddress& address) {
	if (currentSong == nullptr) {
		return false;
	}
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	// Only a cable's depth is made, as a learned knob that reads it makes it (resolved as a learned write resolves it,
	// which records the cable as an undo step); any other param is only looked for.
	if (address.space != project::ParamSpace::ENGINE || !instruments::classic::is_cable_depth(address.id)) {
		const ModelStackWithAutoParam* found = bridge::paramStackFollowingTake(*currentSong, address, memory);
		return found != nullptr && found->autoParam != nullptr;
	}
	project::ParamAddress landed = address;
	const ModelStackWithAutoParam* stack = bridge::paramStackForWrite(*currentSong, landed, memory);
	return stack != nullptr && stack->autoParam != nullptr;
}

void HostEditSink::knob_absolute(const project::OwnerParamTarget& target, const project::MidiCommand& source) {
	if (currentSong != nullptr) {
		bridge::mappingNoLongerRelative(target, source, *currentSong);
	}
}

bool HostEditSink::delete_automation(const project::ParamAddress& address) {
	if (currentSong == nullptr) {
		return false;
	}
	edits::DeleteParamAutomation edit{address};
	return counted_apply(edit);
}

bool HostEditSink::write_text(const deluge::ui::scenes::bind::Target& target, const Addresses& where,
                              std::string_view text) {
	if (currentSong == nullptr) {
		return false;
	}
	const std::optional<project::SettingAddress> address = deluge::ui::scenes::bind::setting_address(target, where);
	if (target.kind != deluge::ui::scenes::bind::Target::Kind::MODEL || !address) {
		refusals_++;
		return false;
	}
	if (target.path == "name" && name_taken(*address, text)) {
		refusals_++;
		return false;
	}
	edits::SetSetting edit{*address, std::string{target.path}, serialization::Value::makeScalar(std::string{text})};
	return counted_apply(edit);
}

void HostEditSink::set_default_audio_input_channel(int32_t channel) {
	defaultAudioOutputInputChannel = static_cast<AudioInputChannel>(channel);
}

void HostEditSink::set_song_threshold_recording(int32_t mode) {
	if (currentSong != nullptr) {
		currentSong->thresholdRecordingMode = static_cast<ThresholdRecordingMode>(mode);
	}
}

void HostEditSink::set_reverb_sidechain_sync_type(plugin::SyncType type) {
	AudioEngine::reverbSidechainSettings.sync_type = type;
}

bool HostEditSink::write(const WriteRequest& request) {
	if (currentSong == nullptr || request.target.kind == deluge::ui::scenes::bind::Target::Kind::NONE) {
		return false;
	}
	const uint32_t refused = refusals_;
	apply(request);
	return refusals_ == refused;
}

void HostEditSink::apply(const WriteRequest& request) {
	using Kind = deluge::ui::scenes::bind::Target::Kind;
	const Kind kind = request.target.kind;
	if (kind == Kind::CABLE) {
		write_cable_target(request);
		return;
	}
	if (kind == Kind::PARAM) {
		write_param_target(request);
		return;
	}
	// A model group's record (a MODEL target, or a sound setting on an audio clip), else a sound's or a chain's.
	const std::optional<project::SettingAddress> model =
	    deluge::ui::scenes::bind::setting_address(request.target, request.where);
	if (kind == Kind::MODEL && !model) {
		refusals_++;
		D_PRINTLN("menu write refused: %.*s has no owner", static_cast<int>(request.target.path.size()),
		          request.target.path.data());
		return;
	}
	const std::span<const plugin::SettingInfo> table = model                  ? project::table_of(model->group)
	                                                   : kind == Kind::EFFECT ? effects::kEffectSettings
	                                                                          : instruments::classic::kSettings;
	const std::string_view path = model && kind == Kind::SOUND ? request.target.audio_clip_path : request.target.path;
	const plugin::SettingInfo* row = plugin::find_setting(table, path);
	const auto stored = row != nullptr ? plugin::from_menu(*row, request.menu_value) : std::nullopt;
	if (!stored) {
		refusals_++;
		D_PRINTLN("menu write refused: %.*s menu %d", static_cast<int>(request.target.path.size()),
		          request.target.path.data(), static_cast<int>(request.menu_value));
		return;
	}
	const std::string_view partner = plugin::mapping_of(*row).partner_path();
	const plugin::SettingInfo* partner_row = partner.empty() ? nullptr : plugin::find_setting(table, partner);

	// Commits @p row's value, then its partner's, each as the Edit @p edit_for makes for a path and a file value.
	const auto commit_both = [&](edits::Context& ctx, const auto& edit_for) {
		const auto commit = [&](const plugin::SettingInfo& on, int32_t value) {
			const std::optional<serialization::Value> file = value_of(on, value);
			if (!file) {
				refusals_++;
				D_PRINTLN("menu write refused: %.*s has no word for %d", static_cast<int>(on.path.size()),
				          on.path.data(), static_cast<int>(value));
				return;
			}
			auto edit = edit_for(std::string(on.path), *file);
			if (edits::apply(ctx, edit) != Error::NONE) {
				refusals_++;
				D_PRINTLN("menu write refused: %.*s = %d", static_cast<int>(on.path.size()), on.path.data(),
				          static_cast<int>(value));
			}
		};
		commit(*row, stored->first);
		if (partner_row != nullptr) {
			commit(*partner_row, stored->second);
		}
	};

	if (model && model->group == project::SettingGroupId::LANE_ARP && request.where.affect_entire
	    && request.target.whole_kit && request.where.kit_output) {
		// A kit row's arp with affect-entire held: every lane that takes it, as legacy's loops write them.
		edits::Context ctx{*currentSong, currentSong->project};
		const bool needs_note_tails =
		    request.target.fan_out_fact == deluge::ui::scenes::bind::Target::FanOutFact::NOTE_TAILS;
		commit_both(ctx, [&](std::string on, const serialization::Value& value) {
			return edits::SetKitLaneSetting{*request.where.kit_output, std::move(on), value, needs_note_tails};
		});
		return;
	}
	if (model && model->group == project::SettingGroupId::LANE_ARP
	    && request.target.fan_out_fact == deluge::ui::scenes::bind::Target::FanOutFact::NOTE_TAILS) {
		// A kit row whose sound cannot hold note tails stays out of its kit's arp (`IncludeInKitArp::writeCurrentValue`
		// in `origin/main:src/deluge/gui/menu_item/arpeggiator/include_in_kit_arp.h`).
		const auto& lane = std::get<project::LaneAddress>(model->owner);
		::Drum* drum = bridge::drumWithId(*currentSong, lane.drum_id);
		if (drum != nullptr && drum->type == DrumType::SOUND
		    && !control_facts().allows_note_tails({project::SlotAddress::Kind::DRUM, lane.drum_id}, std::nullopt)) {
			refusals_++;
			D_PRINTLN("menu write refused: kit row %u cannot hold note tails for its kit's arp",
			          static_cast<unsigned>(lane.drum_id));
			return;
		}
	}
	if (model && model->group == project::SettingGroupId::SOUND_ROUTING && request.where.affect_entire
	    && request.target.whole_kit && request.where.kit_output) {
		// A sound's routing with affect-entire held: every sound row of the kit that plays an instrument, as a sound
		// setting's affect-entire writes them (SetKitSetting)
		Kit* kit = bridge::kitAt(*currentSong, *request.where.kit_output);
		if (kit == nullptr) {
			refusals_++;
			return;
		}
		edits::Context ctx{*currentSong, currentSong->project};
		for (Drum* drum = kit->firstDrum; drum != nullptr; drum = drum->next) {
			const auto lane = drum->type == DrumType::SOUND ? bridge::laneAddressOf(*drum, *currentSong) : std::nullopt;
			if (!lane || project::sound_routing(currentSong->project, *lane) == nullptr) {
				continue;
			}
			const project::SettingAddress address{project::SettingGroupId::SOUND_ROUTING, *lane};
			commit_both(ctx, [&](std::string on, const serialization::Value& value) {
				return edits::SetSetting{address, std::move(on), value};
			});
		}
		return;
	}
	if (model) {
		bridge::seedArpRecordAt(*currentSong, *model); // an arp's writers seed its record from the arp first
		edits::Context ctx{*currentSong, currentSong->project};
		commit_both(ctx, [&](std::string on, const serialization::Value& value) {
			return edits::SetSetting{*model, std::move(on), value};
		});
		return;
	}

	const auto apply_to = [&](project::SlotAddress slot) {
		if (bridge::soundAt(*currentSong, slot) == nullptr) {
			refusals_++;
			D_PRINTLN("menu write refused: no sound at the slot for %.*s", static_cast<int>(request.target.path.size()),
			          request.target.path.data());
			return;
		}
		edits::Context ctx{*currentSong, currentSong->project};
		commit_both(ctx, [&](std::string path, const serialization::Value& value) {
			return edits::SetEngineSetting{slot, std::move(path), value};
		});
	};

	if (request.where.affect_entire && request.target.whole_kit && request.where.kit_output) {
		Kit* kit = bridge::kitAt(*currentSong, *request.where.kit_output);
		if (kit == nullptr) {
			refusals_++;
			D_PRINTLN("menu write refused: no kit with id %u for %.*s",
			          static_cast<unsigned>(*request.where.kit_output), static_cast<int>(request.target.path.size()),
			          request.target.path.data());
			return;
		}
		if (silences_kit_arp(request.target, *row, stored->first) && has_sound_drum(*kit)) {
			kit_arp_note_off(kit);
		}
		edits::Context ctx{*currentSong, currentSong->project};
		commit_both(ctx, [&](std::string path, const serialization::Value& value) {
			return edits::SetKitSetting{*request.where.kit_output, std::move(path), value, request.target.fan_out_when};
		});
		return;
	}
	if (request.where.slot) {
		if (silences_kit_arp(request.target, *row, stored->first) && request.where.kit_output) {
			kit_arp_note_off(bridge::kitAt(*currentSong, *request.where.kit_output));
		}
		apply_to(*request.where.slot);
		return;
	}
	if (kind == Kind::EFFECT && request.where.chain) {
		edits::Context ctx{*currentSong, currentSong->project};
		commit_both(ctx, [&](std::string path, const serialization::Value& value) {
			return edits::SetEffectSetting{*request.where.chain, std::move(path), value};
		});
		return;
	}
	refusals_++;
	D_PRINTLN("menu write refused: %.*s has no slot or chain", static_cast<int>(request.target.path.size()),
	          request.target.path.data());
}

bool HostEditSink::knob_mode(project::KnobModeOwner owner, uint8_t mode) {
	return edits::modKnobModeFor(owner, mode);
}

bool HostEditSink::chain_view(project::ChainAddress chain, std::string_view key, std::string_view word) {
	if (currentSong == nullptr) {
		return false;
	}
	edits::SetChainView edit{chain, std::string{key}, serialization::Value::makeScalar(std::string{word})};
	return counted_apply(edit);
}

bool HostEditSink::write_controls(project::ControlsAddress owner, std::vector<project::ControllerMapping> controls) {
	if (currentSong == nullptr) {
		return false;
	}
	edits::SetControls edit{owner, std::move(controls)};
	return counted_apply(edit);
}

bool HostEditSink::stutter(const deluge::ui::controls::KnobFocus& focus, bool begin) {
	const std::optional<project::ParamAddress> owner = deluge::ui::scenes::bind::knob_owner(focus);
	return currentSong != nullptr && owner && bridge::stutterAt(*currentSong, *owner, begin);
}

bool HostEditSink::grab_region(const deluge::ui::controls::KnobFocus& focus, uint32_t pos) {
	const std::optional<project::ParamAddress> owner = deluge::ui::scenes::bind::knob_owner(focus);
	return currentSong != nullptr && owner && bridge::grabRegionAt(*currentSong, *owner, pos);
}

bool HostEditSink::write_effect(const deluge::ui::scenes::bind::EffectOwner& owner, std::string_view path,
                                const serialization::Value& value) {
	if (currentSong == nullptr) {
		return false;
	}
	if (const auto* slot = std::get_if<project::SlotAddress>(&owner)) {
		edits::SetEngineSetting edit{*slot, std::string{path}, value};
		return counted_apply(edit);
	}
	edits::SetEffectSetting edit{std::get<project::ChainAddress>(owner), std::string{path}, value};
	return counted_apply(edit);
}

bool HostEditSink::midi_cc_select(const deluge::ui::controls::KnobFocus& focus, int32_t knob, bool on) {
	if (!on) {
		// Another mode may be held over the choice (a held horizontal encoder): end only this one
		if (isUIModeActive(UI_MODE_SELECTING_MIDI_CC)) {
			exitUIMode(UI_MODE_SELECTING_MIDI_CC);
			hid::display::OLED::removePopup();
		}
		return false;
	}
	const auto* midi = std::get_if<deluge::ui::scenes::bind::MidiFocus>(&focus.owner);
	if (currentSong == nullptr || midi == nullptr || !midi->clip || currentUIMode != UI_MODE_NONE
	    || getCurrentUI()->toClipMinder() == nullptr) {
		return false;
	}
	const project::DocOutput* output = currentSong->project.output_structure.find(midi->track.output_id);
	if (output == nullptr) {
		return false;
	}
	// A clip the song does not hold has no CC to choose: legacy's push needs the clip's stack.
	alignas(ModelStackWithAutoParam) char owner_memory[MODEL_STACK_MAX_SIZE];
	if (bridge::knobOwnerStackAt(*currentSong, focus, owner_memory) == nullptr) {
		return false;
	}
	const size_t at =
	    (static_cast<size_t>(deluge::ui::scenes::bind::knob_mode(currentSong->project, focus, kDelugeControls))
	     * kDelugeControls.slots)
	    + static_cast<size_t>(knob);
	const project::ModKnobCc cc = at < output->mod_knob_ccs.size() ? output->mod_knob_ccs[at] : project::ModKnobCc{};
	// Reads only: a lane that does not exist yet has no automation, which is what creating it would report too.
	bool automated = false;
	if (const auto lane = midi_knob_lane(*midi->clip, cc)) {
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		const ModelStackWithAutoParam* stack = bridge::paramStackFollowingTake(*currentSong, *lane, memory);
		automated = stack != nullptr && stack->autoParam != nullptr && stack->autoParam->isAutomated(*stack->doc());
	}
	currentUIMode = UI_MODE_SELECTING_MIDI_CC;
	InstrumentClipMinder::editingMIDICCForWhichModKnob = static_cast<uint8_t>(knob);
	InstrumentClipMinder::drawMIDIControlNumber(MIDIInstrument::engineCcOf(cc), automated);
	return true;
}

bool HostEditSink::delete_knob_automation(const project::ParamAddress& address) {
	edits::DeleteParamAutomation edit{address, ActionAddition::NOT_ALLOWED, edits::SetParamValue::Resolve::WRITE};
	return currentSong != nullptr && counted_apply(edit);
}

bool HostEditSink::instrument_edited(const deluge::ui::controls::KnobFocus& focus) {
	Instrument* instrument = currentSong != nullptr ? knob_instrument_of(*currentSong, focus) : nullptr;
	if (instrument == nullptr) {
		return false;
	}
	instrument->beenEdited();
	return true;
}

} // namespace deluge::gui::shell_host
