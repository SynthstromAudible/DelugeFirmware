#include "gui/ui/shell_bind_host.h"

#include "engine-api/types/setting.h"
#include "engines/deluge/settings.h"
#include "engines/deluge/words.h"
#include "engines/effects/settings.h"
#include "io/debug/log.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/edits/edit.h"
#include "model/edits/set_effect_setting.h"
#include "model/edits/set_engine_setting.h"
#include "model/global_effectable/global_effectable.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/project_bridge/chain_bridge.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "scenes/bind/dispatch.h"
#include "scenes/bind/setting_context.h"
#include "scenes/bind/slot_reader.h"
#include "scenes/menu/bind_host.h"

#include <array>
#include <string>
#include <utility>

namespace deluge::gui {

namespace {

using deluge::scenes::bind::Addresses;
using deluge::scenes::bind::WriteRequest;

/// The file form of stored value @p stored of @p row: a CHOICE row's word, any other row's number.
std::optional<deluge::serialization::Value> value_of(const deluge::engine::SettingInfo& row, int32_t stored) {
	if (row.kind == deluge::engine::SettingKind::CHOICE) {
		std::array<char, 1> unused{};
		const std::optional<std::string_view> word = deluge::engine::stored_text(row, stored, unused);
		return word ? std::optional{deluge::serialization::Value::makeScalar(std::string(*word))} : std::nullopt;
	}
	return deluge::serialization::Value::makeNumber(stored);
}

/// The kit whose output has durable id @p id in the current song, or nullptr.
Kit* kit_with_id(uint32_t id) {
	for (Output* output = currentSong->live.media.first_output; output != nullptr; output = output->next) {
		if (output->id() == id && output->type == OutputType::KIT) {
			return static_cast<Kit*>(output);
		}
	}
	return nullptr;
}

/// Whether an affect-entire write of @p target reaches the sound drum at @p slot: its fan-out rule holds, read in the
/// drum's own context.
bool reaches(const deluge::scenes::bind::Target& target, deluge::project::SlotAddress slot) {
	const deluge::scenes::bind::RuntimeFacts& facts = deluge::scenes::menu::host_runtime_facts();
	const deluge::scenes::bind::SlotReader reader{currentSong->project, slot, facts};
	return reader.resolved()
	       && deluge::engine::conditions_hold(target.fan_out_when,
	                                          deluge::scenes::bind::setting_context(currentSong->project, slot, facts),
	                                          reader.reader());
}

/// Whether writing stored value @p stored of @p row through @p target ends a kit row's notes: a sample played once
/// (`sample/repeat.h:61-64`, `:73-75`), whose notes the kit's arpeggiator would otherwise hold.
bool silences_kit_arp(const deluge::scenes::bind::Target& target, const deluge::engine::SettingInfo& row,
                      int32_t stored) {
	if (target.kind != deluge::scenes::bind::Target::Kind::SOUND || !row.path.ends_with("/loopMode")) {
		return false;
	}
	return stored
	       == std::to_underlying(deluge::instrument::SampleRepeatMode::ONCE); // a loop mode is saved as its index
}

/// Sends @p kit's selected drum's note-off through the kit's arpeggiator, as `sample/repeat.h`'s
/// sendNoteOffForKitArpeggiator does.
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
	char memory[MODEL_STACK_MAX_SIZE];
	auto* stack = reinterpret_cast<ModelStack*>(memory);
	ModControllable* mod = kit->selectedDrum->toModControllable();
	ParamManager* params = note_row != nullptr ? &note_row->paramManager : nullptr;
	kit->noteOffPreKitArp(
	    stack->addTimelineCounter(clip)->addNoteRow(note_row_index, note_row)->addOtherTwoThings(mod, params),
	    kit->selectedDrum);
}

/// Applies a menu's write requests as their targets' Edits on the current song. A sound target's request becomes
/// SetEngineSetting on the sound's slot, and its partner row's value a second one; with affect-entire held on a kit,
/// one per sound drum of the kit the target's fan-out rule picks, read in that drum's own context, as legacy's loop
/// picks them. A drum whose Edit then refuses the value is left as it was, as legacy's loop writes it harmlessly; that
/// refusal is logged, not counted. A sample's loop mode set to play once ends the kit arpeggiator's note first. An
/// effect target's request is the same on a sound's slot, and on a host chain (the whole kit's, an audio track's, the
/// master's) SetEffectSetting on the chain, its partner second. A request whose slot or chain does not resolve, or a
/// sound with no slot, is refused: logged and counted, never applied through the runtime's setter alone.
class ShellEditSink final : public deluge::scenes::bind::WriteSink {
public:
	void write(const WriteRequest& request) override;
	[[nodiscard]] uint32_t refusals() const { return refusals_; }

private:
	uint32_t refusals_{0};
};

/// The runtime facts, answered from the running app: the audio engine's inputs, each sound's sources and typed
/// state, found by its slot in the current song, and each host chain's effects, found by its address.
class ShellRuntimeFacts final : public deluge::scenes::bind::RuntimeFacts {
public:
	[[nodiscard]] bool audio_inputs_present() const override;
	[[nodiscard]] uint8_t loaded_sources(deluge::project::SlotAddress slot) const override;
	[[nodiscard]] const deluge::instrument::SoundState* sound_state(deluge::project::SlotAddress slot) const override;
	[[nodiscard]] const deluge::effects::EffectStates*
	chain_effects(deluge::project::ChainAddress chain) const override;
};

ShellEditSink& shell_edit_sink() {
	static ShellEditSink sink;
	return sink;
}

} // namespace

