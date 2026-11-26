// What a gold knob's push and a mod button's press do, from the song's records: the port's push, applied through the
// app's sink, for every owner kind, mode and knob, pushed three times round each cycle, against the outcome the
// engine's own modEncoderButtonAction was recorded leaving (support/recorded_knob_pushes.h) -- the same effect
// settings, knob mappings, chain views, reverb, stutter and text after every push. And the mod button's popup, as the
// engine showed it.

#include "board_controls.hpp"
#include "ui/scenes/services/knob_push.h"

#include "gui/ui/shell_host/host_ports.h"
#include "ui/scenes/bind/knob_assignment.h"

#include "base/notify/notice_queue.h"
#include "effects/chain.h"
#include "effects/words.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/set_mod_knob_mode.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "processing/audio_output.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "util/functions.h"

#include "support/recorded_knob_pushes.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "instruments/classic/state_access.h"
#include "support/spec_main.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Buttons {
extern bool buttonStates[NUM_BUTTON_COLS + 1][NUM_BUTTON_ROWS];
}

namespace {

namespace bind = deluge::ui::scenes::bind;
namespace services = deluge::ui::scenes::services;
using deluge::edits::KnobModeOwner;

/// @brief The display legacy's push and mod button draw on: what it last popped up, kept.
struct StubDisplay final : deluge::hid::Display {
	std::string popup;
	constexpr size_t getNumBrowserAndMenuLines() override { return 3; }
	void displayPopup(char const*, int8_t, bool, uint8_t, int32_t, PopupType) override {}
	void popupText(char const* text, PopupType) override { popup = text; }
	void popupTextTemporary(char const*, PopupType) override {}
	void cancelPopup() override {}
	void freezeWithError(char const*) override {}
	void displayError(Error) override {}
	void removeWorkingAnimation() override {}
	void displayLoadingAnimationText(char const*, bool, bool) override {}
	void removeLoadingAnimation() override {}
	bool hasPopup() override { return false; }
	bool hasPopupOfType(PopupType) override { return false; }
	void consoleText(char const*) override {}
	void timerRoutine() override {}
	void displayNotification(std::string_view, std::optional<std::string_view>) override {}
};

/// @brief One owner under test: a song, the owner its knobs point at as a focus, and whose mod-knob mode it holds.
struct Side {
	Song* song = nullptr;
	bind::KnobFocus focus;
	KnobModeOwner mode_owner;
};

/// @brief What a push leaves that the two sides are compared on.
struct Outcome {
	deluge::effects::EffectStates effects;
	std::vector<deluge::project::ControllerMapping> controls;
	uint8_t filter_type = 0;
	uint8_t mod_fx_param = 0;
	float room = 0;
	float damping = 0;
	bool stuttering = false;
	std::string text;
	bool operator==(const Outcome&) const = default;
};

/// @brief @p side's records and facts now; @p text is what the push showed.
Outcome outcome_of(const Side& side, std::string text) {
	Outcome out;
	currentSong = side.song;
	const deluge::project::Song& model = side.song->project;
	if (const std::optional<services::EffectOwner> owner = services::effect_owner(side.focus)) {
		if (const auto* slot = std::get_if<deluge::project::SlotAddress>(&*owner)) {
			if (const deluge::instruments::classic::SoundState* state =
			        deluge::gui::shell_host::host_ports().sound->sound_state(*slot)) {
				out.effects = deluge::instruments::classic::sound_effects(*state);
			}
			if (slot->kind == deluge::project::SlotAddress::Kind::DRUM) {
				if (const deluge::project::DocDrum* drum = model.drum_structure.find(slot->id)) {
					out.controls = drum->controls;
				}
			}
			else if (const deluge::project::DocOutput* output = model.output_structure.find(slot->id)) {
				out.controls = output->controls;
			}
			else {
				std::fputs(("KNOB-PUSH no record for output " + std::to_string(slot->id) + "\n").c_str(), stdout);
			}
		}
		else {
			const auto chain = std::get<deluge::project::ChainAddress>(*owner);
			if (const deluge::effects::EffectStates* states =
			        deluge::gui::shell_host::host_ports().tracks->chain_effects(chain)) {
				out.effects = *states;
			}
			const deluge::project::FxChain* record = const_cast<deluge::project::Song&>(model).resolveChain(chain);
			if (record != nullptr) {
				out.filter_type = record->filter_type;
				out.mod_fx_param = record->mod_fx_param;
			}
		}
	}
	out.room = model.reverb.room_size;
	out.damping = model.reverb.damping;
	out.stuttering = side.song->live.stutter.active();
	out.text = std::move(text);
	return out;
}

/// @brief @p outcome as text: every field, floats by their bits.
std::string canonical(const Outcome& o) {
	const deluge::effects::EffectStates& e = o.effects;
	std::string text;
	const auto add = [&text](std::initializer_list<int64_t> values) {
		for (const int64_t v : values) {
			text += std::to_string(v) + " ";
		}
		text += "|";
	};
	add({int(e.filter.lpf_mode), int(e.filter.hpf_mode), int(e.filter.route), e.saturation.clipping_amount,
	     int(e.mod_fx.type)});
	add({e.delay.ping_pong, int(e.delay.type), int(e.delay.sync_type), int(e.delay.sync_level)});
	add({e.stutter.use_song_stutter, e.stutter.quantized, e.stutter.reversed, e.stutter.ping_pong});
	add({e.compressor.attack, e.compressor.release, e.compressor.threshold, e.compressor.ratio,
	     e.compressor.sidechain_hpf, e.compressor.blend});
	add({e.sidechain.attack, e.sidechain.release, int(e.sidechain.sync_type), int(e.sidechain.sync_level)});
	add({o.filter_type, o.mod_fx_param, std::bit_cast<uint32_t>(o.room), std::bit_cast<uint32_t>(o.damping),
	     o.stuttering});
	for (const deluge::project::ControllerMapping& m : o.controls) {
		text += std::to_string(m.control.button) + "." + std::to_string(m.control.knob) + ">"
		        + std::to_string(m.target.value) + ",";
	}
	return text + "|" + o.text;
}

/// @brief Where @p actual first differs from @p expected, section by section (the `|`-separated parts of a canonical
///        outcome), or empty when they are equal.
std::string first_difference(std::string_view expected, std::string_view actual) {
	size_t at = 0;
	for (size_t section = 0; at <= expected.size() || at <= actual.size(); section++) {
		const size_t e_end = std::min(expected.find('|', at), expected.size());
		const size_t a_end = std::min(actual.find('|', at), actual.size());
		const std::string_view e = at <= expected.size() ? expected.substr(at, e_end - at) : std::string_view{};
		const std::string_view a = at <= actual.size() ? actual.substr(at, a_end - at) : std::string_view{};
		if (e != a) {
			return "section " + std::to_string(section) + ": recorded '" + std::string{e} + "', pushed '"
			       + std::string{a} + "'";
		}
		if (e_end >= expected.size() && a_end >= actual.size()) {
			break;
		}
		at = e_end + 1;
	}
	return {};
}

void hold_mod_button(int32_t mode, bool held) {
	Buttons::buttonStates[modButtonX[mode]][modButtonY[mode]] = held;
}

/// @brief The last text notice raised, and the queue emptied.
std::string last_notice() {
	std::string text;
	deluge::base::notify::Notice n;
	while (deluge::base::notify::notices().pop(n)) {
		if (n.kind == deluge::base::notify::Notice::Kind::Text) {
			text = n.text;
		}
	}
	return text;
}

services::PushInputs inputs_of(const Side& side, bool held) {
	// The push reads the owner's effects and the device facts from the host, which resolves in the current song.
	currentSong = side.song;
	return {.button_held = held,
	        .clip_context = !std::holds_alternative<bind::SongFocus>(side.focus.owner),
	        .board = kDelugeControls,
	        .ports = &deluge::gui::shell_host::host_ports()};
}

/// @brief Push knob @p knob on @p side (press and release), and where its outcome first differs from the recorded
///        one, or empty.
std::string push_one(Side& side, uint8_t mode, int32_t knob, bool held, std::string_view recorded) {
	(void)last_notice();
	hold_mod_button(mode, held);

	// The push, applied through the app's sink.
	const services::KnobPush press =
	    services::knob_push(side.song->project, side.focus, knob, true, inputs_of(side, held));
	currentSong = side.song;
	(void)services::apply_push(*deluge::gui::shell_host::host_ports().writes,
	                           *deluge::gui::shell_host::host_ports().controls_sink, side.focus, press);
	if (press.comp) {
		side.focus.comp = *press.comp;
	}
	const Outcome pushed = outcome_of(side, held ? press.popup : press.notice);
	const services::KnobPush release =
	    services::knob_push(side.song->project, side.focus, knob, false, inputs_of(side, held));
	currentSong = side.song;
	(void)services::apply_push(*deluge::gui::shell_host::host_ports().writes,
	                           *deluge::gui::shell_host::host_ports().controls_sink, side.focus, release);
	hold_mod_button(mode, false);
	(void)last_notice();

	if (const std::string why = first_difference(recorded, canonical(pushed)); !why.empty()) {
		return "mode " + std::to_string(mode) + " knob " + std::to_string(knob) + (held ? " held" : "") + ": " + why;
	}
	return {};
}

/// @brief Every mode and knob, three pushes each, with and without the alt delay knobs and the button held.
/// @param side     The owner's side.
/// @param label    Names the owner in each disagreement.
/// @param recorded The `kPushTexts` index recorded for each push, in the order the loops visit them.
/// @param extra    Run once per alt setting after the pushes; returns where it disagrees, or empty.
std::vector<std::string> push_everything(Side& side, const std::string& label, std::span<const uint16_t> recorded,
                                         const std::function<std::string()>& extra = {}) {
	StubDisplay stub;
	display = &stub;
	size_t next = 0;
	std::vector<std::string> found;
	for (const bool alt : {false, true}) {
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::AltGoldenKnobDelayParams,
		                           alt ? RuntimeFeatureStateToggle::On : RuntimeFeatureStateToggle::Off);
		for (uint8_t mode = 0; mode < kNumModButtons; mode++) {
			currentSong = side.song;
			(void)deluge::edits::modKnobModeFor(side.mode_owner, mode);
			for (int32_t knob = 0; knob < 2; knob++) {
				// The delay's sync level names its 256th as none on the engine, which wrote it so: two pushes (one
				// plain, one held) stop short of it.
				const int32_t pushes = (alt && mode == 3 && knob == 0) ? 1 : 3;
				for (int32_t n = 0; n < pushes; n++) {
					for (const bool held : {false, true}) {
						if (std::string why =
						        push_one(side, mode, knob, held, knob_recorded::kPushTexts[recorded[next++]]);
						    !why.empty()) {
							found.push_back(label + (alt ? " alt " : " ") + why);
						}
					}
				}
			}
		}
		if (extra) {
			if (std::string why = extra(); !why.empty()) {
				found.push_back(label + " " + why);
			}
		}
	}
	runtimeFeatureSettings.set(RuntimeFeatureSettingType::AltGoldenKnobDelayParams, RuntimeFeatureStateToggle::Off);
	display = nullptr;
	return found;
}

