/// @file
/// @brief `services::ModKnobs` -- driven against the real legacy it ports, `View::modEncoderAction`
///        and friends (`gui/views/view.cpp`), on a real `Song` built by `support/song_fixture.h`.
///
/// @par The oracle, and why it is the model and not the port's report of it
///
/// The central example here (`writes the model exactly as legacy does`) does **not** ask
/// `ModKnobs` what it did. It reads the resulting `ParamManager` -- every value in both param sets,
/// by identity -- and compares it against the `ParamManager` legacy left behind after the same
/// turn. The two sides run on **two different clips**, each with its own `SoundInstrument` and its
/// own param manager, seeded by its own explicit write; there is no shared state for a
/// "port stored nothing" bug to hide in, and the example additionally asserts that the model
/// *moved* on both sides, so a port that wrote nothing fails on the equality and on the movement.
/// That is E2's blindness lesson applied: deleting `menu::mod_encoder_turn`'s
/// `setValuePossiblyForRegion` call turns this example red (see the task-2 report's mutation
/// table).
///
/// @par What this spec counts as uncovered (oracle discipline)
///
///   * **The popup's performance-view and song-browser suppression arms** (`view.cpp:869-874`,
///     `:885`). `PerformanceScene` does not exist and the song browser is not among the legacy UIs
///     `BrowserScene` carries, so there is no port-side state that could make either fire. See
///     `ModKnobs::popup_suppressed`.
///   * **`PopupContext::stuttering`**. Nothing in the port owns the stutter gesture yet, so no
///     caller can set it true and legacy's `isUIModeActive(UI_MODE_STUTTERING)` half of the
///     rate-bucket guard is never exercised from this side.
///   * **`PopupContext::midi_instrument`, the MIDI-clip CC/expression naming arm**
///     (`view.cpp:1122-1146`). Reachable now -- widening `show()` is what made it so -- but not
///     driven: a bare MIDI fixture's gold knobs resolve through
///     `MIDIInstrument::getParamFromModEncoder` (`model/instrument/midi_instrument.cpp:156-169`),
///     whose default CC assignment yields no `autoParam`, so `mod_encoder_turn` takes the
///     non-existent-param arm and no popup is shown at all. Covering it needs a fixture that
///     assigns a mod-knob CC, which is more MIDI-clip setup than this task owns. The
///     `ModControllableAudio*` half of the same widening IS driven, below.
///   * **`ModKnobs::notify_param_automation_occurred`'s `DISPLAY_AUTOMATION` consumer.** Legacy's
///     timer callback is `automationView.displayAutomation()` / `view.displayAutomation()`
///     (`gui/ui_timer_manager.cpp:157-171`), both view-side and not ported; only the *arming* is
///     compared here.

#include "scenes/menu/context.h"
#include "scenes/overlay/param_popup_scene.h"
#include "scenes/registry.h"
#include "scenes/services/mod_knobs.h"

#include "shell/input_lowering.h"
#include "shell/profiles/deluge.h"
#include "shell/router.h"
#include "shell/scene_stack.h"

#include "gui/l10n/l10n.h"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "model/clip/instrument_clip.h"
#include "model/mod_controllable/filters/filter_config.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_instrument.h"
#include "storage/storage_manager.h"

#include "spec/shell/support/fake_surfaces.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::modulation::params::Kind;
using deluge::scenes::services::ModKnobs;
using deluge::ui::EncoderRole;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::deluge_profile;
using deluge::ui::shell::InputLowering;
using deluge::ui::shell::Result;
using deluge::ui::shell::Router;
using deluge::ui::shell::Scene;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::SceneRegistry;
using deluge::ui::shell::SceneStack;
using deluge::ui::shell::Service;
using deluge::ui::shell::Services;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;

namespace {

/// `Action` unqualified is the model's own undo `Action`, reachable through `gui/views/view.h`'s
/// `model/model_stack.h`; the shell's is a different type with the same name (every other spec in
/// this directory makes the same alias for the same reason).
using ShellAction = deluge::ui::shell::Action;

/// @brief The minimum `Display` legacy's popup needs; `display` is null in a spec process
///        (`hid/display/display.cpp:4`) and `View::displayModEncoderValuePopup` dereferences it.
///
/// The popup's *pixels* are `param_popup_spec.cpp`'s subject, driven against the real
/// `OLED::displayNotification`; what this records is the one thing that spec cannot reach, the
/// parameter *name* legacy chose for a MIDI clip -- see the `PopupContext` example below.
struct StubDisplay final : deluge::hid::Display {
	int notifications = 0;
	std::string last_title;
	bool showing = false;

