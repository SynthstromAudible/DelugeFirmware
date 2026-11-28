/// @file
/// @brief `services::KnobPresentation` -- what `ModKnobs`' gestures ask a presentation to show, and in what order.
///
/// A recording presentation stands in for both real ones (the menu shell's and the root views'): every call is one
/// tagged entry, and each example asserts the exact sequence a turn, a push or a mod button produces. The values the
/// gestures write are `mod_knobs_spec`'s subject, against legacy; this spec pins only the calls.

#include "base/notify/dirty_set.h"
#include "base/notify/notice_queue.h"
#include "board_controls.hpp"
#include "gui/ui/shell_host/host_ports.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/overlay/param_popup_scene.h"
#include "ui/scenes/registry.h"
#include "ui/scenes/services/knob_presentation.h"
#include "ui/scenes/services/mod_knobs.h"

#include "gui/views/knob_focus_from_stack.h"

#include "ui/shell/input_lowering.h"
#include "ui/shell/profiles/deluge.h"
#include "ui/shell/scene_stack.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "gui/ui/ui.h"
#include "gui/views/session_view.h"
#include "hid/display/display.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/set_mod_knob_mode.h"
#include "model/model_stack.h"
#include "model/project_bridge/param_ids.h"
#include "model/project_bridge/param_stack.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "processing/sound/sound_instrument.h"

#include "support/popup_capture.h"
#include "support/printers/param_address_printer.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "support/spec_main.h"

using namespace deluge::ui::scenes;
using deluge::modulation::params::Kind;
using deluge::ui::scenes::services::KnobPresentation;
using deluge::ui::scenes::services::ModKnobs;
using deluge::ui::scenes::services::ModTurn;
using deluge::ui::shell::deluge_profile;
using deluge::ui::shell::InputLowering;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::SceneRegistry;
using deluge::ui::shell::SceneStack;

namespace {

/// @brief A presentation that records each call as one tagged entry and draws nothing.
struct Recording final : KnobPresentation {
	std::vector<std::string> calls;
	/// What `suppress_value_popup` answers.
	bool suppress = false;

	bool suppress_value_popup(const ModTurn& /*turn*/) override {
		calls.emplace_back("suppress");
		return suppress;
	}
	void show_value_popup(const ModTurn& turn, PatchSource /*source1*/, PatchSource /*source2*/,
	                      const overlay::PopupContext& /*context*/, uint32_t /*now_ms*/) override {
		calls.push_back("show " + std::to_string(turn.new_knob_pos));
	}
	void value_written() override { calls.emplace_back("written"); }
	void blink_indicator(int32_t knob, bool bipolar) override {
		calls.push_back("blink " + std::to_string(knob) + (bipolar ? " bipolar" : ""));
	}
	void stop_blink_indicator(int32_t knob) override { calls.push_back("stop " + std::to_string(knob)); }
	void indicators_changed() override { calls.emplace_back("indicators"); }
	void mode_changed() override { calls.emplace_back("mode"); }
	void vu_sidebar_changed() override { calls.emplace_back("vu"); }
	void note_tails_changed() override { calls.emplace_back("tails"); }
	void popup_text(std::string_view /*text*/) override { calls.emplace_back("popup_text"); }
	void notice_text(std::string_view /*text*/) override { calls.emplace_back("notice_text"); }
	void popup(deluge::l10n::String string) override {
		calls.push_back(string == deluge::l10n::String::STRING_FOR_AUTOMATION_DELETED ? "popup automation_deleted"
		                                                                              : "popup other");
	}
	void notification(std::string_view title, std::string_view /*value*/) override {
		calls.push_back("notification " + std::string{title});
	}
	void setting_indicator(int32_t knob, int32_t knob_pos) override {
		calls.push_back("setting " + std::to_string(knob) + " " + std::to_string(knob_pos));
	}
	void cancel_popup() override { calls.emplace_back("cancel"); }

	/// @return The calls so far, and forget them.
	std::vector<std::string> take() { return std::exchange(calls, {}); }
};

/// @brief The knobs' focus on @p clip's sound, as legacy's view resolves it.
bind::KnobFocus focus_of(Song& song, InstrumentClip* clip) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* stack = setupModelStackWithSong(memory, &song)->addTimelineCounter(clip);
	clip->getActiveModControllable(stack);
	return deluge::gui::knobFocusFromStack(song, *reinterpret_cast<ModelStackWithThreeMainThings*>(memory));
}

/// @brief A synth song with the knobs on its clip, a recording presentation and a cleared notice queue.
struct Rig {
	spec::SongFixture f = spec::make_synth_song();
	SceneRegistry reg = scene_registry();
	SceneStack stack{reg};
	InputLowering lowering{deluge_profile(), {}};
	ModKnobs knobs{overlay::param_popup_scene(), stack, lowering, kDelugeControls};
	Recording out;