void ShellEditSink::write(const WriteRequest& request) {
	using Kind = deluge::scenes::bind::Target::Kind;
	const Kind kind = request.target.kind;
	if (currentSong == nullptr || (kind != Kind::SOUND && kind != Kind::EFFECT)) {
		return;
	}
	const std::span<const deluge::engine::SettingInfo> table =
	    kind == Kind::EFFECT ? deluge::effects::kEffectSettings : deluge::instrument::kSettings;
	const deluge::engine::SettingInfo* row = deluge::engine::find_setting(table, request.target.path);
	const auto stored = row != nullptr ? deluge::engine::from_menu(*row, request.menu_value) : std::nullopt;
	if (!stored) {
		refusals_++;
		D_PRINTLN("menu write refused: %.*s menu %d", static_cast<int>(request.target.path.size()),
		          request.target.path.data(), static_cast<int>(request.menu_value));
		return;
	}
	const std::string_view partner = deluge::engine::mapping_of(*row).partner_path();
	const deluge::engine::SettingInfo* partner_row =
	    partner.empty() ? nullptr : deluge::engine::find_setting(table, partner);

	// Commits @p row's value, then its partner's, each as the Edit @p edit_for makes for a path and a file value. A
	// @p quiet refusal is a drum of a fan-out whose Edit does not take the value, which legacy's loop writes
	// harmlessly: logged, not counted.
	const auto commit_both = [&](deluge::edits::Context& ctx, bool quiet, const auto& edit_for) {
		const auto commit = [&](const deluge::engine::SettingInfo& on, int32_t value) {
			const std::optional<deluge::serialization::Value> file = value_of(on, value);
			if (!file) {
				refusals_++;
				D_PRINTLN("menu write refused: %.*s has no word for %d", static_cast<int>(on.path.size()),
				          on.path.data(), static_cast<int>(value));
				return;
			}
			auto edit = edit_for(std::string(on.path), *file);
			if (deluge::edits::apply(ctx, edit) != Error::NONE) {
				refusals_ += quiet ? 0 : 1;
				D_PRINTLN("menu write refused: %.*s = %d", static_cast<int>(on.path.size()), on.path.data(),
				          static_cast<int>(value));
			}
		};
		commit(*row, stored->first);
		if (partner_row != nullptr) {
			commit(*partner_row, stored->second);
		}
	};

	const auto apply_to = [&](deluge::project::SlotAddress slot, bool quiet) {
		if (deluge::bridge::soundAt(*currentSong, slot) == nullptr) {
			refusals_++;
			D_PRINTLN("menu write refused: no sound at the slot for %.*s", static_cast<int>(request.target.path.size()),
			          request.target.path.data());
			return;
		}
		deluge::edits::Context ctx{*currentSong, currentSong->project};
		commit_both(ctx, quiet, [&](std::string path, const deluge::serialization::Value& value) {
			return deluge::edits::SetEngineSetting{slot, std::move(path), value};
		});
	};

	if (request.where.affect_entire && request.target.whole_kit && request.where.kit_output) {
		Kit* kit = kit_with_id(*request.where.kit_output);
		if (kit == nullptr) {
			refusals_++;
			D_PRINTLN("menu write refused: no kit with id %u for %.*s",
			          static_cast<unsigned>(*request.where.kit_output), static_cast<int>(request.target.path.size()),
			          request.target.path.data());
			return;
		}
		if (silences_kit_arp(request.target, *row, stored->first)) {
			kit_arp_note_off(kit);
		}
		for (Drum* drum = kit->firstDrum; drum != nullptr; drum = drum->next) {
			if (drum->type != DrumType::SOUND) {
				continue;
			}
			const std::optional<deluge::project::SlotAddress> slot =
			    deluge::bridge::slotAddressOf(*static_cast<SoundDrum*>(drum));
			if (!slot) {
				refusals_++;
				D_PRINTLN("menu write refused: a sound drum has no slot for %.*s",
				          static_cast<int>(request.target.path.size()), request.target.path.data());
				continue;
			}
			if (reaches(request.target, *slot)) {
				apply_to(*slot, /*quiet=*/true);
			}
		}
		return;
	}
	if (request.where.slot) {
		if (silences_kit_arp(request.target, *row, stored->first) && request.where.kit_output) {
			kit_arp_note_off(kit_with_id(*request.where.kit_output));
		}
		apply_to(*request.where.slot, /*quiet=*/false);
		return;
	}
	if (kind == Kind::EFFECT && request.where.chain) {
		deluge::edits::Context ctx{*currentSong, currentSong->project};
		commit_both(ctx, false, [&](std::string path, const deluge::serialization::Value& value) {
			return deluge::edits::SetEffectSetting{*request.where.chain, std::move(path), value};
		});
		return;
	}
	refusals_++;
	D_PRINTLN("menu write refused: %.*s has no slot or chain", static_cast<int>(request.target.path.size()),
	          request.target.path.data());
}

