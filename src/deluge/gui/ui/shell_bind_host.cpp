#include "gui/ui/shell_bind_host.h"

#include "engine-api/types/setting.h"
#include "engines/deluge/settings.h"
#include "io/debug/log.h"
#include "model/drum/drum.h"
#include "model/edits/edit.h"
#include "model/edits/set_engine_setting.h"
#include "model/instrument/kit.h"
#include "model/output.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "scenes/bind/dispatch.h"
#include "scenes/menu/bind_host.h"

#include <array>
#include <string>

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

/// Applies a menu's write requests as their targets' Edits on the current song. A sound target's request becomes
/// SetEngineSetting on the sound's slot, and its partner row's value a second one; with affect-entire held on a kit,
/// one per sound drum of the kit whose row applies in that drum's own context. A request whose slot does not resolve,
/// or a sound with no slot, is refused: logged and counted, never applied through the runtime's setter alone.
class ShellEditSink final : public deluge::scenes::bind::WriteSink {
public:
	void write(const WriteRequest& request) override;
	[[nodiscard]] uint32_t refusals() const { return refusals_; }

private:
	uint32_t refusals_{0};
};

/// The runtime facts, answered from the running app: the audio engine's inputs, and each sound's sources and typed
/// state, found by its slot in the current song.
class ShellRuntimeFacts final : public deluge::scenes::bind::RuntimeFacts {
public:
	[[nodiscard]] bool audio_inputs_present() const override;
	[[nodiscard]] uint8_t loaded_sources(deluge::project::SlotAddress slot) const override;
	[[nodiscard]] const deluge::instrument::SoundState* sound_state(deluge::project::SlotAddress slot) const override;
};

ShellEditSink& shell_edit_sink() {
	static ShellEditSink sink;
	return sink;
}

} // namespace

void ShellEditSink::write(const WriteRequest& request) {
	if (currentSong == nullptr || request.target.kind != deluge::scenes::bind::Target::Kind::SOUND) {
		return;
	}
	const deluge::engine::SettingInfo* row =
	    deluge::engine::find_setting(deluge::instrument::kSettings, request.target.path);
	const auto stored = row != nullptr ? deluge::engine::from_menu(*row, request.menu_value) : std::nullopt;
	if (!stored) {
		refusals_++;
		D_PRINTLN("menu write refused: %.*s menu %d", static_cast<int>(request.target.path.size()),
		          request.target.path.data(), static_cast<int>(request.menu_value));
		return;
	}
	const std::string_view partner = deluge::engine::mapping_of(*row).partner_path();
	const deluge::engine::SettingInfo* partner_row =
	    partner.empty() ? nullptr : deluge::engine::find_setting(deluge::instrument::kSettings, partner);

	const auto apply_to = [&](deluge::project::SlotAddress slot) {
		if (deluge::bridge::soundAt(*currentSong, slot) == nullptr) {
			refusals_++;
			D_PRINTLN("menu write refused: no sound at the slot for %.*s", static_cast<int>(request.target.path.size()),
			          request.target.path.data());
			return;
		}
		deluge::edits::Context ctx{*currentSong, currentSong->project};
		const auto commit = [&](const deluge::engine::SettingInfo& on, int32_t value) {
			const std::optional<deluge::serialization::Value> file = value_of(on, value);
			if (!file) {
				refusals_++;
				D_PRINTLN("menu write refused: %.*s has no word for %d", static_cast<int>(on.path.size()),
				          on.path.data(), static_cast<int>(value));
				return;
			}
			deluge::edits::SetEngineSetting edit{slot, std::string(on.path), *file};
			if (deluge::edits::apply(ctx, edit) != Error::NONE) {
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

	if (request.where.affect_entire && request.where.kit_output) {
		Kit* kit = kit_with_id(*request.where.kit_output);
		if (kit == nullptr) {
			refusals_++;
			return;
		}
		for (Drum* drum = kit->firstDrum; drum != nullptr; drum = drum->next) {
			if (drum->type != DrumType::SOUND) {
				continue;
			}
			const std::optional<deluge::project::SlotAddress> slot =
			    deluge::bridge::slotAddressOf(*static_cast<SoundDrum*>(drum));
			if (!slot) {
				refusals_++;
				continue;
			}
			const auto shown = deluge::scenes::bind::view(request.target, currentSong->project, Addresses{.slot = slot},
			                                              deluge::scenes::menu::host_runtime_facts());
			if (shown && shown->applies) {
				apply_to(*slot);
			}
		}
		return;
	}
	if (!request.where.slot) {
		refusals_++;
		D_PRINTLN("menu write refused: %.*s has no slot", static_cast<int>(request.target.path.size()),
		          request.target.path.data());
		return;
	}
	apply_to(*request.where.slot);
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

uint32_t shellEditRefusals() {
	return shell_edit_sink().refusals();
}

} // namespace deluge::gui

namespace deluge::scenes::menu {

deluge::scenes::bind::WriteSink& host_write_sink() {
	return gui::shell_edit_sink();
}

const deluge::scenes::bind::RuntimeFacts& host_runtime_facts() {
	static const gui::ShellRuntimeFacts facts;
	return facts;
}

} // namespace deluge::scenes::menu