	constexpr size_t getNumBrowserAndMenuLines() override { return 3; }
	void displayPopup(char const*, int8_t, bool, uint8_t, int32_t, PopupType) override {}
	void popupText(char const*, PopupType) override {}
	void popupTextTemporary(char const*, PopupType) override {}
	void cancelPopup() override {}
	void freezeWithError(char const*) override {}
	void displayError(Error) override {}
	void removeWorkingAnimation() override {}
	void displayLoadingAnimationText(char const*, bool, bool) override {}
	void removeLoadingAnimation() override {}
	bool hasPopup() override { return showing; }
	bool hasPopupOfType(PopupType type) override { return showing && type == PopupType::NOTIFICATION; }
	void consoleText(char const*) override {}
	void timerRoutine() override {}
	void displayNotification(std::string_view title, std::optional<std::string_view>) override {
		last_title = title;
		showing = true;
		notifications++;
	}
};

/// @brief A second (or third) default synth clip in @p song -- `make_synth_song`'s own recipe
///        (`song_fixture.h`, `song.cpp:378-403`'s `couldntLoad` branch), applied again.
///
/// The two-clip shape is the point: legacy and the port each get a clip of their own, so neither
/// can read the other's model.
/// @param song The song to add to.
/// @param name The instrument's slot name; two instruments in one song must not share one.
/// @return The new clip.
InstrumentClip* add_synth_clip(Song* song, const char* name) {
	InstrumentClip* clip = spec::detail::make_session_clip(song);
	ParamManagerForTimeline params;
	auto* synth = static_cast<SoundInstrument*>(StorageManager::createNewInstrument(OutputType::SYNTH, &params));
	synth->dirPath = "SYNTHS";
	synth->name = name;
	synth->setupAsDefaultSynth(&params);
	spec::detail::attach(song, clip, synth, params);
	return clip;
}

/// @brief Every stored param value in @p pm, by identity: the patched set then the unpatched set.
///
/// The model-side readback the central example compares. `ParamSet::getValue`
/// (`modulation/params/param_set.h:45`) is `AutoParam::getCurrentValue()` -- what is actually in
/// the model, not what either side reported having put there.
/// @param pm The param manager to read.
/// @return One entry per param, patched set first.
std::vector<int32_t> param_snapshot(ParamManagerForTimeline& pm) {
	std::vector<int32_t> out;
	if (PatchedParamSet* patched = pm.getPatchedParamSet(); patched != nullptr) {
		for (int32_t i = 0; i < patched->getNumParams(); i++) {
			out.push_back(patched->getValue(i));
		}
	}
	if (UnpatchedParamSet* unpatched = pm.getUnpatchedParamSet(); unpatched != nullptr) {
		for (int32_t i = 0; i < unpatched->getNumParams(); i++) {
			out.push_back(unpatched->getValue(i));
		}
	}
	return out;
}

/// @brief How many entries of @p a and @p b differ.
/// @param a One snapshot.
/// @param b The other, of the same length.
/// @return The number of differing entries, or -1 if the lengths disagree.
int32_t differences(const std::vector<int32_t>& a, const std::vector<int32_t>& b) {
	if (a.size() != b.size()) {
		return -1;
	}
	int32_t n = 0;
	for (size_t i = 0; i < a.size(); i++) {
		n += (a[i] != b[i]) ? 1 : 0;
	}
	return n;
}

/// @brief Write @p value into every param of @p pm -- this side's own explicit sentinel.
///
/// Not "whatever `setupAsDefaultSynth` left": each side seeds its own model to a known,
/// non-default starting point through its own write, so neither the starting point nor the
/// expectation is derived from the other side's state.
/// @param pm    The param manager to seed.
/// @param value The value to write into every param.
void seed_params(ParamManagerForTimeline& pm, int32_t value) {
	if (PatchedParamSet* patched = pm.getPatchedParamSet(); patched != nullptr) {
		for (int32_t i = 0; i < patched->getNumParams(); i++) {
			patched->params[i].setCurrentValueBasicForSetup(value);
		}
	}
	if (UnpatchedParamSet* unpatched = pm.getUnpatchedParamSet(); unpatched != nullptr) {
		for (int32_t i = 0; i < unpatched->getNumParams(); i++) {
			unpatched->params[i].setCurrentValueBasicForSetup(value);
		}
	}
}

/// @brief A `UiContext` for a directly-invoked `handle()` call: no lowering, no held buttons.
struct DirectCtx {
	deluge::ui::shell::ButtonSet held{};
	deluge::ui::PadMask pads{};
	UiContext ctx;
	explicit DirectCtx(uint32_t now_ms = 0) : ctx{held, pads, false, false, now_ms, deluge_profile()} {}
};

/// A gold-knob turn event.
UiEvent gold_turn(EncoderRole role, int8_t delta) {
	UiEvent e{};
	e.kind = UiEvent::Kind::Encoder;
	e.encoder = role;
	e.delta = delta;
	return e;
}

/// A plain button event.
UiEvent button_event(ButtonId b, bool on) {
	UiEvent e{};
	e.kind = UiEvent::Kind::Button;
	e.button = b;
	e.on = on;
	return e;
}

/// @brief A scene that counts what it was handed and claims everything.
class RecordingScene final : public Scene {
public:
	SceneKind kind() const override { return SceneKind::FirstApp; }
	Result handle(const UiEvent& event, const UiContext&) override {
		calls++;
		last = event;
		Result r;
		r.handled = true;
		return r;
	}
	int calls = 0;
	UiEvent last{};
};

/// @brief A `Service` that counts what the router hands it and claims nothing.
class RecordingService final : public Service {
public:
	Result handle(const UiEvent& event, const UiContext&) override {
		calls++;
		last = event;
		return Result::unhandled();
	}
	int calls = 0;
	UiEvent last{};
};

/// @brief The stack, lowering and router the routing examples share.
struct RouteRig {
	RecordingScene scene;
	std::array<Scene*, 17> table{};
	SceneRegistry reg{};
	SceneStack stack{reg};
	RecordingService mod_knobs;
	InputLowering lowering{deluge_profile(), {}};
	Router router{stack, Services{&mod_knobs}, lowering};