/// @brief Each mod button pressed on @p side: where the popup the port shows first differs from the recorded one, or
///        empty.
std::vector<std::string> press_buttons(Side& side, const std::string& label,
                                       const std::array<std::string_view, 8>& recorded) {
	StubDisplay stub;
	display = &stub;
	std::vector<std::string> found;
	for (uint8_t button = 0; button < kNumModButtons; button++) {
		currentSong = side.song;
		(void)deluge::edits::modKnobModeFor(side.mode_owner, button);
		const std::string shown =
		    services::mod_button_popup(side.song->project, side.focus, button, inputs_of(side, true));
		if (shown != std::string{recorded[button]}) {
			found.push_back(label + " button " + std::to_string(button) + ": recorded '" + std::string{recorded[button]}
			                + "' vs '" + shown + "'");
		}
	}
	display = nullptr;
	return found;
}

/// @brief Set @p side's chain's mod FX type to @p type and its view to @p view, through the app's sink as a menu sets
///        them: the view moves to nothing the type has, which a type step would have fixed.
/// @return Where the state was not reached, or empty.
std::string set_mod_fx(Side& side, std::string_view type, std::string_view view) {
	currentSong = side.song;
	const std::optional<services::EffectOwner> owner = services::effect_owner(side.focus);
	const auto* chain = owner ? std::get_if<deluge::project::ChainAddress>(&*owner) : nullptr;
	if (chain == nullptr) {
		return "no chain";
	}
	deluge::ui::controls::Sink& sink = *deluge::gui::shell_host::host_ports().controls_sink;
	if (!sink.write_effect(*chain, "modFXType", deluge::serialization::Value::makeScalar(std::string{type}))
	    || !sink.chain_view(*chain, "modFXCurrentParam", view)) {
		return "sink refused " + std::string{type} + "/" + std::string{view};
	}
	const Outcome now = outcome_of(side, {});
	if (deluge::effects::to_string(now.effects.mod_fx.type) != type
	    || deluge::effects::kModFxParamWords[now.mod_fx_param] != view) {
		return "state not reached for " + std::string{type} + "/" + std::string{view};
	}
	return {};
}