bool ShellRuntimeFacts::audio_inputs_present() const {
	return AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn;
}

uint8_t ShellRuntimeFacts::loaded_sources(deluge::project::SlotAddress slot) const {
	::Sound* sound = currentSong != nullptr ? deluge::bridge::soundAt(*currentSong, slot) : nullptr;
	uint8_t loaded = 0;
	if (sound != nullptr) {
		for (uint8_t s = 0; s < kNumSources; ++s) {
			if (sound->sources[s].hasAtLeastOneAudioFileLoaded()) {
				loaded |= static_cast<uint8_t>(1u << s);
			}
		}
	}
	return loaded;
}

const deluge::instrument::SoundState* ShellRuntimeFacts::sound_state(deluge::project::SlotAddress slot) const {
	::Sound* sound = currentSong != nullptr ? deluge::bridge::soundAt(*currentSong, slot) : nullptr;
	return sound != nullptr ? sound->boundState() : nullptr;
}

const deluge::effects::EffectStates* ShellRuntimeFacts::chain_effects(deluge::project::ChainAddress chain) const {
	::GlobalEffectable* owner = currentSong != nullptr ? deluge::bridge::chainOwnerAt(chain, *currentSong) : nullptr;
	return owner != nullptr ? &owner->effects() : nullptr;
}

uint32_t shellEditRefusals() {
	return shell_edit_sink().refusals();
}

} // namespace deluge::gui

namespace deluge::scenes::menu {

bool host_binds(const ::Song& song) {
	return &song == currentSong;
}

deluge::scenes::bind::WriteSink& host_write_sink() {
	return gui::shell_edit_sink();
}

const deluge::scenes::bind::RuntimeFacts& host_runtime_facts() {
	static const gui::ShellRuntimeFacts facts;
	return facts;
}

} // namespace deluge::scenes::menu