	Rig() {
		display = &deluge::ui::scenes::spec::quiet_display();
		currentSong = f.song;
		knobs.set_ports(&deluge::gui::shell_host::host_ports());
		knobs.set_model(&f.song->project);
		knobs.set_focus(focus_of(*f.song, f.clip), false);
		knobs.set_mod_region();
		deluge::base::notify::Notice discarded;
		while (deluge::base::notify::notices().pop(discarded)) {}
	}
	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;
	~Rig() {
		while (stack.depth() > 0) {
			stack.pop(SceneKind::Null);
		}
		menu_scene().reset_process_state();
		display = nullptr;
		currentSong = nullptr;
	}

	/// The knob owner's mode, set directly in its record.
	void set_mode(uint8_t button) {
		(void)deluge::edits::modKnobModeFor(deluge::project::TrackAddress{*f.instrument->id()}, button);
	}
	/// The address of the clip sound's patched param @p id.
	deluge::project::ParamAddress patched(int32_t id) const {
		return deluge::project::ParamAddress{.owner = deluge::project::OwnerId::instrument(*f.instrument->id()),
		                                     .space = deluge::project::ParamSpace::ENGINE,
		                                     .id = deluge::bridge::paramIdOf(Kind::PATCHED, id),
		                                     .clip_id = f.clip->id()};
	}
	/// Set the clip sound's patched param @p id to @p value, without recording.
	/// @return False when the sound has no such param.
	bool set_value(int32_t id, int32_t value) {
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithAutoParam* at = deluge::bridge::paramStackAt(*f.song, patched(id), memory);
		if (at == nullptr) {
			return false;
		}
		at->autoParam->setCurrentValueWithNoReversionOrRecording(at, value);
		return true;
	}
};

using Calls = std::vector<std::string>;

/// @brief @p calls as one comma-separated line, so a failure prints the whole sequence.
std::string joined(const Calls& calls) {
	std::string line;
	for (const std::string& call : calls) {
		line += line.empty() ? call : ", " + call;
	}
	return line;
}

} // namespace