/// @brief The mod FX button's lower knob pushed three times from a view its type has no param for (a flanger on
///        depth, a chorus on feedback), plain and held: the push moves on to the type's next view.
/// @param side     The chain owner's side.
/// @param label    Names the owner in each disagreement.
/// @param recorded The `kPushTexts` index recorded for each push.
std::vector<std::string> push_from_missing_view(Side& side, const std::string& label,
                                                std::span<const uint16_t> recorded) {
	constexpr uint8_t kModFxButton = 5;
	constexpr std::array<std::pair<std::string_view, std::string_view>, 2> kStates{
	    {{"flanger", "depth"}, {"chorus", "feedback"}}};
	StubDisplay stub;
	display = &stub;
	size_t next = 0;
	std::vector<std::string> found;
	for (const auto& [type, view] : kStates) {
		for (const bool held : {false, true}) {
			if (std::string why = set_mod_fx(side, type, view); !why.empty()) {
				found.push_back(label + " " + why);
			}
			currentSong = side.song;
			(void)deluge::edits::modKnobModeFor(side.mode_owner, kModFxButton);
			for (int32_t n = 0; n < 3; n++) {
				if (std::string why =
				        push_one(side, kModFxButton, 0, held, knob_recorded::kPushTexts[recorded[next++]]);
				    !why.empty()) {
					found.push_back(label + " " + std::string{type} + " on " + std::string{view} + " push "
					                + std::to_string(n) + " " + why);
				}
			}
		}
	}
	display = nullptr;
	return found;
}