	RouteRig() {
		table[16] = &scene;
		reg.by_kind = std::span<Scene* const>{table};
		(void)stack.push({SceneKind::FirstApp});
	}
	RouteRig(const RouteRig&) = delete;
	RouteRig& operator=(const RouteRig&) = delete;

	UiContext ctx() const {
		return UiContext{lowering.held(),   lowering.held_pads(),       lowering.shift(),
		                 lowering.sticky(), lowering.snapshot().now_ms, lowering.profile(),
		                 &lowering};
	}
};

/// @brief A live `ModKnobs` over a real (empty) scene stack and a real `InputLowering`.
struct KnobRig {
	SceneRegistry reg = scene_registry();
	SceneStack stack{reg};
	InputLowering lowering{deluge_profile(), {}};
	ModKnobs knobs{overlay::param_popup_scene(), stack, lowering};

	KnobRig() = default;
	KnobRig(const KnobRig&) = delete;
	KnobRig& operator=(const KnobRig&) = delete;

	/// Leave the stack empty again: the popup scene is a process singleton, so a pushed one would
	/// leak into the next example (E3 task 10's cross-contamination lesson).
	~KnobRig() {
		while (stack.depth() > 0) {
			stack.pop(SceneKind::Null);
		}
	}
};

} // namespace