// clang-format off
describe knob_presentation_spec_body("scenes::services::KnobPresentation", $ {

	before_all([] { deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english; });

	it("asks a synth turn's popup, shows it, reports the write and blinks only a centred knob", _ {
		Rig rig;
		rig.set_mode(0); // lower knob: pan, centred and with a middle detent
		expect(rig.set_value(deluge::modulation::params::LOCAL_PAN, 0)).to_be_true();

		rig.knobs.turn(0, 1, 1000, rig.out);
		expect(joined(rig.out.take())).to_equal(joined(Calls{"suppress", "show 1", "written", "stop 0"}));

		rig.knobs.turn(0, -1, 1010, rig.out);
		expect(joined(rig.out.take())).to_equal(joined(Calls{"suppress", "show 0", "written", "blink 0 bipolar"}));
	});

	it("shows no popup when the presentation suppresses it, and still writes", _ {
		Rig rig;
		rig.set_mode(0);
		expect(rig.set_value(deluge::modulation::params::LOCAL_PAN, 0)).to_be_true();
		rig.out.suppress = true;
		rig.knobs.turn(0, 1, 1000, rig.out);
		expect(joined(rig.out.take())).to_equal(joined(Calls{"suppress", "written", "stop 0"}));
	});

	it("asks only for the popup when a turn moves nothing", _ {
		Rig rig;
		rig.set_mode(0);
		expect(rig.set_value(deluge::modulation::params::LOCAL_PAN, std::numeric_limits<int32_t>::max())).to_be_true();
		rig.knobs.turn(0, 1, 1000, rig.out);
		expect(joined(rig.out.take())).to_equal(joined(Calls{"suppress", "show 64"}));
	});

	it("reports changed note tails between the write and the blink", _ {
		Rig rig;
		rig.set_mode(2); // lower knob: envelope 1's release
		expect(rig.set_value(deluge::modulation::params::LOCAL_ENV_0_SUSTAIN, std::numeric_limits<int32_t>::min())).to_be_true();
		expect(rig.set_value(deluge::modulation::params::LOCAL_ENV_0_DECAY, -(1 << 29))).to_be_true();
		expect(rig.set_value(deluge::modulation::params::LOCAL_ENV_0_RELEASE, std::numeric_limits<int32_t>::min())).to_be_true();
		Calls flip;
		int32_t flips = 0;
		for (int32_t i = 0; i < 60; i++) {
			rig.knobs.turn(0, 1, 1000, rig.out);
			Calls calls = rig.out.take();
			if (std::ranges::find(calls, std::string{"tails"}) != calls.end()) {
				flip = calls;
				flips++;
			}
		}
		expect(flips).to_equal(1);
		// The popup's position is the turn's; any shown position will do, but it must be a show.
		const std::string shown = flip.size() > 1 ? flip[1] : std::string{};
		expect(shown.starts_with("show ")).to_be_true();
		expect(joined(flip)).to_equal(joined(Calls{"suppress", shown, "written", "tails", "stop 0"}));
	});

	it("shows a filter-knob push's setting as a notice, then re-levels the indicators", _ {
		Rig rig;
		rig.set_mode(1); // the low-pass pair; the upper knob's push cycles it to high-pass
		rig.knobs.push(1, true, {}, rig.out);
		expect(joined(rig.out.take())).to_equal(joined(Calls{"notice_text", "indicators"})); // the filter type, as a notice
	});

	it("re-levels nothing on a learn push, which is a copy or paste", _ {
		Rig rig;
		rig.set_mode(1);
		rig.knobs.push(1, true, {.learn = true}, rig.out);
		expect(joined(rig.out.take())).to_equal(std::string{});
	});

	it("shows a mod button's popup, then its mode change; outside a clip minder the VU sidebar first", _ {
		Rig rig;
		rig.knobs.set_view_state({.root_is_clip_minder = true});
		rig.knobs.section_button(1, true, rig.out);
		expect(joined(rig.out.take())).to_equal(joined(Calls{"popup_text", "mode"}));
		rig.knobs.section_button(1, false, rig.out);
		expect(joined(rig.out.take())).to_equal(joined(Calls{"cancel"}));

		rig.knobs.set_view_state({.root_is_clip_minder = false});
		rig.knobs.section_button(1, true, rig.out);
		expect(joined(rig.out.take())).to_equal(joined(Calls{"vu", "popup_text", "mode"}));
	});

	it("ignores a mod button in the automation editor", _ {
		Rig rig;
		rig.knobs.set_view_state({.in_automation_editor = true});
		rig.knobs.section_button(1, true, rig.out);
		expect(joined(rig.out.take())).to_equal(std::string{});
	});

	it("shows the automation-deleted popup on a shift+push of an automated param, and re-levels nothing", _ {
		Rig rig;
		rig.set_mode(0);
		rig.knobs.set_mod_region(0, 24);
		rig.knobs.turn(0, 5, 1000, rig.out); // a region turn automates pan
		expect(rig.f.song->project.resolveParam(rig.patched(deluge::modulation::params::LOCAL_PAN))->nodes.empty())
		    .to_be_false();
		(void)rig.out.take();

		changeRootUI(&sessionView); // an undo step opens only with a view as the current UI
		actionLogger.deleteAllLogs();
		rig.knobs.push(0, true, {.shift = true}, rig.out);
		expect(joined(rig.out.take())).to_equal(joined(Calls{"popup automation_deleted"}));
		expect(rig.f.song->project.resolveParam(rig.patched(deluge::modulation::params::LOCAL_PAN))->nodes.empty())
		    .to_be_true();
		actionLogger.deleteAllLogs();
		nullifyUIs();
	});

	it("shows a song compressor turn as its knob's new position, then its setting's notification", _ {
		Rig rig;
		(void)deluge::edits::modKnobModeFor(deluge::project::SongAddress{}, 4);
		rig.knobs.set_focus(bind::KnobFocus{.owner = bind::SongFocus{}}, false);
		rig.knobs.push(1, true, {}, rig.out); // the upper knob's push edits the compressor: the knobs leave their params
		rig.knobs.push(1, false, {}, rig.out);
		(void)rig.out.take();
		const deluge::ui::scenes::services::ModIndicatorInfo before = rig.knobs.indicator_info(1);
		expect(before.non_existent_param).to_be_true();

		rig.knobs.turn(1, 3, 1000, rig.out); // the threshold
		const std::string title{deluge::l10n::get(deluge::l10n::String::STRING_FOR_THRESHOLD)};
		expect(joined(rig.out.take()))
		    .to_equal(joined(Calls{"setting 1 " + std::to_string(before.non_existent_knob_pos + 3), "notification " + title}));
	});

	it("reads no indicator with no song", _ {
		Rig rig;
		rig.set_mode(0);
		rig.knobs.set_model(nullptr);
		const deluge::ui::scenes::services::ModIndicatorInfo info = rig.knobs.indicator_info(0);
		expect(info.has_auto_param).to_be_false("the indicator's auto param");
		expect(info.non_existent_param).to_be_false("the indicator's non-existent param");
		expect(info.patch_cable_default).to_be_false("the indicator's patch cable default");
		expect(ModKnobs::indicator_level(info, false).declared).to_be_false();
	});

	it("reads the address a knob turns, and none for a knob on nothing", _ {
		Rig rig;
		rig.set_mode(0);
		const std::optional<deluge::project::ParamAddress> lower = rig.knobs.knob_address(0);
		expect(lower.has_value()).to_be_true("the lower knob's address");
		if (lower.has_value()) {
			expect(*lower).to_equal(rig.patched(deluge::modulation::params::LOCAL_PAN), "the lower knob's address");
		}
		rig.knobs.set_focus({}, false);
		expect(rig.knobs.knob_address(0).has_value()).to_be_false();
	});
});
// clang-format on

DELUGE_SPEC(knob_presentation_spec, knob_presentation_spec_body)