void report(const std::vector<std::string>& found) {
	for (const std::string& why : found) {
		std::fputs(("KNOB-PUSH " + why + "\n").c_str(), stdout);
	}
}

/// @brief A synth's side.
Side synth_side(deluge::ui::scenes::spec::SongFixture& f) {
	auto* synth = static_cast<SoundInstrument*>(f.instrument);
	Side side;
	side.song = f.song;
	side.focus = {.owner = bind::SoundFocus{.slot = {deluge::project::SlotAddress::Kind::OUTPUT, *synth->id()},
	                                        .clip = deluge::project::ClipAddress{f.clip->id()}}};
	side.mode_owner = deluge::project::TrackAddress{*synth->id()};
	return side;
}

/// @brief The song's own chain's side.
Side song_side(deluge::ui::scenes::spec::SongFixture& f) {
	Side side;
	side.song = f.song;
	side.focus = {.owner = bind::SongFocus{}};
	side.mode_owner = deluge::project::SongAddress{};
	return side;
}

/// @brief A kit's chain's side.
Side kit_chain_side(deluge::ui::scenes::spec::SongFixture& f) {
	auto* kit = static_cast<Kit*>(f.instrument);
	f.clip->affectEntire = true; // the kit's own chain, not its selected row
	Side side;
	side.song = f.song;
	side.focus = {.owner = bind::ChainFocus{.chain = {deluge::project::ChainAddress::Kind::OUTPUT, *kit->id()},
	                                        .owner = bind::ChainOwner::KIT,
	                                        .clip = deluge::project::ClipAddress{f.clip->id()}}};
	side.mode_owner = deluge::project::TrackAddress{*kit->id()};
	return side;
}