// clang-format off
describe mod_knobs_spec_body("scenes::services::ModKnobs", $ {

	// Legacy's param naming resolves l10n strings, and nothing here boots the app, so
	// `l10n::chosenLanguage` is otherwise left null (`gui/l10n/l10n.cpp:6`) and
	// `getParamDisplayName` dereferences it -- the same `before_all` every other spec in this
	// directory needs, for the same reason.
	before_all([] { deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english; });

	it("clamps a turn exactly as View::calculateKnobPosForModEncoderTurn does", _ {
		// The pure half, compared against the live legacy function for every kind that branches in
		// it and every edge the clamp has. Both sides are mutation-proved in the task report.
		const Kind kinds[] = {Kind::PATCH_CABLE, Kind::MIDI, Kind::PATCHED, Kind::UNPATCHED_SOUND,
		                      Kind::UNPATCHED_GLOBAL, Kind::EXPRESSION, Kind::NONE};
		const int32_t positions[] = {-256, -192, -193, -64, -65, -1, 0, 1, 62, 63, 64, 65};
		const int32_t offsets[] = {-40, -5, -1, 0, 1, 5, 40};

		int32_t compared = 0;
		for (const Kind kind : kinds) {
			for (const int32_t pos : positions) {
				for (const int32_t offset : offsets) {
					const int32_t legacy = view.calculateKnobPosForModEncoderTurn(kind, pos, offset);
					const int32_t port = ModKnobs::calculate_knob_pos_for_mod_encoder_turn(kind, pos, offset);
					if (legacy != port) {
						// Name the disagreeing triple rather than just "false is not true".
						const std::string where = "kind=" + std::to_string(static_cast<int>(kind))
						                        + " pos=" + std::to_string(pos)
						                        + " offset=" + std::to_string(offset);
						expect(where + " port=" + std::to_string(port)
						       + " legacy=" + std::to_string(legacy)).to_equal(where);
					}
					compared++;
				}
			}
		}
		expect(compared).to_equal(int32_t(7 * 12 * 7));
	});

	it("folds a patch cable's position exactly as View::convertPatchCableKnobPosToIndicatorLevel does", _ {
		for (int32_t pos = -192; pos <= 192; pos++) {
			const int32_t legacy = view.convertPatchCableKnobPosToIndicatorLevel(pos);
			const int32_t port = ModKnobs::convert_patch_cable_knob_pos_to_indicator_level(pos);
			if (legacy != port) {
				expect("pos=" + std::to_string(pos) + " port=" + std::to_string(port)).to_equal(
				    "pos=" + std::to_string(pos) + " legacy=" + std::to_string(legacy));
			}
		}
	});

	it("writes the model exactly as legacy's View::modEncoderAction does, each side on its own clip", _ {
		StubDisplay stub;
		display = &stub;
		AudioEngine::audioSampleTimer = 0;

		spec::SongFixture f = spec::make_synth_song();
		InstrumentClip* legacy_clip = f.clip;
		InstrumentClip* port_clip = add_synth_clip(f.song, "1");

		// Each side seeds its own model, through its own write, to the same known non-default
		// starting point. `0` is a real, reachable param value and not the default any of these
		// params carries, so "the model moved" below is a real observation.
		constexpr int32_t kStart = 0;
		seed_params(legacy_clip->paramManager, kStart);
		seed_params(port_clip->paramManager, kStart);

		const std::vector<int32_t> legacy_before = param_snapshot(legacy_clip->paramManager);
		const std::vector<int32_t> port_before = param_snapshot(port_clip->paramManager);
		// The two sides really do start identical -- otherwise the comparison below proves nothing.
		expect(differences(legacy_before, port_before)).to_equal(int32_t(0));
		expect(legacy_before.empty()).to_be_false();

		// Legacy, through its own entry points.
		view.setActiveModControllableTimelineCounter(legacy_clip, false);
		view.setModRegion();
		view.modEncoderAction(0, 5);

		// The port, through its own -- a routed gold-knob turn on the LOWER knob, which is mod
		// encoder 0 (`hid/encoders.h:80-81`).
		KnobRig rig;
		rig.knobs.set_song(f.song);
		rig.knobs.set_target_with_timeline(port_clip, false);
		rig.knobs.set_mod_region();
		DirectCtx c{1000};
		const Result r = rig.knobs.handle(gold_turn(EncoderRole::GoldLower, 5), c.ctx);

		const std::vector<int32_t> legacy_after = param_snapshot(legacy_clip->paramManager);
		const std::vector<int32_t> port_after = param_snapshot(port_clip->paramManager);

		// Legacy moved the model; so did the port; and they agree, param for param, by identity.
		expect(differences(legacy_before, legacy_after)).to_equal(int32_t(1));
		expect(differences(port_before, port_after)).to_equal(int32_t(1));
		expect(differences(legacy_after, port_after)).to_equal(int32_t(0));

		// A gold-knob turn is consumed here, exactly as `interpretEncoders` consumes it.
		expect(r.handled).to_be_true();
	});

	it("takes the non-existent-param arm without writing anything, as legacy does", _ {
		StubDisplay stub;
		display = &stub;

		spec::SongFixture f = spec::make_synth_song();
		InstrumentClip* legacy_clip = f.clip;
		InstrumentClip* port_clip = add_synth_clip(f.song, "1");
		seed_params(legacy_clip->paramManager, 0);
		seed_params(port_clip->paramManager, 0);

		const std::vector<int32_t> legacy_before = param_snapshot(legacy_clip->paramManager);
		const std::vector<int32_t> port_before = param_snapshot(port_clip->paramManager);

		// A `Sound` in mod-knob mode 0 has a real param on both knobs, so the way to reach the
		// non-existent arm without inventing a `ModControllable` is the target with no controllable
		// at all -- `view.cpp:824`'s test, which skips the whole body on both sides.
		view.setActiveModControllableTimelineCounter(nullptr, false);
		view.modEncoderAction(0, 5);

		KnobRig rig;
		rig.knobs.set_song(f.song);
		rig.knobs.set_target_with_timeline(nullptr, false);
		DirectCtx c{1000};
		(void)rig.knobs.handle(gold_turn(EncoderRole::GoldLower, 5), c.ctx);

		expect(rig.knobs.target().mod_controllable == nullptr).to_be_true();
		expect(differences(legacy_before, param_snapshot(legacy_clip->paramManager))).to_equal(int32_t(0));
		expect(differences(port_before, param_snapshot(port_clip->paramManager))).to_equal(int32_t(0));
	});

	it("points at what View::setActiveModControllableTimelineCounter points at", _ {
		StubDisplay stub;
		display = &stub;

		spec::SongFixture f = spec::make_synth_song();
		InstrumentClip* clip = f.clip;

		view.setActiveModControllableTimelineCounter(clip, false);

		KnobRig rig;
		rig.knobs.set_song(f.song);
		rig.knobs.set_target_with_timeline(clip, false);

		// The five resolved fields, by identity against legacy's own resolved stack.
		expect(rig.knobs.target().timeline_counter
		       == view.activeModControllableModelStack.getTimelineCounterAllowNull()).to_be_true();
		expect(rig.knobs.target().mod_controllable
		       == view.activeModControllableModelStack.modControllable).to_be_true();
		expect(rig.knobs.target().param_manager
		       == view.activeModControllableModelStack.paramManager).to_be_true();
		expect(rig.knobs.target().note_row
		       == view.activeModControllableModelStack.getNoteRowAllowNull()).to_be_true();
		expect(rig.knobs.target().note_row_id).to_equal(view.activeModControllableModelStack.noteRowId);

		// And it really did resolve something, so the five equalities above are not all "null == null".
		expect(rig.knobs.target().mod_controllable != nullptr).to_be_true();
		expect(rig.knobs.target().param_manager != nullptr).to_be_true();

		// `getModKnobMode` agrees too.
		expect(rig.knobs.mode()).to_equal(view.getModKnobMode());
	});

	it("points at what View::setActiveModControllableWithoutTimelineCounter points at", _ {
		StubDisplay stub;
		display = &stub;

		spec::SongFixture f = spec::make_synth_song();
		// A bare `ModControllable` with no timeline: the clip's own `SoundInstrument` (a `Sound`,
		// which is a `ModControllable`) plus the clip's param manager -- the shape legacy's
		// audition/arranger callers pass.
		ModControllable* controllable = static_cast<SoundInstrument*>(f.instrument);
		ParamManager* params = &f.clip->paramManager;

		view.setActiveModControllableWithoutTimelineCounter(controllable, params);

		KnobRig rig;
		rig.knobs.set_song(f.song);
		rig.knobs.set_target_without_timeline(controllable, params);

		expect(rig.knobs.target().timeline_counter == nullptr).to_be_true();
		expect(view.activeModControllableModelStack.timelineCounterIsSet()).to_be_false();
		expect(rig.knobs.target().mod_controllable == view.activeModControllableModelStack.modControllable)
		    .to_be_true();
		expect(rig.knobs.target().param_manager == view.activeModControllableModelStack.paramManager).to_be_true();
		expect(rig.knobs.target().note_row == nullptr).to_be_true();
	});

	it("records the mod region legacy records", _ {
		StubDisplay stub;
		display = &stub;

		spec::SongFixture f = spec::make_synth_song();
		view.setActiveModControllableTimelineCounter(f.clip, false);
		view.setModRegion(1234, 56, 7);

		KnobRig rig;
		rig.knobs.set_song(f.song);
		rig.knobs.set_target_with_timeline(f.clip, false);
		rig.knobs.set_mod_region(1234, 56, 7);

		expect(rig.knobs.target().mod_pos).to_equal(view.modPos);
		expect(rig.knobs.target().mod_length).to_equal(view.modLength);
		expect(rig.knobs.target().mod_note_row_id).to_equal(view.modNoteRowId);

		// And the defaulted call is legacy's defaulted call (`view.h:81`).
		view.setModRegion();
		rig.knobs.set_mod_region();
		expect(rig.knobs.target().mod_pos).to_equal(view.modPos);
		expect(rig.knobs.target().mod_length).to_equal(view.modLength);
	});

	it("accelerates a repeated turn through InputLowering::calc_next_knob_speed", _ {
		// The carry item E1 opened and this task closes. `calc_next_knob_speed` itself is pinned by
		// `tests/spec/shell/input_lowering_spec.cpp` against the legacy arithmetic; what is checked
		// here is that `ModKnobs` is the consumer legacy's `hid/encoder_input.cpp:192` is -- i.e.
		// that a fast repeat moves the model further than a slow one does.
		StubDisplay stub;
		display = &stub;

		spec::SongFixture f = spec::make_synth_song();
		InstrumentClip* slow_clip = f.clip;
		InstrumentClip* fast_clip = add_synth_clip(f.song, "1");
		seed_params(slow_clip->paramManager, 0);
		seed_params(fast_clip->paramManager, 0);

		// Slow: each turn is a second apart, past `reset_speed_time_threshold` (0.3s), so the ramp
		// resets every time and the multiplier stays at its 1.0 floor.
		{
			KnobRig rig;
			rig.knobs.set_song(f.song);
			rig.knobs.set_target_with_timeline(slow_clip, false);
			rig.knobs.set_mod_region();
			for (uint32_t i = 0; i < 4; i++) {
				DirectCtx c{1000 + i * 1000};
				(void)rig.knobs.handle(gold_turn(EncoderRole::GoldLower, 1), c.ctx);
			}
		}
		// Fast: the same four turns, 10ms apart, all in the same direction -- the ramp builds.
		{
			KnobRig rig;
			rig.knobs.set_song(f.song);
			rig.knobs.set_target_with_timeline(fast_clip, false);
			rig.knobs.set_mod_region();
			for (uint32_t i = 0; i < 4; i++) {
				DirectCtx c{1000 + i * 10};
				(void)rig.knobs.handle(gold_turn(EncoderRole::GoldLower, 1), c.ctx);
			}
		}

		const std::vector<int32_t> slow = param_snapshot(slow_clip->paramManager);
		const std::vector<int32_t> fast = param_snapshot(fast_clip->paramManager);
		expect(differences(slow, fast) > 0).to_be_true();

		// Both moved the same single param, and the accelerated side moved it further.
		int32_t moved = -1;
		for (size_t i = 0; i < slow.size(); i++) {
			if (slow[i] != fast[i]) {
				moved = static_cast<int32_t>(i);
			}
		}
		expect(moved >= 0).to_be_true();
		// Guarded rather than indexed straight: a failure above must report as a failure, not as a
		// libstdc++ bounds assertion that takes the whole process (and every later example) down.
		if (moved >= 0) {
			expect(fast[static_cast<size_t>(moved)] > slow[static_cast<size_t>(moved)]).to_be_true();
		}
	});

	it("shows the value popup and pushes it exactly once, however many turns arrive", _ {
		StubDisplay stub;
		display = &stub;

		spec::SongFixture f = spec::make_synth_song();
		KnobRig rig;
		rig.knobs.set_song(f.song);
		rig.knobs.set_target_with_timeline(f.clip, false);
		rig.knobs.set_mod_region();

		expect(rig.stack.depth()).to_equal(size_t(0));
		for (uint32_t i = 0; i < 5; i++) {
			DirectCtx c{2000 + i * 200};
			(void)rig.knobs.handle(gold_turn(EncoderRole::GoldLower, 1), c.ctx);
		}
		expect(rig.stack.depth()).to_equal(size_t(1));
		expect(rig.stack.id_at(0).kind == overlay::kParamPopupSceneKind).to_be_true();
		expect(overlay::param_popup_scene().remaining_ms() > 0).to_be_true();
	});

	it("gives the popup the model context legacy reads off View, so its filter-morph name can fire", _ {
		// The interface decision this task made, and the example that spends it. E4 task 1 shipped
		// `ParamPopupScene::show()` with legacy's five value-conversion arguments only, so three of
		// legacy's arms -- the `ModControllableAudio*`-aware LPF/HPF-morph rename
		// (`modulation/params/param.cpp:356-381`), the stutter rate-bucket text, and the MIDI-clip
		// CC naming -- could never fire and were recorded as uncovered. `ModKnobs` is the class
		// that HAS that context and is the popup's first real caller, so `show()` was widened
		// (`overlay::PopupContext`) instead of leaving them dead. This drives the widened path and
		// the un-widened one and compares BOTH against the real
		// `View::displayModEncoderValuePopup`.
		StubDisplay stub;
		display = &stub;

		spec::SongFixture f = spec::make_synth_song();
		auto* sound = static_cast<SoundInstrument*>(f.instrument);
		// A ladder LPF is what makes the rename fire; an SVF or a filter that is off keeps the
		// default name (`param.cpp:365-372`'s own comment).
		sound->lpfMode = FilterMode::TRANSISTOR_24DB;

		constexpr int32_t kMorph = deluge::modulation::params::LOCAL_LPF_MORPH;

		// Legacy, with the context it reads off `View::activeModControllableModelStack`.
		AudioEngine::audioSampleTimer = 0;
		stub.showing = false;
		view.setActiveModControllableTimelineCounter(f.clip, false);
		view.displayModEncoderValuePopup(Kind::PATCHED, kMorph, 0);
		const std::string legacy_named = stub.last_title;

		// The port, with the same context, from its own (drained) state.
		overlay::param_popup_scene().tick(10000);
		expect(overlay::param_popup_scene().remaining_ms()).to_equal(uint32_t(0));
		overlay::PopupContext popup_context; // not `context`: CppSpec's own DSL owns that name here
		popup_context.mod = static_cast<ModControllableAudio*>(static_cast<ModControllable*>(sound));
		overlay::param_popup_scene().show(Kind::PATCHED, kMorph, 0, PatchSource::NONE, PatchSource::NONE, 60000,
		                                  popup_context);
		const std::string port_named{overlay::param_popup_scene().shown_name()};

		// And with NO context -- task 1's shape, which is what the widening was needed to escape.
		overlay::param_popup_scene().tick(10000);
		overlay::param_popup_scene().show(Kind::PATCHED, kMorph, 0, PatchSource::NONE, PatchSource::NONE, 120000);
		const std::string port_unnamed{overlay::param_popup_scene().shown_name()};

		expect(legacy_named.empty()).to_be_false();
		expect(port_named).to_equal(legacy_named);
		// Not vacuous: the context really is what selects this name. Without it the port falls back
		// to the mode-agnostic one, which is exactly the arm task 1 could not leave.
		expect(port_unnamed == port_named).to_be_false();
	});

	it("agrees with View::isClipContext on every reachable target shape", _ {
		// Fix round 1, Important 1: the port's predicate dropped legacy's outer
		// `if (activeModControllableModelStack.modControllable)` (`view.cpp:1734`), so a target with
		// a clip counter but no resolved mod controllable read as a clip context here and not in
		// legacy -- which makes `target_changed` send a MIDI CC legacy would never send. Shape 4 is
		// the one that fails without the guard.
		StubDisplay stub;
		display = &stub;

		spec::SongFixture f = spec::make_synth_song();
		auto* sound = static_cast<SoundInstrument*>(f.instrument);

		KnobRig rig;
		rig.knobs.set_song(f.song);

		// 1. No counter, no controllable.
		view.setActiveModControllableTimelineCounter(nullptr, false);
		rig.knobs.set_target_with_timeline(nullptr, false);
		expect(deluge::scenes::services::is_clip_context(rig.knobs.target())).to_equal(view.isClipContext());
		expect(view.isClipContext()).to_be_false();

		// 2. No counter, a controllable.
		view.setActiveModControllableWithoutTimelineCounter(sound, &f.clip->paramManager);
		rig.knobs.set_target_without_timeline(sound, &f.clip->paramManager);
		expect(deluge::scenes::services::is_clip_context(rig.knobs.target())).to_equal(view.isClipContext());
		expect(view.isClipContext()).to_be_false();

		// 3. A clip counter and a controllable -- the only true case.
		view.setActiveModControllableTimelineCounter(f.clip, false);
		rig.knobs.set_target_with_timeline(f.clip, false);
		expect(deluge::scenes::services::is_clip_context(rig.knobs.target())).to_equal(view.isClipContext());
		expect(view.isClipContext()).to_be_true();

		// 4. A clip counter and NO controllable. Each side is dropped into that shape through its
		//    own state -- legacy's public stack member, the port's own `ModTarget` -- because no
		//    entry point on either side reaches it directly on this fixture.
		view.activeModControllableModelStack.modControllable = nullptr;
		deluge::scenes::services::ModTarget no_controllable = rig.knobs.target();
		no_controllable.mod_controllable = nullptr;
		expect(deluge::scenes::services::is_clip_context(no_controllable)).to_equal(view.isClipContext());
		expect(view.isClipContext()).to_be_false();

		// 5. The SONG as the counter, with a controllable: set, but not a clip. `affectEntire` is
		//    what makes `Song::getActiveModControllable` resolve one at all (`song.cpp:5537-5544`);
		//    without it the song resolves nothing and this shape would collapse into shape 1.
		f.song->affectEntire = true;
		view.setActiveModControllableTimelineCounter(f.song, false);
		rig.knobs.set_target_with_timeline(f.song, false);
		expect(deluge::scenes::services::is_clip_context(rig.knobs.target())).to_equal(view.isClipContext());
		expect(view.isClipContext()).to_be_false();
		// ...and it really is pointed at something, so shape 5 is not shape 1 in disguise.
		expect(rig.knobs.target().timeline_counter != nullptr).to_be_true();
		expect(rig.knobs.target().mod_controllable != nullptr).to_be_true();
	});

	it("routes a gold-knob turn to the service first and a gold-knob push to the scene first", _ {
		// Step 5's subject is the PUSH, and that half is legacy's exactly:
		// `MOD_ENCODER_0`/`MOD_ENCODER_1` are handled in `Buttons::buttonAction`'s TAIL, after
		// `getCurrentUI()->buttonAction` (`hid/buttons.cpp:225-231`), so the scene sees the push
		// first and the service never sees it at all. That is what this example is here to pin.
		//
		// The turn and the mod button are pinned here as they are ROUTED TODAY, which is NOT
		// legacy's order: `interpretEncoders` calls `getCurrentUI()->modEncoderAction`
		// (`hid/encoder_input.cpp:194`) and the mod-button loop calls
		// `getCurrentUI()->modButtonAction` (`hid/buttons.cpp:104`), with `UI`'s own base
		// implementations forwarding to `view` as the fallback (`gui/ui/ui.cpp:35-41`) -- so both
		// should reach the top scene first and `mod_knobs` second. E4 task 2 found that; the
		// controller ruled the chain becomes top scene -> mod_knobs -> global_controls, implemented
		// as E4 task 3's first steps with its own spec. **These two `expect`s change with that
		// fix**; the `ModEnc0`/`ModEnc1` ones do not.
		RouteRig r;

		r.router.route(gold_turn(EncoderRole::GoldLower, 1), r.ctx());
		expect(r.mod_knobs.calls).to_equal(1);
		expect(r.scene.calls).to_equal(1); // the recording service claims nothing, so it falls through

		r.router.route(button_event(ButtonId::ModEnc0, true), r.ctx());
		expect(r.mod_knobs.calls).to_equal(1); // unchanged: a knob PUSH is not a mod_knobs event
		expect(r.scene.calls).to_equal(2);
		expect(r.scene.last.button == ButtonId::ModEnc0).to_be_true();

		r.router.route(button_event(ButtonId::ModEnc1, true), r.ctx());
		expect(r.mod_knobs.calls).to_equal(1);
		expect(r.scene.calls).to_equal(3);

		// A mod BUTTON, by contrast, does reach the service first -- `hid/buttons.cpp:100-107`.
		r.router.route(button_event(ButtonId::Mod0, true), r.ctx());
		expect(r.mod_knobs.calls).to_equal(2);
		expect(r.scene.calls).to_equal(4);
	});

	it("ignores a knob push handed to it directly, so ordering is the only thing keeping them apart", _ {
		// Defence in depth behind the routing example above: even called directly, `ModKnobs` has
		// nothing to say about `ButtonId::ModEnc0` -- `View::modEncoderButtonAction`
		// (`view.cpp:1332-1350`) is E4 task 3's citation, not this task's.
		spec::SongFixture f = spec::make_synth_song();
		KnobRig rig;
		rig.knobs.set_song(f.song);
		DirectCtx c{0};
		expect(rig.knobs.handle(button_event(ButtonId::ModEnc0, true), c.ctx).handled).to_be_false();
		expect(rig.knobs.handle(gold_turn(EncoderRole::Select, 1), c.ctx).handled).to_be_false();
		expect(rig.knobs.handle(gold_turn(EncoderRole::GoldLower, 0), c.ctx).handled).to_be_false();
	});

	it("arms notifyParamAutomationOccurred's two timers exactly when legacy does", _ {
		StubDisplay stub;
		display = &stub;

		spec::SongFixture f = spec::make_synth_song();
		KnobRig rig;
		rig.knobs.set_song(f.song);
		rig.knobs.set_target_with_timeline(f.clip, false);
		rig.knobs.tick(5000, 0);

		// A param manager the knobs are NOT pointed at arms nothing (`view.cpp:1692`).
		ParamManagerForTimeline other;
		rig.knobs.notify_param_automation_occurred(&other, true);
		expect(rig.knobs.automation_display_pending()).to_be_false();
		expect(rig.knobs.next_timer().has_value()).to_be_false();

		// The one they are pointed at arms both, 25ms out (`:1699`, `:1708`).
		rig.knobs.notify_param_automation_occurred(rig.knobs.target().param_manager, false);
		expect(rig.knobs.automation_display_pending()).to_be_true();
		expect(rig.knobs.automation_updates_mod_levels()).to_be_false();
		expect(rig.knobs.next_timer().value()).to_equal(uint32_t(25));

		// A second call inside the window does not re-arm, but does upgrade the payload
		// (`:1702-1706`).
		rig.knobs.tick(5010, 10);
		rig.knobs.notify_param_automation_occurred(rig.knobs.target().param_manager, true);
		expect(rig.knobs.automation_updates_mod_levels()).to_be_true();
		expect(rig.knobs.next_timer().value()).to_equal(uint32_t(15));

		// And it disarms on its own deadline, with no input.
		rig.knobs.tick(5025, 15);
		expect(rig.knobs.automation_display_pending()).to_be_false();
		expect(rig.knobs.next_timer().has_value()).to_be_false();
	});
});
// clang-format on

DELUGE_SPEC(mod_knobs_spec, mod_knobs_spec_body)