/// @brief An audio track's chain's side.
Side audio_side(deluge::ui::scenes::spec::AudioSongFixture& f) {
	Side side;
	side.song = f.song;
	side.focus = {.owner = bind::ChainFocus{.chain = {deluge::project::ChainAddress::Kind::OUTPUT, *f.output->id()},
	                                        .owner = bind::ChainOwner::AUDIO_TRACK,
	                                        .clip = deluge::project::ClipAddress{f.clip->id()}}};
	side.mode_owner = deluge::project::TrackAddress{*f.output->id()};
	return side;
}

} // namespace

// clang-format off
describe knob_push("what a gold knob's push and a mod button do, from the records", ${
	it("pushes a synth's knobs as the recorded engine did, in every mode, round every cycle", _ {
		Song* const before = currentSong;
		deluge::ui::scenes::spec::SongFixture a = deluge::ui::scenes::spec::make_synth_song();
		Side side = synth_side(a);
		const auto found = push_everything(side, "synth", knob_recorded::kSynthPushes);
		report(found);
		expect(found.empty()).to_be_true();
		currentSong = before;
	});

	it("pushes a kit row's knobs as the recorded engine did, with the kit's mode", _ {
		Song* const before = currentSong;
		deluge::ui::scenes::spec::SongFixture a = deluge::ui::scenes::spec::make_kit_song();
		currentSong = a.song; // a drum takes its id from the current song
		SoundDrum* drum = deluge::ui::scenes::spec::add_drum<SoundDrum>(a);
		auto* kit = static_cast<Kit*>(a.instrument);
		Side side;
		side.song = a.song;
		side.focus = {.owner = bind::SoundFocus{
		    .slot = {deluge::project::SlotAddress::Kind::DRUM, *drum->id()},
		    .clip = deluge::project::ClipAddress{a.clip->id()},
		    .row = deluge::project::NoteRowAddress{a.clip->id(), true, static_cast<int32_t>(*drum->id())},
		    .kit = *kit->id()}};
		side.mode_owner = deluge::project::TrackAddress{*kit->id()};
		const auto found = push_everything(side, "kit row", knob_recorded::kKitRowPushes);
		report(found);
		expect(found.empty()).to_be_true();
		currentSong = before;
	});

	it("pushes the song's knobs as the recorded engine did, its compressor view included", _ {
		Song* const before = currentSong;
		deluge::ui::scenes::spec::SongFixture a = deluge::ui::scenes::spec::make_synth_song();
		Side side = song_side(a);
		const auto found = push_everything(side, "song", knob_recorded::kSongPushes, [&]() -> std::string {
			// Recorded from the engine's compressor view after the pushes: not editing, on its first param.
			if (side.focus.comp != deluge::ui::controls::CompView{.editing = false, .param = 0}) {
				return "compressor view";
			}
			return {};
		});
		report(found);
		expect(found.empty()).to_be_true();
		currentSong = before;
	});

	it("pushes a kit's chain as the recorded engine did", _ {
		Song* const before = currentSong;
		deluge::ui::scenes::spec::SongFixture a = deluge::ui::scenes::spec::make_kit_song();
		Side side = kit_chain_side(a);
		const auto found = push_everything(side, "kit chain", knob_recorded::kKitChainPushes);
		report(found);
		expect(found.empty()).to_be_true();
		currentSong = before;
	});

	it("pushes an audio track's chain as the recorded engine did", _ {
		Song* const before = currentSong;
		deluge::ui::scenes::spec::AudioSongFixture a = deluge::ui::scenes::spec::make_audio_clip_song();
		Side side = audio_side(a);
		const auto found = push_everything(side, "audio", knob_recorded::kAudioPushes);
		report(found);
		expect(found.empty()).to_be_true();
		currentSong = before;
	});

	it("pushes a chain's mod FX knob from a view its type lacks as the recorded engine did: a kit's, the song's, an audio track's", _ {
		Song* const before = currentSong;
		std::vector<std::string> found;
		{
			deluge::ui::scenes::spec::SongFixture a = deluge::ui::scenes::spec::make_kit_song();
			Side side = kit_chain_side(a);
			auto more = push_from_missing_view(side, "kit chain", knob_recorded::kMissingKitChainPushes);
			found.insert(found.end(), more.begin(), more.end());
		}
		{
			deluge::ui::scenes::spec::SongFixture a = deluge::ui::scenes::spec::make_synth_song();
			Side side = song_side(a);
			auto more = push_from_missing_view(side, "song", knob_recorded::kMissingSongPushes);
			found.insert(found.end(), more.begin(), more.end());
		}
		{
			deluge::ui::scenes::spec::AudioSongFixture a = deluge::ui::scenes::spec::make_audio_clip_song();
			Side side = audio_side(a);
			auto more = push_from_missing_view(side, "audio", knob_recorded::kMissingAudioPushes);
			found.insert(found.end(), more.begin(), more.end());
		}
		report(found);
		expect(found.empty()).to_be_true();
		currentSong = before;
	});

	it("shows the popup the recorded engine showed for every mod button, on a synth, the song and a kit's chain", _ {
		Song* const before = currentSong;
		std::vector<std::string> found;
		{
			deluge::ui::scenes::spec::SongFixture a = deluge::ui::scenes::spec::make_synth_song();
			Side side = synth_side(a);
			auto more = press_buttons(side, "synth", knob_recorded::kSynthPopups);
			found.insert(found.end(), more.begin(), more.end());
		}
		{
			deluge::ui::scenes::spec::SongFixture a = deluge::ui::scenes::spec::make_synth_song();
			Side side = song_side(a);
			auto more = press_buttons(side, "song", knob_recorded::kSongPopups);
			found.insert(found.end(), more.begin(), more.end());
		}
		{
			deluge::ui::scenes::spec::SongFixture a = deluge::ui::scenes::spec::make_kit_song();
			Side side = kit_chain_side(a);
			auto more = press_buttons(side, "kit chain", knob_recorded::kKitChainPopups);
			found.insert(found.end(), more.begin(), more.end());
		}
		report(found);
		expect(found.empty()).to_be_true();
		currentSong = before;
	});

	it("names a note length as the song does", _ {
		expect(services::note_length_name(3, 0)).to_equal(std::string{"32nd-notes"});
		expect(services::note_length_name(48, 0)).to_equal(std::string{"2nd-notes"});
		expect(services::note_length_name(96, 0)).to_equal(std::string{"1-bar"});
		expect(services::note_length_name(384, 0)).to_equal(std::string{"4-bar"});
	});
});
// clang-format on

CPPSPEC_SPEC(knob_push)
