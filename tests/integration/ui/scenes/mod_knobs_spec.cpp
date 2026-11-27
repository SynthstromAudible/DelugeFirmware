/// @file
/// @brief `services::ModKnobs` -- driven on a real `Song` built by `support/song_fixture.h` and asserted against the
///        values the root views' gold-knob code (`View::modEncoderAction` and friends, `gui/views/view.cpp`) was
///        recorded producing.
///
/// @par The oracle, and why it is the model and not the port's report of it
///
/// The central example here (`writes the model exactly as legacy does`) does **not** ask
/// `ModKnobs` what it did. It reads the resulting `ParamManager` -- every value in both param sets,
/// by identity -- and requires that exactly one param moved, which one, and to what value; the index and value are
/// recorded from the turn `View::modEncoderAction` made on the same fixture. The example seeds its own param manager
/// with an explicit write, so the starting point is not derived from the port's state, and the movement assertion
/// fails a port that wrote nothing.
/// The knobs are pointed at a focus as the view resolves its target (`focus_of`).
///
/// @par What this spec counts as uncovered (oracle discipline)
///
///   * **The popup's performance-view and song-browser suppression arms** (`View::modEncoderAction_existentParam` in
///     `origin/main:src/deluge/gui/views/view.cpp`). `PerformanceScene` does not exist and the song browser is not
///     among the legacy UIs `BrowserScene` carries, so there is no port-side state that could make either fire. See
///     `GuestKnobPresentation::suppress_value_popup`.
///   * **`PopupContext::stuttering` true**: no example plays a stutter while a knob turns.
///   * **`PopupContext::midi_clip`, the MIDI-clip CC/expression naming arm** (`View::displayModEncoderValuePopup` in
///     `views/view.cpp`): no example compares its text with legacy's. The filter-aware naming half of the context IS
///     driven, below.
///   * **`ModKnobs::notify_param_automation_occurred`'s `DISPLAY_AUTOMATION` consumer.** Legacy's
///     timer callback is `automationView.displayAutomation()` / `view.displayAutomation()`
///     (`UITimerManager::routine` in `gui/ui_timer_manager.cpp`), both view-side and not ported; only the *arming* is
///     compared here.

#include "base/notify/dirty_set.h"
#include "base/notify/notice_queue.h"
#include "board_controls.hpp"
#include "gui/ui/menu_view_of.h"
#include "gui/ui/shell_host/host_ports.h"
#include "modulation/automation/record_events.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/node_id.h"
#include "ui/scenes/overlay/param_popup_scene.h"
#include "ui/scenes/registry.h"
#include "ui/scenes/services/mod_knobs.h"

#include "project/device_settings.h"
#include "ui/controls/knob_math.h"

#include "gui/views/knob_focus_from_stack.h"
#include "ui/scenes/bind/knob_assignment.h"

#include "ui/shell/input_lowering.h"
#include "ui/shell/profiles/deluge.h"
#include "ui/shell/router.h"
#include "ui/shell/scene_stack.h"

#include "effects/plugin.h"
#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "gui/ui/ui.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "instruments/classic/params.h"
#include "instruments/classic/sound_state.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_follow.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/edits/set_mod_knob_cc.h"
#include "model/edits/set_mod_knob_mode.h"
#include "model/instrument/kit.h"
#include "model/instrument/midi_instrument.h"
#include "model/mod_controllable/filters/filter_config.h"
#include "model/model_stack.h"
#include "model/project_bridge/param_ids.h"
#include "model/project_bridge/param_stack.h"
#include "model/project_bridge/patch_cable_words.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/midi/midi_param_collection.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "storage/storage_manager.h"

#include "support/fake_host_rig.h"
#include "support/global_guest_menu.h"
#include "support/mocks/fake_surfaces.h"
#include "support/printers/fx_chain_printer.h"
#include "support/printers/knob_focus_printer.h"
#include "support/printers/param_id_printer.h"
#include "support/recorded_knob_indicators.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"
#include "support/matchers/value_matchers.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "support/menu_context.h"
#include "support/spec_main.h"

using namespace deluge::ui::scenes;
using deluge::modulation::params::Kind;
using deluge::ui::scenes::services::ModKnobs;
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
using deluge::ui::widgets::core::EncoderRole;

namespace {

/// `Action` unqualified is the model's own undo `Action`, reachable through `gui/views/view.h`'s
/// `model/model_stack.h`; the shell's is a different type with the same name (every other spec in
/// this directory makes the same alias for the same reason).
using ShellAction = deluge::ui::shell::Action;

/// @brief The minimum `Display` legacy's popup needs; `display` is null in a spec process
///        (`hid/display/display.cpp`) and `View::displayModEncoderValuePopup` dereferences it.
///
/// The popup's *pixels* are `param_popup_spec.cpp`'s subject, driven against the real
/// `OLED::displayNotification`; what this records is the one thing that spec cannot reach, the
/// parameter *name* legacy chose for a MIDI clip -- see the `PopupContext` example below.
struct StubDisplay final : deluge::hid::Display {
	std::string last_popup; ///< the text of the latest popupText
	int notifications = 0;
	std::string last_title;
	bool showing = false;

	constexpr size_t getNumBrowserAndMenuLines() override { return 3; }
	void displayPopup(char const*, int8_t, bool, uint8_t, int32_t, PopupType) override {}
	void popupText(char const* text, PopupType) override { last_popup = text; }
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

/// @brief A second (or third) default synth clip in @p song -- `make_synth_song`'s own recipe (`song_fixture.h`,
///        the card-less default synth of `Song::ensureAtLeastOneSessionClip` in `song/song.cpp`), applied again.
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
/// The model-side readback the central example compares. `ParamSet::getValue` is `AutoParam::getCurrentValue()` -- what
/// is actually in the model, not what either side reported having put there.
/// @param pm The param manager to read.
/// @return One entry per param, patched set first.
std::vector<int32_t> param_snapshot(ParamManager& pm) {
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

/// @brief The one param that differs between two snapshots, as "index=value".
/// @param before The earlier snapshot.
/// @param after  The later snapshot, of the same length.
/// @return "index=value" of the single entry that differs; "none" when nothing differs and "several" when more than one
///         does, or the lengths disagree.
std::string sole_change(const std::vector<int32_t>& before, const std::vector<int32_t>& after) {
	if (before.size() != after.size()) {
		return "several";
	}
	std::string found = "none";
	for (size_t i = 0; i < before.size(); i++) {
		if (before[i] == after[i]) {
			continue;
		}
		if (found != "none") {
			return "several";
		}
		found = std::to_string(i) + "=" + std::to_string(after[i]);
	}
	return found;
}

/// @brief Write @p value into every param of @p pm -- this side's own explicit sentinel.
///
/// Not "whatever `setupAsDefaultSynth` left": each side seeds its own model to a known,
/// non-default starting point through its own write, so neither the starting point nor the
/// expectation is derived from the other side's state.
/// @param pm    The param manager to seed.
/// @param value The value to write into every param.
void seed_params(ParamManager& pm, int32_t value) {
	if (PatchedParamSet* patched = pm.getPatchedParamSet(); patched != nullptr) {
		for (int32_t i = 0; i < patched->getNumParams(); i++) {
			patched->params[i].setPlayed(value);
		}
	}
	if (UnpatchedParamSet* unpatched = pm.getUnpatchedParamSet(); unpatched != nullptr) {
		for (int32_t i = 0; i < unpatched->getNumParams(); i++) {
			unpatched->params[i].setPlayed(value);
		}
	}
}

/// @brief The host BSP's shadow of the knob indicator columns.
///
/// `indicator_leds::clearKnobIndicatorLevels` is the one legacy writer left that the "clears both columns" example
/// drives, and in the host BSP its sink keeps a shadow of what it was last handed (`record_ring` in
/// `peripherals/ui_harness.rs`, called from `deluge_control_set_indicator` in `peripherals/control.rs`).
///
/// Declared rather than included: `tests/harness/level3/host_script.h` is not on this target's include path, and an
/// `extern "C"` line is cheaper -- and more explicit about the dependency -- than another include directory.
extern "C" const uint8_t* host_script_ring_shadow(uint8_t which);

/// @brief One mod button's LED as the app declares it: lit or not, and whether it blinks.
///
/// Two facts, because `LedState` distinguishes three cases and "lit" alone cannot tell a steady LED from a blinking
/// one.
struct ModLed {
	bool lit = false;
	bool blinking = false;
	bool operator==(const ModLed&) const = default;
};

/// @brief Prints an LED's two facts, 1 for set, e.g. `{lit 1, blinking 0}`.
/// @param os The stream to print to.
/// @param led The LED.
/// @return @p os.
std::ostream& operator<<(std::ostream& os, const ModLed& led) {
	return os << "{lit " << int{led.lit} << ", blinking " << int{led.blinking} << "}";
}

/// @brief Read mod button @p i's LED out of a `LedFrame`'s declaration.
///
/// By identity: the entry must be *present* (a missing declaration is a defect, not an "off").
/// @param frame The composed frame.
/// @param i     0..7.
/// @return The declared state, or `std::nullopt` when nothing declared that LED.
std::optional<ModLed> port_mod_led(const deluge::ui::widgets::core::LedFrame& frame, int32_t i) {
	const auto want = static_cast<deluge::ui::widgets::core::LedId>(static_cast<uint8_t>(ButtonId::Mod0) + i);
	for (const auto& [id, state] : frame.entries()) {
		if (id != want) {
			continue;
		}
		switch (state.kind) {
		case deluge::ui::widgets::core::LedState::Kind::Off:
			return ModLed{false, false};
		case deluge::ui::widgets::core::LedState::Kind::On:
			return ModLed{true, false};
		case deluge::ui::widgets::core::LedState::Kind::Blink:
		case deluge::ui::widgets::core::LedState::Kind::Shared:
			// `blinkLed`'s default `initialState` is true (`indicator_leds.h`), and
			// `ui/shell/leds.h`'s `led_is_on` reads phase 0 of a blink as lit too, so a freshly
			// declared blink is lit.
			return ModLed{true, true};
		}
	}
	return std::nullopt;
}

/// @brief Which mod LEDs are lit, one bit per LED, after each mod button 0..7 is pressed: that button's LED alone,
/// steady. Recorded from `View::setModLedStates` through the host BSP's LED shadow.
inline constexpr std::array<uint8_t, 8> kRecordedModLeds = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};

/// @brief The four segment brightnesses `indicator_leds` last sent for knob @p which, or `nullptr`.
const uint8_t* indicator_shadow(int32_t which) {
	return host_script_ring_shadow(static_cast<uint8_t>(which));
}

/// @brief A `UiContext` for a directly-invoked `handle()` call: no lowering, no held buttons.
struct DirectCtx {
	deluge::ui::shell::ButtonSet held{};
	deluge::ui::widgets::core::PadMask pads{};
	UiContext ctx;
	/// Both clocks at @p now_ms: the tick's, and the one acceleration is timed on.
	explicit DirectCtx(uint32_t now_ms = 0)
	    : ctx{held, pads, false, false, now_ms, deluge_profile(), nullptr, now_ms} {}
};

/// Empty the notice queue, so a case counts only what it raises.
void drain_notices() {
	deluge::base::notify::Notice discarded;
	while (deluge::base::notify::notices().pop(discarded)) {}
}

/// A gold-knob turn event.
UiEvent gold_turn(EncoderRole role, int8_t delta) {
	UiEvent e{};
	e.kind = UiEvent::Kind::Encoder;
	e.encoder = role;
	e.delta = delta;
	return e;
}

/// An encoder-button event -- the shape `InputLowering` emits for `ButtonId::ModEnc0`/`ModEnc1`
/// and `SelectEnc` (`InputLowering::on_button`).
UiEvent encoder_button_event(ButtonId b, bool on) {
	UiEvent e{};
	e.kind = UiEvent::Kind::EncoderButton;
	e.button = b;
	e.on = on;
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

/// @brief A scene that counts what it was handed and claims everything, unless told not to.
///
/// `claim` is what makes this rig able to stand in for both sides of legacy's dispatch: a UI that
/// *overrides* `modEncoderAction`/`modButtonAction` (claim = true) and one that inherits `UI`'s
/// base implementation and so drops through to `view` (claim = false, the router's `mod_knobs`
/// step).
class RecordingScene final : public Scene {
public:
	SceneKind kind() const override { return SceneKind::FirstApp; }
	Result handle(const UiEvent& event, const UiContext&) override {
		calls++;
		last = event;
		Result r;
		r.handled = claim;
		return r;
	}
	bool claim = true;
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

// --------------------------------------------------------------------------------------------- A one-column horizontal
// page bound to `LOCAL_LPF_RESONANCE` -- `modKnobs[1][0]`'s param (`Sound::Sound` in
// `origin/main:src/deluge/processing/sound/sound.cpp`), which is what `GoldLower` actually edits on the
// `SoundInstrument` `spec::make_synth_song()` builds: measured (not assumed) against the fixture's own default mod-knob
// mode, which is 1 (the FILTER page), not 0. Built to test `GuestKnobPresentation::suppress_value_popup`'s
// horizontal-page forwarding: the menu's own focused *column*, not the page node, is what the same-param guard has to
// compare against.
namespace mod_knobs_spec_nodes {
inline constexpr deluge::ui::scenes::menu::Node res_col =
    deluge::ui::scenes::menu::param(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_RESONANCE),
                                    deluge::ui::scenes::menu::sound_param(deluge::plugin::ParamId{
                                        static_cast<uint32_t>(deluge::modulation::params::LOCAL_LPF_RESONANCE)}));
inline constexpr const deluge::ui::scenes::menu::Node* kResPageChildren[] = {&res_col};
inline constexpr deluge::ui::scenes::menu::Node res_page =
    deluge::ui::scenes::menu::submenu(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_LPF), kResPageChildren,
                                      deluge::ui::scenes::menu::Layout::Horizontal);
inline constexpr const deluge::ui::scenes::menu::Node* kRoots[] = {&res_page};
} // namespace mod_knobs_spec_nodes

/// @brief A live `ModKnobs` over a real (empty) scene stack and a real `InputLowering`.
/// @brief The knobs' focus on @p counter's active mod controllable, resolved as legacy's
///        `View::setActiveModControllableTimelineCounter` resolves it (`views/view.cpp`), over a stack of its own.
deluge::ui::scenes::bind::KnobFocus focus_of(Song& song, TimelineCounter* counter) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	if (counter != nullptr) {
		counter = counter->getTimelineCounterToRecordTo();
	}
	ModelStackWithTimelineCounter* stack = setupModelStackWithSong(memory, &song)->addTimelineCounter(counter);
	if (counter != nullptr) {
		counter->getActiveModControllable(stack);
	}
	else {
		stack->addOtherTwoThingsButNoNoteRow(deluge::model::LaneOwner{}, nullptr);
	}
	return deluge::gui::knobFocusFromStack(song, *reinterpret_cast<ModelStackWithThreeMainThings*>(memory));
}

/// @brief The owner stack of the view's own knob focus, resolved in @p memory: none unless the focus is on the current
///        song.
/// @param memory A model-stack buffer.
ModelStackWithThreeMainThings* view_knob_stack(void* memory) {
	return view.knobOwnerStack(memory);
}

/// @brief The knobs' focus on a bare mod controllable, as `View::setActiveModControllableWithoutTimelineCounter`.
deluge::ui::scenes::bind::KnobFocus focus_without(Song& song, deluge::model::LaneOwner controllable,
                                                  ParamManager* params) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	(void)setupModelStackWithSong(memory, &song)
	    ->addTimelineCounter(nullptr)
	    ->addOtherTwoThingsButNoNoteRow(controllable, params);
	return deluge::gui::knobFocusFromStack(song, *reinterpret_cast<ModelStackWithThreeMainThings*>(memory));
}

struct KnobRig {
	SceneRegistry reg = scene_registry();
	SceneStack stack{reg};
	InputLowering lowering{deluge_profile(), {}};
	ModKnobs knobs{overlay::param_popup_scene(), stack, lowering, kDelugeControls};
	Song* song = nullptr;

	/// Point the knobs at @p s's record, over the app's ports.
	void set_song(Song* s) {
		song = s;
		knobs.set_ports(&deluge::gui::shell_host::host_ports());
		knobs.set_model(s != nullptr ? &s->project : nullptr);
	}
	/// Point the knobs where legacy's view would point them at @p counter.
	void set_target_with_timeline(TimelineCounter* counter, bool send_midi_feedback) {
		knobs.set_focus(focus_of(*song, counter), send_midi_feedback);
	}
	/// Point the knobs where legacy's view would point them at a bare owner.
	void set_target_without_timeline(deluge::model::LaneOwner controllable, ParamManager* params) {
		knobs.set_focus(focus_without(*song, controllable, params), true);
	}

	KnobRig() = default;
	KnobRig(const KnobRig&) = delete;
	KnobRig& operator=(const KnobRig&) = delete;

	/// Leave the stack empty again: the popup scene is a process singleton, so a pushed one would
	/// leak into the next example (the cross-contamination every `~Rig` in this directory guards against).
	~KnobRig() {
		while (stack.depth() > 0) {
			stack.pop(SceneKind::Null);
		}
		menu_scene().reset_process_state(); // each example starts with the rows a boot has
	}
};

/// @brief A presentation that counts everything a gesture shows.
class CountingPresentation final : public deluge::ui::scenes::services::KnobPresentation {
public:
	int shown = 0;
	bool suppress_value_popup(const deluge::ui::scenes::services::ModTurn&) override { return false; }
	void show_value_popup(const deluge::ui::scenes::services::ModTurn&, PatchSource, PatchSource,
	                      const deluge::ui::scenes::overlay::PopupContext&, uint32_t) override {
		shown++;
	}
	void value_written() override { shown++; }
	void blink_indicator(int32_t, bool) override { shown++; }
	void stop_blink_indicator(int32_t) override { shown++; }
	void indicators_changed() override { shown++; }
	void mode_changed() override { shown++; }
	void vu_sidebar_changed() override { shown++; }
	void note_tails_changed() override { shown++; }
	void popup_text(std::string_view) override { shown++; }
	void notice_text(std::string_view) override { shown++; }
	void notification(std::string_view, std::string_view) override { shown++; }
	void setting_indicator(int32_t, int32_t) override { shown++; }
	void popup(deluge::l10n::String) override { shown++; }
	void cancel_popup() override { shown++; }
};

/// @brief How many CCs MIDI follow has stamped as sent since @p clear_stamps.
size_t stamped_ccs() {
	size_t count = 0;
	for (const uint32_t stamp : midiFollow.timeLastCCSent) {
		count += stamp != 0 ? 1 : 0;
	}
	return count;
}

void clear_stamps() {
	for (uint32_t& stamp : midiFollow.timeLastCCSent) {
		stamp = 0;
	}
}

/// @brief Both knob indicators, the port's against the levels `View::setKnobIndicatorLevel` was recorded putting up,
///        over every mode and a turn each way on each knob: where they first differ, by mode, offset and knob.
/// @param rig            The knobs, pointed at the fixture, with a scene on the stack so indicators are declared.
/// @param song           The fixture's song, current while the knobs act.
/// @param label          Names the fixture in each disagreement.
/// @param recorded       The levels recorded at each step: entry `mode * 3 + offset index`.
/// @param after_modes    Run once the modes are done (a push), then compared again; may be empty.
/// @param after_recorded The levels recorded after `after_modes`.
std::vector<std::string> indicator_disagreements(KnobRig& rig, Song* song, const std::string& label,
                                                 const std::array<knob_recorded::KnobIndicatorStep, 24>& recorded,
                                                 const std::function<void(DirectCtx&)>& after_modes = {},
                                                 const knob_recorded::KnobIndicatorStep& after_recorded = {}) {
	std::vector<std::string> found;
	currentSong = song;
	const auto compare = [&](const std::string& at, const knob_recorded::KnobIndicatorStep& want) {
		deluge::ui::widgets::core::LedFrame frame;
		rig.knobs.leds(frame);
		for (int32_t k = 0; k < 2; k++) {
			const deluge::ui::widgets::core::KnobIndicator* port =
			    frame.indicator_or_null(static_cast<deluge::ui::widgets::core::IndicatorId>(k));
			const std::string where = label + " " + at + " knob=" + std::to_string(k);
			if (port == nullptr) {
				found.push_back(where + " port-null");
				continue;
			}
			const std::array<uint8_t, 4>& want_levels = k == 0 ? want.lower : want.upper;
			for (int32_t seg = 0; seg < 4; seg++) {
				if (port->levels[seg] != want_levels[static_cast<size_t>(seg)]) {
					found.push_back(where + " seg=" + std::to_string(seg) + " port=" + std::to_string(port->levels[seg])
					                + " recorded=" + std::to_string(want_levels[static_cast<size_t>(seg)]));
					break;
				}
			}
		}
	};
	DirectCtx c{0};
	size_t step = 0;
	for (int32_t mode = 0; mode < 8; mode++) {
		rig.knobs.mod_button_action(mode, true);
		rig.knobs.mod_button_action(mode, false);
		for (int32_t offset : knob_recorded::kIndicatorOffsets) {
			if (offset != 0) {
				(void)rig.knobs.handle(gold_turn(EncoderRole::GoldLower, static_cast<int8_t>(offset)), c.ctx);
				(void)rig.knobs.handle(gold_turn(EncoderRole::GoldUpper, static_cast<int8_t>(-offset)), c.ctx);
			}
			compare("mode=" + std::to_string(mode) + " offset=" + std::to_string(offset), recorded[step++]);
		}
	}
	if (after_modes) {
		after_modes(c);
		compare("after", after_recorded);
	}
	return found;
}

/// The CC the fake's MIDI follow sends a synth's LPF frequency on.
constexpr uint8_t kLpfFrequencyCc = 74;

/// @brief The knobs over the ports' fakes (`spec::FakeHostRig`) and its model song: one synth on the filter's mod
///        button, whose upper knob moves the LPF frequency, with the knobs on its clip and the fakes' set installed.
struct FakeKnobs {
	spec::FakeHostRig fakes;
	bind::Ports ports = fakes.host.ports();
	KnobRig rig;
	CountingPresentation out;

	FakeKnobs() {
		fakes.hold_synth(deluge::instruments::classic::SoundState{});
		deluge::project::DocOutput& output =
		    fakes.model.output_structure.getOrCreate(spec::FakeHostRig::kSynthOutputId);
		output.mod_knob_mode = 1;
		output.controls = {
		    {.control = {.button = 1, .knob = 1}, .target = deluge::instruments::classic::param::kLpfFrequency}};
		fakes.host.midi_feedback.ccs.emplace_back(
		    deluge::project::OwnerPlugin::INSTRUMENT,
		    deluge::project::engine_target(deluge::instruments::classic::param::kLpfFrequency.value), kLpfFrequencyCc);
		rig.knobs.set_ports(&ports);
		rig.knobs.set_model(&fakes.model);
		rig.knobs.set_focus(bind::KnobFocus{.owner = bind::SoundFocus{.slot = spec::FakeHostRig::kSynthSlot,
		                                                              .clip = spec::FakeHostRig::kSynthClip}},
		                    false);
	}
	FakeKnobs(const FakeKnobs&) = delete;
	FakeKnobs& operator=(const FakeKnobs&) = delete;

	/// @brief How many region grabs the knobs' sink was asked for.
	[[nodiscard]] size_t grabs() const {
		return static_cast<size_t>(std::ranges::count_if(fakes.host.controls_sink.gestures, [](const auto& g) {
			return g.kind == bind::test::RecordingControlsSink::Gesture::Kind::GRAB_REGION;
		}));
	}
};

/// @brief Report each disagreement as its own line.
void report_indicators(const std::vector<std::string>& found) {
	for (const std::string& why : found) {
		std::fputs(("MOD-INDICATOR " + why + "\n").c_str(), stdout);
	}
}

} // namespace

// clang-format off
describe mod_knobs_spec_body("scenes::services::ModKnobs", $ {

	// Legacy's param naming resolves l10n strings, and nothing here boots the app, so
	// `l10n::chosenLanguage` is otherwise left null (`gui/l10n/l10n.cpp`) and
	// `getParamDisplayName` dereferences it -- the same `before_all` every other spec in this
	// directory needs, for the same reason.
	before_all([] { deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english; });

	it("clamps a turn exactly as View::calculateKnobPosForModEncoderTurn does", _ {
		// The pure half, compared against the live legacy function for every kind that branches in
		// it and every edge the clamp has. Both sides are mutation-proved.
		const Kind kinds[] = {Kind::PATCH_CABLE, Kind::MIDI, Kind::PATCHED, Kind::UNPATCHED_SOUND,
		                      Kind::UNPATCHED_GLOBAL, Kind::EXPRESSION, Kind::NONE};
		const int32_t positions[] = {-256, -192, -193, -64, -65, -1, 0, 1, 62, 63, 64, 65};
		const int32_t offsets[] = {-40, -5, -1, 0, 1, 5, 40};

		int32_t compared = 0;
		for (const Kind kind : kinds) {
			for (const int32_t pos : positions) {
				for (const int32_t offset : offsets) {
					const int32_t legacy = view.calculateKnobPosForModEncoderTurn(kind, pos, offset);
					const auto knob = kind == Kind::PATCH_CABLE ? deluge::ui::controls::KnobKind::CABLE
					                  : kind == Kind::MIDI      ? deluge::ui::controls::KnobKind::MIDI_CC
					                                            : deluge::ui::controls::KnobKind::PARAM;
					const int32_t port = deluge::ui::controls::turn(knob, pos, offset);
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
			const int32_t port = deluge::ui::controls::fold_cable(pos);
			if (legacy != port) {
				expect("pos=" + std::to_string(pos) + " port=" + std::to_string(port)).to_equal(
				    "pos=" + std::to_string(pos) + " legacy=" + std::to_string(legacy));
			}
		}
	});

	it("writes the model exactly as the recorded View::modEncoderAction turn does", _ {
		StubDisplay stub;
		display = &stub;
		AudioEngine::audioSampleTimer = 0;

		spec::SongFixture f = spec::make_synth_song();
		InstrumentClip* clip = f.clip;

		// The model is seeded to a known non-default starting point through an explicit write. `0` is a real,
		// reachable param value and not the default any of these params carries, so "the model moved" below is a
		// real observation.
		constexpr int32_t kStart = 0;
		seed_params(clip->paramManager, kStart);
		const std::vector<int32_t> before = param_snapshot(clip->paramManager);
		expect(before.empty()).to_be_false();

		// A routed gold-knob turn on the LOWER knob, which is mod encoder 0 (`hid/encoders.h`'s `mod0`/`mod1`).
		KnobRig rig;
		rig.set_song(f.song);
		rig.set_target_with_timeline(clip, false);
		rig.knobs.set_mod_region();
		DirectCtx c{1000};
		const Result r = rig.knobs.handle(gold_turn(EncoderRole::GoldLower, 5), c.ctx);

		const std::vector<int32_t> after = param_snapshot(clip->paramManager);

		// Exactly one param moved. Recorded from `View::modEncoderAction` (lower knob, +5) on a synth clip: param 11
		// ends at 167772160.
		expect(sole_change(before, after)).to_equal("11=167772160");

		// A gold-knob turn is consumed here, exactly as `interpretEncoders` consumes it.
		expect(r.handled).to_be_true();
	});

	it("blinks one indicator at a time: centring the other knob re-levels the first, as the recorded view does", _ {
		StubDisplay stub;
		display = &stub;
		AudioEngine::audioSampleTimer = 0;

		spec::SongFixture f = spec::make_synth_song();
		InstrumentClip* clip = f.clip;
		// Delay feedback and delay rate, the delay mode's two knobs, both indicate their middle
		// (`PatchedParamSet::shouldParamIndicateMiddleValue`), so a turn that lands on the centre blinks.
		seed_params(clip->paramManager, 0);
		constexpr int32_t kDelayMode = 3;
		currentSong = f.song;

		// Centre the lower knob, then the upper one, the four turns a few milliseconds apart: well inside one blink.
		KnobRig rig;
		rig.set_song(f.song);
		rig.set_target_with_timeline(clip, false);
		rig.knobs.set_mod_region();
		rig.knobs.mod_button_action(kDelayMode, true);
		rig.knobs.mod_button_action(kDelayMode, false);
		uint32_t now = 1000;
		for (const auto& [role, delta] : {std::pair{EncoderRole::GoldLower, int8_t{1}},
		                                  std::pair{EncoderRole::GoldLower, int8_t{-1}},
		                                  std::pair{EncoderRole::GoldUpper, int8_t{1}},
		                                  std::pair{EncoderRole::GoldUpper, int8_t{-1}}}) {
			rig.knobs.tick(now, 5);
			DirectCtx c{now};
			(void)rig.knobs.handle(gold_turn(role, delta), c.ctx);
			now += 5;
		}
		rig.knobs.tick(now, 5);
		deluge::ui::widgets::core::LedFrame frame;
		rig.knobs.leds(frame);
		const deluge::ui::widgets::core::KnobIndicator* port = frame.indicator_or_null(deluge::ui::widgets::core::IndicatorId::Lower);
		expect(port).not_().to_be_null("the lower indicator");
		// Recorded from `View::setKnobIndicatorLevel` after the same four turns: the lower column centred, so it blinks.
		constexpr std::array<uint8_t, 4> kRecordedBlink = {255, 255, 0, 0};
		if (port != nullptr) {
			for (int32_t seg = 0; seg < 4; seg++) {
				expect(static_cast<int>(port->levels[seg])).to_equal(static_cast<int>(kRecordedBlink[seg]));
			}
		}
		currentSong = nullptr;
	});

	it("writes a kit row's knob as the recorded View::modEncoderAction turn does", _ {
		StubDisplay stub;
		display = &stub;
		AudioEngine::audioSampleTimer = 0;
		spec::SongFixture f = spec::make_kit_song();
		currentSong = f.song;
		auto* kit = static_cast<Kit*>(f.instrument);
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		NoteRow* row = f.clip->getNoteRowForDrum(drum);
		expect(row).not_().to_be_null("the drum's note row");
		if (row != nullptr) {
			seed_params(row->paramManager, 0);
			const std::vector<int32_t> before = param_snapshot(row->paramManager);
			f.clip->affectEntire = false;

			kit->selectedDrum = drum;
			KnobRig rig;
			rig.set_song(f.song);
			rig.set_target_with_timeline(f.clip, false);
			rig.knobs.set_mod_region();
			DirectCtx c{1000};
			(void)rig.knobs.handle(gold_turn(EncoderRole::GoldLower, 5), c.ctx);

			const std::vector<int32_t> after = param_snapshot(row->paramManager);
			// Recorded from `View::modEncoderAction` on a kit row, the same turn.
			expect(sole_change(before, after)).to_equal("11=167772160");
		}
		display = nullptr;
		currentSong = nullptr;
	});

	it("keeps a kit chain's stutter through its reverb button, and ends it on another, as main's kit chain does", _ {
		StubDisplay stub;
		display = &stub;
		spec::SongFixture f = spec::make_kit_song();
		currentSong = f.song;
		f.clip->affectEntire = true;
		KnobRig rig;
		rig.set_song(f.song);
		rig.set_target_with_timeline(f.clip, false);
		expect(std::holds_alternative<bind::ChainFocus>(rig.knobs.focus().owner)).to_be_true();
		currentUIMode = UI_MODE_NONE;
		(void)deluge::gui::shell_host::host_ports().controls_sink->stutter(rig.knobs.focus(), true);
		expect(isUIModeActive(UI_MODE_STUTTERING)).to_be_true();
		// main's `GlobalEffectableForClip::modButtonAction`: the reverb button leaves the stutter playing.
		rig.knobs.mod_button_action(4, true);
		rig.knobs.mod_button_action(4, false);
		expect(isUIModeActive(UI_MODE_STUTTERING)).to_be_true();
		// main's `GlobalEffectable::modButtonAction`: any other button ends it.
		rig.knobs.mod_button_action(3, true);
		expect(isUIModeActive(UI_MODE_STUTTERING)).to_be_false();
		rig.knobs.mod_button_action(3, false);
		currentUIMode = UI_MODE_NONE;
		display = nullptr;
		currentSong = nullptr;
	});

	it("writes an audio track's knob as the recorded View::modEncoderAction turn does", _ {
		StubDisplay stub;
		display = &stub;
		AudioEngine::audioSampleTimer = 0;
		spec::AudioSongFixture song = spec::make_audio_clip_song();
		seed_params(song.clip->paramManager, 0);
		const std::vector<int32_t> before = param_snapshot(song.clip->paramManager);

		currentSong = song.song;
		KnobRig rig;
		rig.set_song(song.song);
		rig.set_target_with_timeline(song.clip, false);
		rig.knobs.set_mod_region();
		DirectCtx c{1000};
		(void)rig.knobs.handle(gold_turn(EncoderRole::GoldLower, 5), c.ctx);

		const std::vector<int32_t> after = param_snapshot(song.clip->paramManager);
		// Recorded from `View::modEncoderAction` on an audio clip, the same turn.
		expect(sole_change(before, after)).to_equal("31=167772160");
		display = nullptr;
		currentSong = nullptr;
	});

	it("turns the song's compressor as the recorded View turn does, its view pushed in and cycled", _ {
		StubDisplay stub;
		display = &stub;
		spec::SongFixture song = spec::make_synth_song();
		const auto push = [](ButtonId knob) {
			UiEvent e{};
			e.kind = UiEvent::Kind::EncoderButton;
			e.button = knob;
			e.on = true;
			return e;
		};

		// The compressor button, its upper knob pushed in to edit the compressor, the lower pushed twice to reach the
		// release, then the lower turned.
		currentSong = song.song;
		song.song->persistentView.affectEntire = true;
		(void)deluge::edits::modKnobModeFor(deluge::project::SongAddress{}, 4);
		KnobRig rig;
		rig.set_song(song.song);
		rig.set_target_with_timeline(song.song, false);
		rig.knobs.set_mod_region();
		DirectCtx c{1000};
		(void)rig.knobs.handle(push(ButtonId::ModEnc1), c.ctx);
		for (int32_t n = 0; n < 2; n++) {
			(void)rig.knobs.handle(push(ButtonId::ModEnc0), c.ctx);
		}
		(void)rig.knobs.handle(gold_turn(EncoderRole::GoldLower, 5), c.ctx);

		// Recorded from the same gestures through `View::modEncoderButtonAction` and `View::modEncoderAction`: the
		// view's compressor focus is editing, on its third parameter (the release), and the turn left the release at
		// 167772160.
		expect(rig.knobs.focus().comp.editing).to_be_true();
		expect(rig.knobs.focus().comp.param).to_equal(uint8_t{2});
		expect(song.song->live.master_channel.binding().effects().compressor.release).to_equal(167772160);
		const auto* recorded = song.song->project.master_effects.effects.as<deluge::effects::DelugeEffects>();
		expect(recorded).not_().to_be_null("the master channel's effects");
		if (recorded != nullptr) {
			expect(recorded->compressor.release).to_equal(167772160);
		}
		display = nullptr;
		currentSong = nullptr;
	});

	it("does nothing on a CV track, whose knobs have no modes, as the recorded view does", _ {
		StubDisplay stub;
		display = &stub;
		spec::SongFixture f = spec::make_cv_song();
		currentSong = f.song;
		KnobRig rig;
		rig.set_song(f.song);
		rig.set_target_with_timeline(f.clip, false);
		// Recorded from main's `ModControllable::getModKnobMode` on a CV output, which has no mode field: -1.
		expect(rig.knobs.mode()).to_equal(-1);
		DirectCtx c{1000};
		const Result r = rig.knobs.handle(gold_turn(EncoderRole::GoldLower, 5), c.ctx);
		expect(r.handled).to_be_true();
		expect(r.actions.size()).to_equal(size_t(0));
		display = nullptr;
		currentSong = nullptr;
	});

	it("turns an arranger row with no clip from its backed-up value, as the recorded view does", _ {
		StubDisplay stub;
		display = &stub;
		AudioEngine::audioSampleTimer = 0;
		spec::SongFixture f = spec::make_synth_song();
		currentSong = f.song;
		const auto row = [&](const char* name) {
			ParamManagerForTimeline params;
			auto* synth = static_cast<SoundInstrument*>(StorageManager::createNewInstrument(OutputType::SYNTH, &params));
			synth->dirPath = "SYNTHS";
			synth->name = name;
			synth->setupAsDefaultSynth(&params);
			f.song->live.param_backup.backUp(*synth, nullptr, &params, true);
			f.song->addOutput(synth);
			ParamManager* backed = f.song->live.param_backup.getPreferablyWithClip(*synth, nullptr);
			seed_params(*backed, 1 << 28);
			return std::pair{synth, backed};
		};
		const auto [port_synth, port_params] = row("ROW");
		const std::vector<int32_t> port_before = param_snapshot(*port_params);

		KnobRig rig;
		rig.set_song(f.song);
		rig.set_target_without_timeline(port_synth, port_params);
		rig.knobs.set_mod_region();
		DirectCtx c{1000};
		(void)rig.knobs.handle(gold_turn(EncoderRole::GoldLower, 5), c.ctx);

		const std::vector<int32_t> port_after = param_snapshot(*port_params);
		// Recorded from `View::modEncoderAction` on an arranger row with no clip: the turn from its backed-up value.
		expect(sole_change(port_before, port_after)).to_equal("11=436207616");
		display = nullptr;
		currentSong = nullptr;
	});

	it("marks the instrument edited where the recorded view's turn and push do, and only there", _ {
		StubDisplay stub;
		display = &stub;
		AudioEngine::audioSampleTimer = 0;
		spec::SongFixture f = spec::make_synth_song();
		currentSong = f.song;
		auto* synth = static_cast<SoundInstrument*>(f.instrument);
		KnobRig rig;
		rig.set_song(f.song);
		rig.set_target_with_timeline(f.clip, false);
		DirectCtx c{1000};
		const auto push = [](int32_t knob, bool on) {
			UiEvent e{};
			e.kind = UiEvent::Kind::EncoderButton;
			e.button = knob == 0 ? ButtonId::ModEnc0 : ButtonId::ModEnc1;
			e.on = on;
			return e;
		};
		struct Push {
			int32_t mode;
			int32_t knob;
			bool edits; ///< recorded from `View::modEncoderButtonAction`'s `instrumentBeenEdited`
		};
		// The filter knob's cycle, the filter mode, the delay's ping-pong, the reverb preset.
		for (const auto [mode, knob, edits] : {Push{1, 1, false}, Push{1, 0, true}, Push{3, 1, true}, Push{4, 0, false}}) {
			rig.knobs.mod_button_action(mode, true);
			rig.knobs.mod_button_action(mode, false);
			synth->editedByUser = false;
			(void)rig.knobs.handle(push(knob, true), c.ctx);
			(void)rig.knobs.handle(push(knob, false), c.ctx);
			expect(synth->editedByUser).to_equal(edits);
		}
		// A turn marks it, whatever it moved.
		synth->editedByUser = false;
		(void)rig.knobs.handle(gold_turn(EncoderRole::GoldLower, 3), c.ctx);
		expect(synth->editedByUser).to_be_true();
		display = nullptr;
		currentSong = nullptr;
	});

	it("leaves a MIDI knob's CC choice unentered outside a clip minder, so the next mod button is taken", _ {
		StubDisplay stub;
		display = &stub;
		spec::SongFixture f = spec::make_midi_song();
		currentSong = f.song;
		KnobRig rig;
		rig.set_song(f.song);
		rig.set_target_with_timeline(f.clip, false);
		DirectCtx c{1000};
		UiEvent push{};
		push.kind = UiEvent::Kind::EncoderButton;
		push.button = ButtonId::ModEnc0;
		push.on = true;
		(void)rig.knobs.handle(push, c.ctx); // legacy's CC choice needs a clip minder on screen, which a spec has not
		rig.knobs.mod_button_action(3, true);
		rig.knobs.mod_button_action(3, false);
		expect(rig.knobs.mode()).to_equal(3);
		display = nullptr;
		currentSong = nullptr;
	});

	it("repaints every pad when a synth's note tails change with a turn", _ {
		StubDisplay stub;
		display = &stub;
		spec::SongFixture f = spec::make_synth_song();
		currentSong = f.song;
		auto* sound = static_cast<SoundInstrument*>(f.instrument);
		// `modKnobModeFor(..., 2)` puts the knobs on the third mod button, whose lower knob is envelope 1's release;
		// with sustain at its minimum, tails hold only while the decay is above the release.
		(void)deluge::edits::modKnobModeFor(deluge::project::TrackAddress{*sound->id()}, 2);
		const auto env_param = [&](int32_t id) {
			return deluge::project::ParamAddress{
			    .owner = deluge::project::OwnerId::instrument(*f.instrument->id()),
			    .space = deluge::project::ParamSpace::ENGINE,
			    .id = deluge::bridge::paramIdOf(Kind::PATCHED, id),
			    .clip_id = f.clip->id()};
		};
		const auto set_value = [&](int32_t id, int32_t value) {
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			ModelStackWithAutoParam* stack = deluge::bridge::paramStackAt(*f.song, env_param(id), memory);
			expect(stack).not_().to_be_null("the param's stack");
			if (stack != nullptr) {
				stack->autoParam->setCurrentValueWithNoReversionOrRecording(stack, value);
			}
		};
		set_value(deluge::modulation::params::LOCAL_ENV_0_SUSTAIN, std::numeric_limits<int32_t>::min());
		set_value(deluge::modulation::params::LOCAL_ENV_0_DECAY, -(1 << 29));
		set_value(deluge::modulation::params::LOCAL_ENV_0_RELEASE, std::numeric_limits<int32_t>::min());
		const auto tails = [&] {
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			ModelStackWithThreeMainThings* stack =
			    deluge::bridge::knobOwnerStackAt(*f.song, focus_of(*f.song, f.clip), memory);
			return stack != nullptr && sound->allowNoteTails(stack->addSoundFlags());
		};
		expect(tails()).to_be_true();

		KnobRig rig;
		rig.set_song(f.song);
		rig.set_target_with_timeline(f.clip, false);
		rig.knobs.set_mod_region();
		DirectCtx c{1000};
		// Turns the release up until the tails go, and answers how many turns asked the shell to repaint every pad.
		const auto turn_until_no_tails = [&] {
			int32_t repaints = 0;
			for (int32_t i = 0; i < 60 && tails(); i++) {
				const Result r = rig.knobs.handle(gold_turn(EncoderRole::GoldLower, 1), c.ctx);
				for (const auto& a : r.actions) {
					repaints += a.kind == ShellAction::Kind::RepaintAll ? 1 : 0;
				}
			}
			return repaints;
		};

		// Over a timeline view, the root view's pads under a menu are legacy's to draw: they are told too.
		rig.knobs.set_view_state({.root_is_timeline_view = true});
		(void)deluge::base::notify::dirty().drain(deluge::base::notify::DirtyConsumer::Legacy);
		expect(turn_until_no_tails()).to_equal(1);
		expect(tails()).to_be_false();
		expect(deluge::base::notify::dirty().drain(deluge::base::notify::DirtyConsumer::Legacy).has(deluge::base::notify::Dirty::GridMain))
		    .to_be_true();

		// Over any other root, only the shell's pads repaint.
		set_value(deluge::modulation::params::LOCAL_ENV_0_RELEASE, std::numeric_limits<int32_t>::min());
		expect(tails()).to_be_true();
		rig.knobs.set_view_state({.root_is_timeline_view = false});
		(void)deluge::base::notify::dirty().drain(deluge::base::notify::DirtyConsumer::Legacy);
		expect(turn_until_no_tails()).to_equal(1);
		expect(tails()).to_be_false();
		expect(deluge::base::notify::dirty().drain(deluge::base::notify::DirtyConsumer::Legacy).has(deluge::base::notify::Dirty::GridMain))
		    .to_be_false();
		display = nullptr;
		currentSong = nullptr;
	});

	it("reads a synth's note tails from the clip a turn is in, not the clip its synth plays", _ {
		spec::SongFixture f = spec::make_synth_song();
		currentSong = f.song;
		auto* synth = static_cast<SoundInstrument*>(f.instrument);
		InstrumentClip* other = spec::detail::make_session_clip(f.song);
		ParamManagerForTimeline params;
		expect(params.cloneParamCollectionsFrom(&f.clip->paramManager, false)).to_equal(Error::NONE);
		other->setAudioInstrument(synth, f.song, true, &params);
		const auto set_value = [&](const Clip& clip, int32_t id, int32_t value) {
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			ModelStackWithAutoParam* stack = deluge::bridge::paramStackAt(
			    *f.song,
			    deluge::project::ParamAddress{.owner = deluge::project::OwnerId::instrument(*synth->id()),
			                                  .space = deluge::project::ParamSpace::ENGINE,
			                                  .id = deluge::bridge::paramIdOf(Kind::PATCHED, id),
			                                  .clip_id = clip.id()},
			    memory);
			expect(stack).not_().to_be_null("the param's stack");
			if (stack != nullptr) {
				stack->autoParam->setCurrentValueWithNoReversionOrRecording(stack, value);
			}
		};
		// Both clips hold no sustain and a decay above the release: tails. The other clip's release then goes above
		// its decay: no tails there.
		for (const Clip* clip : {static_cast<const Clip*>(f.clip), static_cast<const Clip*>(other)}) {
			set_value(*clip, deluge::modulation::params::LOCAL_ENV_0_SUSTAIN, std::numeric_limits<int32_t>::min());
			set_value(*clip, deluge::modulation::params::LOCAL_ENV_0_DECAY, -(1 << 29));
			set_value(*clip, deluge::modulation::params::LOCAL_ENV_0_RELEASE, std::numeric_limits<int32_t>::min());
		}
		set_value(*other, deluge::modulation::params::LOCAL_ENV_0_RELEASE, std::numeric_limits<int32_t>::max());

		// The first clip plays, and the other does not: a turn with no clip reads the playing one.
		f.clip->activeIfNoSolo = true;
		other->activeIfNoSolo = false;
		const deluge::project::SlotAddress slot{deluge::project::SlotAddress::Kind::OUTPUT, *synth->id()};
		const auto& facts = *deluge::gui::shell_host::host_ports().controls;
		expect(facts.allows_note_tails(slot, deluge::project::ClipAddress{f.clip->id()})).to_be_true();
		expect(facts.allows_note_tails(slot, deluge::project::ClipAddress{other->id()})).to_be_false();
		expect(facts.allows_note_tails(slot, std::nullopt)).to_be_true();
		currentSong = nullptr;
	});

	it("leaves a MIDI CC select running when a mod button is pressed, and ends it on the release", _ {
		StubDisplay stub;
		display = &stub;
		deluge::ui::scenes::spec::UiStackGuard ui_guard;
		setRootUILowLevel(&instrumentClipView); // the select starts only from a clip minder
		spec::SongFixture f = spec::make_midi_song();
		currentSong = f.song;
		KnobRig rig;
		rig.set_song(f.song);
		rig.set_target_with_timeline(f.clip, false);
		DirectCtx c{1000};
		UiEvent push{};
		push.kind = UiEvent::Kind::EncoderButton;
		push.button = ButtonId::ModEnc0;
		push.on = true;
		(void)rig.knobs.handle(push, c.ctx);
		expect(currentUIMode).to_equal(UI_MODE_SELECTING_MIDI_CC);
		drain_notices();

		rig.knobs.mod_button_action(3, true);
		expect(currentUIMode).to_equal(UI_MODE_SELECTING_MIDI_CC);
		expect(deluge::base::notify::notices().size()).to_equal(size_t{0});
		expect(rig.knobs.mode()).to_equal(0);

		rig.knobs.mod_button_action(3, false);
		expect(currentUIMode).to_equal(UI_MODE_NONE);
		expect(deluge::base::notify::notices().size()).to_equal(size_t{1});
		deluge::base::notify::Notice raised;
		expect(deluge::base::notify::notices().pop(raised)).to_be_true();
		expect(raised.kind).to_equal(deluge::base::notify::Notice::Kind::CancelPopup);
		currentUIMode = UI_MODE_NONE;
		display = nullptr;
		currentSong = nullptr;
	});

	it("selects the CC its track's record names for the knob and mode, and ends on a release", _ {
		StubDisplay stub;
		display = &stub;
		deluge::ui::scenes::spec::UiStackGuard ui_guard;
		setRootUILowLevel(&instrumentClipView);
		spec::SongFixture f = spec::make_midi_song();
		currentSong = f.song;
		auto* instrument = static_cast<MIDIInstrument*>(f.instrument);
		// The record names the CC; the engine's assignment agrees with it for every knob of every mode.
		(void)deluge::edits::modKnobCCFor(deluge::project::TrackAddress{*instrument->id()}, 1,
		                                  {.kind = deluge::project::ModKnobCc::Kind::NUMBER, .number = 74});
		const deluge::project::DocOutput* record = f.song->project.output_structure.find(*instrument->id());
		expect(record).not_().to_be_null("the track's record");
		if (record != nullptr) {
			for (size_t at = 0; at < record->mod_knob_ccs.size(); at++) {
				expect(static_cast<int32_t>(instrument->modKnobCCAssignments[at]))
				    .to_equal(static_cast<int32_t>(MIDIInstrument::engineCcOf(record->mod_knob_ccs[at])));
			}
		}
		const deluge::ui::scenes::bind::KnobFocus focus = focus_of(*f.song, f.clip);
		auto& sink = *deluge::gui::shell_host::host_ports().controls_sink;
		expect(sink.midi_cc_select(focus, 1, true)).to_be_true();
		expect(currentUIMode).to_equal(UI_MODE_SELECTING_MIDI_CC);
		expect(InstrumentClipMinder::editingMIDICCForWhichModKnob).to_equal(1);
		// A select already running takes no second press.
		expect(sink.midi_cc_select(focus, 0, true)).to_be_false();
		expect(InstrumentClipMinder::editingMIDICCForWhichModKnob).to_equal(1);
		expect(sink.midi_cc_select(focus, 1, false)).to_be_false();
		expect(currentUIMode).to_equal(UI_MODE_NONE);
		// A release with no select running leaves a menu's mode alone.
		currentUIMode = UI_MODE_HORIZONTAL_SCROLL;
		expect(sink.midi_cc_select(focus, 1, false)).to_be_false();
		expect(currentUIMode).to_equal(UI_MODE_HORIZONTAL_SCROLL);
		currentUIMode = UI_MODE_NONE;
		display = nullptr;
		currentSong = nullptr;
	});

	it("draws the CC a knob sends and whether its lane is automated, for a CC, bend, aftertouch and the Y axis", _ {
		StubDisplay stub;
		display = &stub;
		deluge::ui::scenes::spec::UiStackGuard ui_guard;
		setRootUILowLevel(&instrumentClipView);
		using Cc = deluge::project::ModKnobCc;
		struct Lane {
			uint8_t mode;
			uint8_t knob;
			Cc cc;
		};
		const Lane lanes[] = {
		    {0, 0, {.kind = Cc::Kind::NUMBER, .number = 74}},
		    {0, 1, {.kind = Cc::Kind::BEND}},
		    {1, 0, {.kind = Cc::Kind::AFTERTOUCH}},
		    {1, 1, {.kind = Cc::Kind::NUMBER, .number = Cc::kYAxisNumber}},
		};
		auto& sink = *deluge::gui::shell_host::host_ports().controls_sink;
		// One song per lane, with only that lane automated: a lane read in place of another reads no automation.
		for (size_t automated = 0; automated < std::size(lanes); automated++) {
			spec::SongFixture f = spec::make_midi_song();
			currentSong = f.song;
			f.song->setCurrentClip(f.clip);
			auto* instrument = static_cast<MIDIInstrument*>(f.instrument);
			for (const Lane& lane : lanes) {
				(void)deluge::edits::modKnobCCFor(deluge::project::TrackAddress{*instrument->id()},
				                                  static_cast<uint8_t>(lane.mode * 2 + lane.knob), lane.cc);
			}
			{
				const Lane& lane = lanes[automated];
				(void)deluge::edits::modKnobModeFor(deluge::project::TrackAddress{*instrument->id()}, lane.mode);
				KnobRig rig;
				rig.set_song(f.song);
				rig.set_target_with_timeline(f.clip, false);
				rig.knobs.set_mod_region(0, 24);
				DirectCtx c{1000};
				(void)rig.knobs.handle(gold_turn(lane.knob == 0 ? EncoderRole::GoldLower : EncoderRole::GoldUpper, 5),
				                       c.ctx);
			}
			for (size_t pressed = 0; pressed < std::size(lanes); pressed++) {
				const Lane& lane = lanes[pressed];
				(void)deluge::edits::modKnobModeFor(deluge::project::TrackAddress{*instrument->id()}, lane.mode);
				const deluge::ui::scenes::bind::KnobFocus focus = focus_of(*f.song, f.clip);
				etl::string<30> name;
				instrument->appendCCName(name, MIDIInstrument::engineCcOf(lane.cc));
				std::string expected = name.c_str();
				if (pressed == automated) {
					expected += "\n(automated)";
				}
				stub.last_popup.clear();
				expect(sink.midi_cc_select(focus, lane.knob, true)).to_be_true();
				expect(stub.last_popup).to_equal(expected);
				expect(sink.midi_cc_select(focus, lane.knob, false)).to_be_false();
			}
		}
		currentUIMode = UI_MODE_NONE;
		display = nullptr;
		currentSong = nullptr;
	});

	it("draws a CC whose lane is not made, and a knob that sends nothing, unautomated, and makes no lane", _ {
		StubDisplay stub;
		display = &stub;
		deluge::ui::scenes::spec::UiStackGuard ui_guard;
		setRootUILowLevel(&instrumentClipView);
		using Cc = deluge::project::ModKnobCc;
		spec::SongFixture f = spec::make_midi_song();
		currentSong = f.song;
		f.song->setCurrentClip(f.clip);
		auto* instrument = static_cast<MIDIInstrument*>(f.instrument);
		const deluge::project::TrackAddress track{*instrument->id()};
		(void)deluge::edits::modKnobCCFor(track, 4, {.kind = Cc::Kind::NUMBER, .number = 75});
		(void)deluge::edits::modKnobCCFor(track, 5, {});
		(void)deluge::edits::modKnobModeFor(track, 2);
		MIDIParamCollection* ccs = f.clip->paramManager.getMIDIParamCollection();
		expect(ccs).not_().to_be_null("the clip's CC params");
		if (ccs != nullptr) {
			expect(ccs->params.getParamFromCC(75)).to_be_null("CC 75's param");
		}
		auto& sink = *deluge::gui::shell_host::host_ports().controls_sink;
		const deluge::ui::scenes::bind::KnobFocus focus = focus_of(*f.song, f.clip);
		for (const auto& [knob, cc] : {std::pair{0, Cc{.kind = Cc::Kind::NUMBER, .number = 75}}, std::pair{1, Cc{}}}) {
			etl::string<30> name;
			instrument->appendCCName(name, MIDIInstrument::engineCcOf(cc));
			stub.last_popup.clear();
			expect(sink.midi_cc_select(focus, knob, true)).to_be_true();
			expect(stub.last_popup).to_equal(std::string{name.c_str()});
			expect(sink.midi_cc_select(focus, knob, false)).to_be_false();
		}
		if (ccs != nullptr) {
			expect(ccs->params.getParamFromCC(75)).to_be_null("CC 75's param");
		}

		// A clip the song does not hold starts nothing.
		deluge::ui::scenes::bind::KnobFocus elsewhere = focus;
		std::get<deluge::ui::scenes::bind::MidiFocus>(elsewhere.owner).clip = deluge::project::ClipAddress{f.clip->id() + 1000};
		expect(sink.midi_cc_select(elsewhere, 0, true)).to_be_false();
		expect(currentUIMode).to_equal(UI_MODE_NONE);
		currentUIMode = UI_MODE_NONE;
		display = nullptr;
		currentSong = nullptr;
	});

	it("ends a MIDI CC choice on its knob's release with LEARN held, or with another mode held over it", _ {
		// `View::modEncoderButtonAction` refuses a LEARN-held press only (#5290): a release ends what its press began
		StubDisplay stub;
		display = &stub;
		deluge::ui::scenes::spec::UiStackGuard ui_guard;
		setRootUILowLevel(&instrumentClipView);
		spec::SongFixture f = spec::make_midi_song();
		currentSong = f.song;
		{
			KnobRig rig;
			rig.set_song(f.song);
			(void)rig.stack.push({SceneKind::FirstApp});
			rig.set_target_with_timeline(f.clip, false);
			CountingPresentation out;
			rig.knobs.push(1, true, {}, out);
			expect(currentUIMode).to_equal(UI_MODE_SELECTING_MIDI_CC);
			rig.knobs.push(1, false, {.learn = true}, out);
			expect(currentUIMode).to_equal(UI_MODE_NONE);
		}
		// A held horizontal encoder over the choice: the release ends the choice and keeps the encoder's mode
		auto& sink = *deluge::gui::shell_host::host_ports().controls_sink;
		const deluge::ui::scenes::bind::KnobFocus focus = focus_of(*f.song, f.clip);
		expect(sink.midi_cc_select(focus, 1, true)).to_be_true();
		enterUIMode(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON);
		expect(sink.midi_cc_select(focus, 1, false)).to_be_false();
		expect(currentUIMode).to_equal(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON);
		currentUIMode = UI_MODE_NONE;
		display = nullptr;
		currentSong = nullptr;
	});

	it("records a stutter-rate turn while recording, as it does any param's", _ {
		StubDisplay stub;
		display = &stub;
		AudioEngine::audioSampleTimer = 0;
		spec::SongFixture f = spec::make_synth_song();
		auto* sound = static_cast<SoundInstrument*>(f.instrument);
		(void)deluge::edits::modKnobModeFor(deluge::project::TrackAddress{*sound->id()}, 6); // the stutter mode: its upper knob is the stutter rate
		const deluge::project::ParamAddress stutter{
		    .owner = deluge::project::OwnerId::instrument(*f.instrument->id()),
		    .space = deluge::project::ParamSpace::ENGINE,
		    .id = deluge::bridge::paramIdOf(Kind::UNPATCHED_SOUND, deluge::modulation::params::UNPATCHED_STUTTER_RATE),
		    .clip_id = f.clip->id()};

		KnobRig rig;
		rig.set_song(f.song);
		rig.set_target_with_timeline(f.clip, false);
		rig.knobs.set_mod_region();
		const auto recording = playbackHandler.recording;
		const auto state = playbackHandler.playbackState;
		const bool armed = f.clip->armedForRecording;
		playbackHandler.recording = RecordingMode::NORMAL;
		playbackHandler.playbackState = PLAYBACK_CLOCK_INTERNAL_ACTIVE;
		f.clip->armedForRecording = true;
		f.song->setCurrentClip(f.clip);
		DirectCtx c{1000};
		(void)rig.knobs.handle(gold_turn(EncoderRole::GoldUpper, 5), c.ctx);
		deluge::modulation::applyRecordEvents(*f.song); // the render's next block writes the recorded value
		deluge::modulation::commitBeforeJob(*f.song);   // and the next job's commit carries it into the model
		playbackHandler.recording = recording;
		playbackHandler.playbackState = state;
		f.clip->armedForRecording = armed;

		const deluge::project::DocAutoParam* record = f.song->project.resolveParam(stutter);
		expect(record).not_().to_be_null();
		if (record != nullptr) {
			expect(record->nodes.empty()).to_be_false();
		}
	});

	it("writes a region turn as a region in the record", _ {
		StubDisplay stub;
		display = &stub;
		spec::SongFixture f = spec::make_synth_song();
		(void)deluge::edits::modKnobModeFor(deluge::project::TrackAddress{*f.instrument->id()}, 1); // the filter mode: upper knob is LPF frequency
		const deluge::project::ParamAddress lpf{
		    .owner = deluge::project::OwnerId::instrument(*f.instrument->id()),
		    .space = deluge::project::ParamSpace::ENGINE,
		    .id = deluge::bridge::paramIdOf(Kind::PATCHED, deluge::modulation::params::LOCAL_LPF_FREQ),
		    .clip_id = f.clip->id()};
		KnobRig rig;
		rig.set_song(f.song);
		rig.set_target_with_timeline(f.clip, false);
		rig.knobs.set_mod_region(24, 24);
		DirectCtx c{1000};
		(void)rig.knobs.handle(gold_turn(EncoderRole::GoldUpper, 5), c.ctx);
		const deluge::project::DocAutoParam* record = f.song->project.resolveParam(lpf);
		expect(record).not_().to_be_null();
		if (record != nullptr) {
			expect(record->nodes.empty()).to_be_false();
		}
		const bool at_region = record != nullptr && std::ranges::any_of(record->nodes, [](const auto& node) {
			return node.pos == 24;
		});
		expect(at_region).to_be_true();
		display = nullptr;
	});

	it("writes a knob on a cable and on a MIDI clip's CC", _ {
		StubDisplay stub;
		display = &stub;
		spec::SongFixture synth = spec::make_synth_song();
		(void)deluge::edits::modKnobModeFor(deluge::project::TrackAddress{*synth.instrument->id()}, 5); // lower knob: LFO1 -> pitch
		deluge::synth::CableTarget pitch = deluge::synth::CableTarget::param(deluge::modulation::params::LOCAL_PITCH_ADJUST);
		const deluge::project::ParamAddress vibrato{
		    .owner = deluge::project::OwnerId::instrument(*synth.instrument->id()),
		    .space = deluge::project::ParamSpace::ENGINE,
		    .id = deluge::bridge::cable_depth(PatchSource::LFO_GLOBAL_1, pitch),
		    .clip_id = synth.clip->id()};
		const auto value_at = [](Song& song, const deluge::project::ParamAddress& address) -> std::optional<int32_t> {
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			ModelStackWithAutoParam* stack = deluge::bridge::paramStackAt(song, address, memory);
			return stack != nullptr ? std::optional{stack->autoParam->getCurrentValue()} : std::nullopt;
		};
		const std::optional<int32_t> vibrato_before = value_at(*synth.song, vibrato);
		{
			KnobRig rig;
			rig.set_song(synth.song);
			rig.set_target_with_timeline(synth.clip, false);
			rig.knobs.set_mod_region();
			DirectCtx c{1000};
			(void)rig.knobs.handle(gold_turn(EncoderRole::GoldLower, 5), c.ctx);
		}
		const std::optional<int32_t> vibrato_after = value_at(*synth.song, vibrato);
		expect(vibrato_after.has_value()).to_be_true();
		expect(vibrato_after).not_().to_equal(vibrato_before);

		spec::SongFixture midi = spec::make_midi_song();
		auto* instrument = static_cast<MIDIInstrument*>(midi.instrument);
		(void)deluge::edits::modKnobModeFor(deluge::project::TrackAddress{*instrument->id()}, 0);
		// Mode 0's lower knob sends CC 74, in the track's record.
		(void)deluge::edits::modKnobCCFor(deluge::project::TrackAddress{*instrument->id()}, 0,
		                                  {.kind = deluge::project::ModKnobCc::Kind::NUMBER, .number = 74});
		const deluge::project::ParamAddress cc74{.owner = deluge::project::OwnerId::clip(midi.clip->id()),
		                                         .space = deluge::project::ParamSpace::MIDI_CC,
		                                         .id = deluge::plugin::ParamId{74}};
		const std::optional<int32_t> cc_before = value_at(*midi.song, cc74);
		{
			KnobRig rig;
			rig.set_song(midi.song);
			rig.set_target_with_timeline(midi.clip, false);
			rig.knobs.set_mod_region();
			DirectCtx c{1000};
			(void)rig.knobs.handle(gold_turn(EncoderRole::GoldLower, 5), c.ctx);
		}
		const std::optional<int32_t> cc_after = value_at(*midi.song, cc74);
		expect(cc_after.has_value()).to_be_true();
		expect(cc_after).not_().to_equal(cc_before);
		display = nullptr;
	});

	it("deletes the knob's automation on a shift+push, as one undo step", _ {
		StubDisplay stub;
		display = &stub;
		spec::SongFixture f = spec::make_synth_song();
		currentSong = f.song;
		(void)deluge::edits::modKnobModeFor(deluge::project::TrackAddress{*f.instrument->id()}, 0); // lower knob: pan
		const deluge::project::ParamAddress pan{
		    .owner = deluge::project::OwnerId::instrument(*f.instrument->id()),
		    .space = deluge::project::ParamSpace::ENGINE,
		    .id = deluge::bridge::paramIdOf(Kind::PATCHED, deluge::modulation::params::LOCAL_PAN),
		    .clip_id = f.clip->id()};
		KnobRig rig;
		rig.set_song(f.song);
		rig.set_target_with_timeline(f.clip, false);
		rig.knobs.set_mod_region(0, 24);
		DirectCtx c{1000};
		(void)rig.knobs.handle(gold_turn(EncoderRole::GoldLower, 5), c.ctx);
		expect(f.song->project.resolveParam(pan)->nodes.empty()).to_be_false();

		changeRootUI(&sessionView); // an undo step opens only with a view as the current UI
		actionLogger.deleteAllLogs();
		UiEvent push{};
		push.kind = UiEvent::Kind::EncoderButton;
		push.button = ButtonId::ModEnc0;
		push.on = true;
		struct ShiftCtx {
			deluge::ui::shell::ButtonSet held{};
			deluge::ui::widgets::core::PadMask pads{};
			UiContext ctx;
			ShiftCtx() : ctx{held, pads, true, false, 0, deluge_profile()} {}
		} shifted;
		(void)rig.knobs.handle(push, shifted.ctx);
		expect(f.song->project.resolveParam(pan)->nodes.empty()).to_be_true();
		Action* action = actionLogger.firstAction[BEFORE];
		expect(action).not_().to_be_null();
		if (action != nullptr) {
			expect(action->nextAction).to_be_null();
		}
		expect(actionLogger.revert(BEFORE, false, false)).to_be_true();
		expect(f.song->project.resolveParam(pan)->nodes.empty()).to_be_false();
		actionLogger.deleteAllLogs();
		nullifyUIs();
		display = nullptr;
		currentSong = nullptr;
	});

	it("shows neither a turn nor a delete the app refuses as done", _ {
		StubDisplay stub;
		display = &stub;
		spec::SongFixture f = spec::make_synth_song();
		(void)deluge::edits::modKnobModeFor(deluge::project::TrackAddress{*f.instrument->id()}, 1); // upper knob: LPF frequency
		KnobRig rig;
		rig.set_song(f.song);
		rig.set_target_with_timeline(f.clip, false);
		const std::vector<int32_t> before = param_snapshot(f.clip->paramManager);
		Song* const saved = currentSong;
		currentSong = nullptr; // the app's sink takes nothing for a song it does not play
		DirectCtx plain{1000};
		const Result turned = rig.knobs.handle(gold_turn(EncoderRole::GoldUpper, 5), plain.ctx);
		struct ShiftCtx {
			deluge::ui::shell::ButtonSet held{};
			deluge::ui::widgets::core::PadMask pads{};
			UiContext ctx;
			ShiftCtx() : ctx{held, pads, true, false, 2000, deluge_profile()} {}
		} shifted;
		UiEvent push{};
		push.kind = UiEvent::Kind::EncoderButton;
		push.button = ButtonId::ModEnc1;
		push.on = true;
		const Result deleted = rig.knobs.handle(push, shifted.ctx);
		currentSong = saved;
		expect(turned.handled).to_be_true();
		expect(turned.actions.size()).to_equal(size_t(0)); // no popup, no repaint
		expect(deleted.actions.size()).to_equal(size_t(0)); // no "automation deleted"
		expect(differences(before, param_snapshot(f.clip->paramManager))).to_equal(int32_t(0));
		display = nullptr;
	});

	it("writes a turn while recording the arrangement to the clip being recorded from the knob's", _ {
		StubDisplay stub;
		display = &stub;
		spec::SongFixture f = spec::make_synth_song();
		Song* const saved = currentSong;
		currentSong = f.song;
		auto* synth = static_cast<SoundInstrument*>(f.instrument);
		(void)deluge::edits::modKnobModeFor(deluge::project::TrackAddress{*synth->id()}, 1); // upper knob: LPF frequency
		// The clip the arrangement records into: the synth's active clip, recording from the session clip.
		InstrumentClip* recording = spec::detail::make_session_clip(f.song);
		ParamManagerForTimeline params;
		expect(params.cloneParamCollectionsFrom(&f.clip->paramManager, false)).to_equal(Error::NONE);
		recording->setAudioInstrument(synth, f.song, true, &params);
		recording->beingRecordedFromClip = f.clip;
		alignas(ModelStack) char memory[MODEL_STACK_MAX_SIZE];
		expect(synth->setActiveClip(setupModelStackWithSong(memory, f.song)->addTimelineCounter(recording),
		                            PgmChangeSend::NEVER))
		    .to_be_true();
		f.clip->activeIfNoSolo = true;
		const auto lpf_on = [&](const Clip& clip) {
			return deluge::project::ParamAddress{
			    .owner = deluge::project::OwnerId::instrument(*f.instrument->id()),
			    .space = deluge::project::ParamSpace::ENGINE,
			    .id = deluge::bridge::paramIdOf(Kind::PATCHED, deluge::modulation::params::LOCAL_LPF_FREQ),
			    .clip_id = clip.id()};
		};
		const auto value_at = [&](const deluge::project::ParamAddress& address) -> std::optional<int32_t> {
			alignas(ModelStackWithAutoParam) char at[MODEL_STACK_MAX_SIZE];
			ModelStackWithAutoParam* stack = deluge::bridge::paramStackAt(*f.song, address, at);
			return stack != nullptr ? std::optional{stack->autoParam->getCurrentValue()} : std::nullopt;
		};
		const std::optional<int32_t> session_before = value_at(lpf_on(*f.clip));
		const std::optional<int32_t> recording_before = value_at(lpf_on(*recording));

		const auto mode = playbackHandler.recording;
		const auto state = playbackHandler.playbackState;
		playbackHandler.recording = RecordingMode::ARRANGEMENT;
		playbackHandler.playbackState = PLAYBACK_CLOCK_INTERNAL_ACTIVE;
		KnobRig rig;
		rig.set_song(f.song);
		rig.set_target_with_timeline(f.clip, false);
		DirectCtx plain{1000};
		(void)rig.knobs.handle(gold_turn(EncoderRole::GoldUpper, 5), plain.ctx);
		playbackHandler.recording = mode;
		playbackHandler.playbackState = state;

		// The knobs keep the session clip; the write lands in the take.
		const auto* sound = std::get_if<deluge::ui::scenes::bind::SoundFocus>(&rig.knobs.focus().owner);
		expect(sound).not_().to_be_null();
		if (sound != nullptr) {
			expect(sound->clip.has_value()).to_be_true();
			if (sound->clip) {
				expect(sound->clip->clip_id).to_equal(f.clip->id());
			}
		}
		expect(value_at(lpf_on(*recording))).not_().to_equal(recording_before);
		expect(value_at(lpf_on(*f.clip))).to_equal(session_before);
		currentSong = saved;
		display = nullptr;
	});

	it("takes the non-existent-param arm without writing anything, as the recorded view does", _ {
		StubDisplay stub;
		display = &stub;

		spec::SongFixture f = spec::make_synth_song();
		InstrumentClip* clip = f.clip;
		seed_params(clip->paramManager, 0);
		const std::vector<int32_t> before = param_snapshot(clip->paramManager);

		// A `Sound` in mod-knob mode 0 has a real param on both knobs, so the way to reach the
		// non-existent arm without inventing an owner is the target with no owner
		// at all. Recorded from `View::modEncoderAction`: it skips the whole body there and writes nothing.
		KnobRig rig;
		rig.set_song(f.song);
		rig.set_target_with_timeline(nullptr, false);
		DirectCtx c{1000};
		(void)rig.knobs.handle(gold_turn(EncoderRole::GoldLower, 5), c.ctx);

		expect(std::holds_alternative<std::monostate>(rig.knobs.focus().owner)).to_be_true();
		expect(sole_change(before, param_snapshot(clip->paramManager))).to_equal("none");
	});

	it("points at what View::setActiveModControllableTimelineCounter points at", _ {
		StubDisplay stub;
		display = &stub;

		spec::SongFixture f = spec::make_synth_song();
		InstrumentClip* clip = f.clip;

		view.setActiveModControllableTimelineCounter(clip, false);

		KnobRig rig;
		rig.set_song(f.song);
		rig.set_target_with_timeline(clip, false);

		// The owner legacy's view names, as addresses: the synth, on its clip.
		expect(rig.knobs.focus().owner).to_equal(view.knobFocus().owner);
		const auto* sound = std::get_if<deluge::ui::scenes::bind::SoundFocus>(&rig.knobs.focus().owner);
		expect(sound).not_().to_be_null();
		if (sound != nullptr) {
			expect(sound->slot.id).to_equal(*f.instrument->id());
			expect(sound->clip.has_value()).to_be_true();
			if (sound->clip) {
				expect(sound->clip->clip_id).to_equal(clip->id());
			}
		}

		// The view's own knob mode agrees too.
		expect(rig.knobs.mode()).to_equal(view.knobMode());
		expect(rig.knobs.mode()).to_equal(1); // the synth fixture's default: the filter page
	});

	it("points at what View::setActiveModControllableWithoutTimelineCounter points at", _ {
		StubDisplay stub;
		display = &stub;

		spec::SongFixture f = spec::make_synth_song();
		// A bare owner with no timeline: the clip's own `SoundInstrument` (its `Sound`) plus the clip's param
		// manager -- the shape legacy's audition/arranger callers pass.
		const deluge::model::LaneOwner controllable = deluge::model::owner_of(*f.instrument);
		ParamManager* params = &f.clip->paramManager;

		view.setActiveModControllableWithoutTimelineCounter(controllable, params);

		KnobRig rig;
		rig.set_song(f.song);
		rig.set_target_without_timeline(controllable, params);

		expect(view.knobsClipId().has_value()).to_be_false(); // a bare controllable has no timeline counter
		expect(rig.knobs.focus().owner).to_equal(view.knobFocus().owner);
		const auto* sound = std::get_if<deluge::ui::scenes::bind::SoundFocus>(&rig.knobs.focus().owner);
		expect(sound).not_().to_be_null();
		if (sound != nullptr) {
			expect(sound->clip.has_value()).to_be_false(); // no clip: an arranger row's backed params
		}
	});

	it("records the mod region legacy records", _ {
		StubDisplay stub;
		display = &stub;

		spec::SongFixture f = spec::make_synth_song();
		view.setActiveModControllableTimelineCounter(f.clip, false);
		view.setModRegion(1234, 56, 7);

		KnobRig rig;
		rig.set_song(f.song);
		rig.set_target_with_timeline(f.clip, false);
		rig.knobs.set_mod_region(1234, 56);

		expect(rig.knobs.focus().region.pos).to_equal(view.modPos());
		expect(rig.knobs.focus().region.length).to_equal(view.modLength());

		// And the defaulted call is legacy's defaulted call (`views/view.h`).
		view.setModRegion();
		rig.knobs.set_mod_region();
		expect(rig.knobs.focus().region.pos).to_equal(view.modPos());
		expect(rig.knobs.focus().region.length).to_equal(view.modLength());
	});

	it("applies a lowered turn as it is, however fast the turns come", _ {
		// A host's lowered turn (`Shell::route_lowered_turn`) is accelerated already: a fast run of them moves the
		// model exactly as a slow run of plain turns at the multiplier's 1.0 floor does.
		StubDisplay stub;
		display = &stub;

		spec::SongFixture f = spec::make_synth_song();
		InstrumentClip* slow_clip = f.clip;
		InstrumentClip* lowered_clip = add_synth_clip(f.song, "1");
		seed_params(slow_clip->paramManager, 0);
		seed_params(lowered_clip->paramManager, 0);
		{
			KnobRig rig;
			rig.set_song(f.song);
			rig.set_target_with_timeline(slow_clip, false);
			rig.knobs.set_mod_region();
			for (uint32_t i = 0; i < 4; i++) {
				DirectCtx c{1000 + i * 1000};
				(void)rig.knobs.handle(gold_turn(EncoderRole::GoldLower, 1), c.ctx);
			}
		}
		{
			KnobRig rig;
			rig.set_song(f.song);
			rig.set_target_with_timeline(lowered_clip, false);
			rig.knobs.set_mod_region();
			for (uint32_t i = 0; i < 4; i++) {
				DirectCtx c{1000 + i * 10};
				UiEvent turn = gold_turn(EncoderRole::GoldLower, 1);
				turn.lowered = true;
				(void)rig.knobs.handle(turn, c.ctx);
			}
		}
		expect(differences(param_snapshot(slow_clip->paramManager), param_snapshot(lowered_clip->paramManager)))
		    .to_equal(0);
		display = nullptr;
	});

	it("times the acceleration on its own clock, not the tick's", _ {
		// Four turns 10 ms apart on the tick's clock but a second apart on the acceleration clock: no ramp, so the
		// knob moves as far as four slow turns do.
		StubDisplay stub;
		display = &stub;
		spec::SongFixture f = spec::make_synth_song();
		InstrumentClip* split_clip = f.clip;
		InstrumentClip* slow_clip = add_synth_clip(f.song, "1");
		seed_params(split_clip->paramManager, 0);
		seed_params(slow_clip->paramManager, 0);
		const auto turn_four = [&](InstrumentClip* clip, uint32_t tick_step, uint32_t clock_step) {
			KnobRig rig;
			rig.set_song(f.song);
			rig.set_target_with_timeline(clip, false);
			rig.knobs.set_mod_region();
			for (uint32_t i = 0; i < 4; i++) {
				DirectCtx c{1000 + i * tick_step};
				c.ctx.acceleration_clock_ms = 1000 + i * clock_step;
				(void)rig.knobs.handle(gold_turn(EncoderRole::GoldLower, 1), c.ctx);
			}
		};
		const std::vector<int32_t> untouched = param_snapshot(slow_clip->paramManager);
		turn_four(split_clip, 10, 1000);
		turn_four(slow_clip, 1000, 1000);
		expect(differences(untouched, param_snapshot(slow_clip->paramManager))).to_be_greater_than(0);
		expect(differences(param_snapshot(split_clip->paramManager), param_snapshot(slow_clip->paramManager)))
		    .to_equal(int32_t{0});
	});

	it("accelerates a repeated turn through InputLowering::calc_next_knob_speed", _ {
		// `calc_next_knob_speed` itself is pinned by
		// `tests/unit/ui/shell/input_lowering_spec.cpp` against the legacy arithmetic; what is checked
		// here is that `ModKnobs` is the consumer legacy's `interpretEncoders` (`hid/encoder_input.cpp`) is -- i.e.
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
			rig.set_song(f.song);
			rig.set_target_with_timeline(slow_clip, false);
			rig.knobs.set_mod_region();
			for (uint32_t i = 0; i < 4; i++) {
				DirectCtx c{1000 + i * 1000};
				(void)rig.knobs.handle(gold_turn(EncoderRole::GoldLower, 1), c.ctx);
			}
		}
		// Fast: the same four turns, 10ms apart, all in the same direction -- the ramp builds.
		{
			KnobRig rig;
			rig.set_song(f.song);
			rig.set_target_with_timeline(fast_clip, false);
			rig.knobs.set_mod_region();
			for (uint32_t i = 0; i < 4; i++) {
				DirectCtx c{1000 + i * 10};
				(void)rig.knobs.handle(gold_turn(EncoderRole::GoldLower, 1), c.ctx);
			}
		}

		const std::vector<int32_t> slow = param_snapshot(slow_clip->paramManager);
		const std::vector<int32_t> fast = param_snapshot(fast_clip->paramManager);
		expect(differences(slow, fast)).to_be_greater_than(0);

		// Both moved the same single param, and the accelerated side moved it further.
		int32_t moved = -1;
		for (size_t i = 0; i < slow.size(); i++) {
			if (slow[i] != fast[i]) {
				moved = static_cast<int32_t>(i);
			}
		}
		to_be_at_least(expect(moved), 0);
		// Guarded rather than indexed straight: a failure above must report as a failure, not as a
		// libstdc++ bounds assertion that takes the whole process (and every later example) down.
		if (moved >= 0) {
			expect(fast[static_cast<size_t>(moved)]).to_be_greater_than(slow[static_cast<size_t>(moved)]);
		}
	});

	it("shows the value popup and pushes it exactly once, however many turns arrive", _ {
		StubDisplay stub;
		display = &stub;

		spec::SongFixture f = spec::make_synth_song();
		KnobRig rig;
		rig.set_song(f.song);
		rig.set_target_with_timeline(f.clip, false);
		rig.knobs.set_mod_region();

		expect(rig.stack.depth()).to_equal(size_t(0));
		for (uint32_t i = 0; i < 5; i++) {
			DirectCtx c{2000 + i * 200};
			(void)rig.knobs.handle(gold_turn(EncoderRole::GoldLower, 1), c.ctx);
		}
		expect(rig.stack.depth()).to_equal(size_t(1));
		expect(rig.stack.id_at(0).kind).to_equal(overlay::kParamPopupSceneKind);
		expect(overlay::param_popup_scene().remaining_ms()).to_be_greater_than(0);
	});

	it("takes the notification slot when its popup shows, as legacy's knob popup replaces a notification", _ {
		StubDisplay stub;
		display = &stub;
		spec::SongFixture f = spec::make_synth_song();
		KnobRig rig;
		rig.set_song(f.song);
		rig.set_target_with_timeline(f.clip, false);
		rig.knobs.set_mod_region();
		deluge::base::notify::Notice n;
		while (deluge::base::notify::notices().pop(n)) {}
		DirectCtx c{60'000};
		(void)rig.knobs.handle(gold_turn(EncoderRole::GoldLower, 1), c.ctx);
		bool cancelled = false;
		while (deluge::base::notify::notices().pop(n)) {
			cancelled = cancelled || n.kind == deluge::base::notify::Notice::Kind::CancelNotification;
		}
		expect(cancelled).to_be_true();
	});

	it("suppresses the popup when the menu is focused on a horizontal page's same-param column",
	   _ {
		// `GuestKnobPresentation::suppress_value_popup` compares against
		// `MenuScene::focused_value()`, not `focused()`: legacy's same-param test forwards through a
		// horizontal page to the focused *column* (`Submenu::getParamKind`/`getParamIndex`,
		// `origin/main:src/deluge/gui/menu_item/submenu.cpp`, both gated on `shouldForwardButtons`),
		// and the page node itself has no `bind.param` to ever match against -- so comparing against
		// `focused()` could never suppress on a horizontal page, only on a full-page leaf. `GoldLower`
		// edits `LOCAL_LPF_RESONANCE` on this fixture (see
		// `mod_knobs_spec_nodes`'s own note), which `res_page`'s one column is bound to, so this is
		// `LOCAL_LPF_RESONANCE` on both sides of the comparison.
		StubDisplay stub;
		display = &stub;
		deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;

		spec::SongFixture f = spec::make_synth_song();
		KnobRig rig;
		rig.set_song(f.song);
		rig.set_target_with_timeline(f.clip, false);
		rig.knobs.set_mod_region();

		const menu::MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
		(void)menu::assign_node_ids(mod_knobs_spec_nodes::kRoots);
		menu_scene().open(mod_knobs_spec_nodes::res_page, ctx);
		expect(rig.stack.push(SceneId{SceneKind::FirstApp, menu::node_id(mod_knobs_spec_nodes::res_page)}))
		    .to_be_true();
		expect(rig.stack.depth()).to_equal(size_t(1));
		expect(menu_scene().focused_value()).to_equal(&mod_knobs_spec_nodes::res_col);

		for (uint32_t i = 0; i < 5; i++) {
			DirectCtx c{2000 + i * 200};
			(void)rig.knobs.handle(gold_turn(EncoderRole::GoldLower, 1), c.ctx);
		}
		// No popup pushed: the stack still holds only the menu.
		expect(rig.stack.depth()).to_equal(size_t(1));
	});

	it("files an OLED repaint for every COMMITTED popup update, not only the first", _ {
		// **A defect the level-2 golden found, pinned at the unit level.**
		// `ParamPopupScene::show` writes the popup's text; legacy's counterpart,
		// `display->displayNotification(...)` in `View::displayModEncoderValuePopup`, writes AND blits. The scene has no
		// `Result` of its own to file a repaint in (`show()` is not a `Scene::handle`) and its
		// `next_timer()` while showing is the popup's whole 2000 ms lifetime -- so without this
		// action the panel keeps the value it had when the popup opened, and the next OLED frame is
		// the one that draws the popup disappearing. A gold knob turned slowly showed a stale value
		// for two seconds; the *fast* path did not, because the throttled arm arms
		// `pending_flush_remaining_ms_` and `timer_repaints()` names `{.oled = true}`. So only the
		// immediate-commit arm lacked it.
		StubDisplay stub;
		display = &stub;

		spec::SongFixture f = spec::make_synth_song();
		KnobRig rig;
		rig.set_song(f.song);
		rig.set_target_with_timeline(f.clip, false);
		rig.knobs.set_mod_region();

		// **`ParamPopupScene` is a process singleton and the example above legitimately left it
		// showing.** Driven to its own expiry through its own `tick()` rather than by reaching into
		// its fields, so this example starts from the state a fresh boot has: nothing showing, and
		// therefore an arbitration that cannot refuse the first commit. Without it the leftover
		// `last_display_value_` can make the first turn take the same-param-refresh arm and this
		// example asserts about the previous one's state.
		expect(overlay::param_popup_scene().remaining_ms()).to_be_greater_than(0);
		overlay::param_popup_scene().tick(0); // the tick after a show takes nothing off its lifetime
		overlay::param_popup_scene().tick(3000);
		expect(overlay::param_popup_scene().remaining_ms()).to_equal(uint32_t{0});

		const auto repaints_main = [](const Result& r) {
			int32_t n = 0;
			for (const ShellAction& a : r.actions) {
				if (a.kind == ShellAction::Kind::Repaint && a.surface == deluge::ui::shell::SurfaceRole::Main) {
					n++;
				}
			}
			return n;
		};

		// The first turn commits (nothing is showing, so the arbitration cannot refuse) and pushes.
		DirectCtx first{2000};
		const Result r1 = rig.knobs.handle(gold_turn(EncoderRole::GoldLower, 5), first.ctx);
		expect(repaints_main(r1)).to_equal(int32_t{1});

		// A LATER turn, far enough apart that `kMinUpdateIntervalMs` (45 ms) has elapsed, so this
		// takes the immediate-commit arm too -- the arm that has to file a repaint. `kMinDisplayOwnership`
		// does not bite: it is the same param, so `current_param_owns_display` is true.
		DirectCtx second{2500};
		const Result r2 = rig.knobs.handle(gold_turn(EncoderRole::GoldLower, 5), second.ctx);
		expect(repaints_main(r2)).to_equal(int32_t{1});
		// It really was a commit and not a refresh: the shown value moved.
		const std::string moved{overlay::param_popup_scene().shown_value()};

		// And a turn that changes NOTHING files nothing: `has_param_info_changed` is false, the
		// arbitration takes its same-param-refresh arm, no pixel changes, no repaint is owed. This is
		// the half that makes the assertion above a mapping rather than "always file one".
		DirectCtx third{3000};
		const Result r3 = rig.knobs.handle(gold_turn(EncoderRole::GoldLower, 0), third.ctx);
		expect(repaints_main(r3)).to_equal(int32_t{0});
		expect(std::string(overlay::param_popup_scene().shown_value())).to_equal(moved);
	});

	it("gives the popup the model context legacy reads off View, so its filter-morph name can fire", _ {
		// `ParamPopupScene::show()` takes legacy's five value-conversion arguments and, optionally, a
		// model context (`overlay::PopupContext`). Without the context three of legacy's arms -- the
		// owner-aware LPF/HPF-morph rename (`modulation::params::getParamDisplayName`),
		// the stutter rate-bucket text, and the MIDI-clip CC naming -- cannot fire. `ModKnobs` is the
		// class that HAS that context, so it passes it. This drives the path with the context and the
		// one without and compares BOTH against the real
		// `View::displayModEncoderValuePopup`.
		StubDisplay stub;
		display = &stub;

		spec::SongFixture f = spec::make_synth_song();
		auto* sound = static_cast<SoundInstrument*>(f.instrument);
		// A ladder LPF is what makes the rename fire; an SVF or a filter that is off keeps the
		// default name (`getParamDisplayName`'s own comment in `modulation/params/param.cpp`).
		deluge::model::changeEffects(*sound->effectsOwner(), [](deluge::effects::EffectStates& e) { e.filter.lpf_mode = FilterMode::TRANSISTOR_24DB; });

		constexpr int32_t kMorph = deluge::modulation::params::LOCAL_LPF_MORPH;

		// Legacy, with the context it reads off `View`'s knobs.
		AudioEngine::audioSampleTimer = 0;
		stub.showing = false;
		view.setActiveModControllableTimelineCounter(f.clip, false);
		view.displayModEncoderValuePopup(Kind::PATCHED, kMorph, 0);
		const std::string legacy_named = stub.last_title;

		// The port, with the same context, from its own (drained) state.
		overlay::param_popup_scene().tick(0); // the tick after a show takes nothing off its lifetime
		overlay::param_popup_scene().tick(10000);
		expect(overlay::param_popup_scene().remaining_ms()).to_equal(uint32_t(0));
		overlay::PopupContext popup_context; // not `context`: CppSpec's own DSL owns that name here
		deluge::effects::EffectStates effects;
		effects.filter.lpf_mode = FilterMode::TRANSISTOR_24DB;
		popup_context.filter = effects.filter;
		overlay::param_popup_scene().show(Kind::PATCHED, kMorph, 0, PatchSource::NONE, PatchSource::NONE, 60000,
		                                  popup_context);
		const std::string port_named{overlay::param_popup_scene().shown_name()};

		// And with NO context -- the shape that cannot reach the context-aware arms.
		overlay::param_popup_scene().tick(0); // the tick after a show takes nothing off its lifetime
		overlay::param_popup_scene().tick(10000);
		overlay::param_popup_scene().show(Kind::PATCHED, kMorph, 0, PatchSource::NONE, PatchSource::NONE, 120000);
		const std::string port_unnamed{overlay::param_popup_scene().shown_name()};

		expect(legacy_named.empty()).to_be_false();
		expect(port_named).to_equal(legacy_named);
		// Not vacuous: the context really is what selects this name. Without it the port falls back
		// to the mode-agnostic one, which is exactly the arm the context-less call cannot leave.
		expect(port_unnamed).not_().to_equal(port_named);
	});

	it("moves View's own view of the focus when it writes the store they share", _ {
		spec::SongFixture f = spec::make_synth_song();
		currentSong = f.song;
		view.releaseActiveModControllable();
		KnobRig rig;
		rig.set_song(f.song);
		rig.knobs.use_focus(view.knobFocus(), [] { view.knobFocusChanged(); });
		rig.set_target_with_timeline(f.clip, false);
		alignas(ModelStackWithAutoParam) char owner_memory[MODEL_STACK_MAX_SIZE];
		const ModelStackWithThreeMainThings* owner = view_knob_stack(owner_memory);
		expect(owner).not_().to_be_null("the focus's owner stack");
		if (owner != nullptr) {
			Sound* const sound = static_cast<SoundInstrument*>(f.instrument);
			expect(std::get<Sound*>(owner->owner)).to_equal(sound);
		}
		expect(view.knobsOnClipContext()).to_be_true();
		rig.knobs.set_mod_region(96, 48, 3);
		expect(view.modNoteRowId()).to_equal(3);
		rig.knobs.set_mod_region();
		view.releaseActiveModControllable();
		currentSong = nullptr;
	});

	it("agrees with View::knobsOnClipContext on every reachable target shape", _ {
		// Legacy's clip context needs a resolved mod controllable as well as a clip counter: a target
		// with a clip counter and none is not a clip context, or `target_changed` would send a MIDI CC
		// legacy never sends. Shape 4 is the one that fails without that guard.
		StubDisplay stub;
		display = &stub;

		spec::SongFixture f = spec::make_synth_song();
		auto* sound = static_cast<SoundInstrument*>(f.instrument);

		KnobRig rig;
		rig.set_song(f.song);

		// 1. No counter, no controllable.
		view.setActiveModControllableTimelineCounter(nullptr, false);
		rig.set_target_with_timeline(nullptr, false);
		expect(deluge::ui::scenes::services::is_clip_context(rig.knobs.focus())).to_equal(view.knobsOnClipContext());
		expect(view.knobsOnClipContext()).to_be_false();

		// 2. No counter, a controllable.
		view.setActiveModControllableWithoutTimelineCounter(sound, &f.clip->paramManager);
		rig.set_target_without_timeline(sound, &f.clip->paramManager);
		expect(deluge::ui::scenes::services::is_clip_context(rig.knobs.focus())).to_equal(view.knobsOnClipContext());
		expect(view.knobsOnClipContext()).to_be_false();

		// 3. A clip counter and a controllable -- the only true case.
		view.setActiveModControllableTimelineCounter(f.clip, false);
		rig.set_target_with_timeline(f.clip, false);
		expect(deluge::ui::scenes::services::is_clip_context(rig.knobs.focus())).to_equal(view.knobsOnClipContext());
		expect(view.knobsOnClipContext()).to_be_true();

		// 4. A clip counter and NO controllable: legacy's stack dropped into that shape, and its focus is none. View
		//    answers from what its writers saw, and no writer leaves this shape, so only the port side is asked.
		alignas(ModelStackWithAutoParam) char knob_memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* knobs = view_knob_stack(knob_memory);
		expect(knobs).not_().to_be_null("the focus's owner stack");
		if (knobs != nullptr) {
			knobs->owner = deluge::model::LaneOwner{};
			const deluge::ui::scenes::bind::KnobFocus no_controllable = deluge::gui::knobFocusFromStack(*f.song, *knobs);
			expect(deluge::ui::scenes::services::is_clip_context(no_controllable)).to_be_false();
		}

		// 5. The SONG as the counter, with a controllable: set, but not a clip. `affectEntire` is
		//    what makes `Song::getActiveModControllable` resolve one at all (`song/song.cpp`);
		//    without it the song resolves nothing and this shape would collapse into shape 1.
		f.song->persistentView.affectEntire = true;
		view.setActiveModControllableTimelineCounter(f.song, false);
		rig.set_target_with_timeline(f.song, false);
		expect(deluge::ui::scenes::services::is_clip_context(rig.knobs.focus())).to_equal(view.knobsOnClipContext());
		expect(view.knobsOnClipContext()).to_be_false();
		// ...and it really is pointed at something, so shape 5 is not shape 1 in disguise.
		expect(std::holds_alternative<deluge::ui::scenes::bind::SongFocus>(rig.knobs.focus().owner)).to_be_true();

		// 6. A CV track in its clip: its knobs have an empty layout, yet it is a clip context, as it is for View.
		spec::SongFixture cv = spec::make_cv_song();
		currentSong = cv.song;
		rig.set_song(cv.song);
		view.setActiveModControllableTimelineCounter(cv.clip, false);
		rig.set_target_with_timeline(cv.clip, false);
		expect(std::holds_alternative<deluge::ui::scenes::bind::NoControlsFocus>(rig.knobs.focus().owner)).to_be_true();
		expect(deluge::ui::scenes::services::is_clip_context(rig.knobs.focus())).to_equal(view.knobsOnClipContext());
		expect(view.knobsOnClipContext()).to_be_true();
		view.releaseActiveModControllable();
		currentSong = nullptr;
	});

	context("a CV track's empty layout", _ {
		/// The knobs on @p f's clip, a scene up so indicators are declared, and a song that is current.
		struct CvRig {
			spec::SongFixture f = spec::make_cv_song();
			KnobRig rig;
			CvRig() {
				currentSong = f.song;
				rig.set_song(f.song);
				(void)rig.stack.push({SceneKind::FirstApp});
				rig.set_target_with_timeline(f.clip, false);
			}
			~CvRig() { currentSong = nullptr; }
		};

		it("sends no MIDI-follow CC for a CV clip, where a synth's focus sends", _ {
			// `View::sendMidiFollowFeedback` reaches the send for a CV clip (`isClipContext`), and
			// `MidiFollow::getModelStackWithParamForClip` returns no param for `OutputType::CV`: nothing is sent.
			StubDisplay stub;
			display = &stub;
			const auto saved_type = midiEngine.midiFollowFeedbackChannelType;
			const LearnedMIDI saved_a = midiEngine.midiFollowChannelType[0];
			const uint32_t saved_timer = AudioEngine::audioSampleTimer;
			midiEngine.midiFollowFeedbackChannelType = MIDIFollowFeedbackChannelType::A;
			midiEngine.midiFollowChannelType[0].channelOrZone = 0;
			AudioEngine::audioSampleTimer = 987654;

			const auto sent = [&](spec::SongFixture& f, bool region) {
				currentSong = f.song;
				f.song->setCurrentClip(f.clip);
				KnobRig rig;
				rig.set_song(f.song);
				clear_stamps();
				rig.set_target_with_timeline(f.clip, true);
				if (region) {
					clear_stamps();
					rig.knobs.set_mod_region(96, 48, 0);
				}
				const size_t count = stamped_ccs();
				currentSong = nullptr;
				return count;
			};
			spec::SongFixture synth = spec::make_synth_song();
			expect(sent(synth, false)).to_be_greater_than(size_t{0}); // the contrast: this setup does send
			spec::SongFixture cv = spec::make_cv_song();
			expect(sent(cv, false)).to_equal(size_t{0});
			expect(sent(cv, true)).to_equal(size_t{0});

			midiEngine.midiFollowFeedbackChannelType = saved_type;
			midiEngine.midiFollowChannelType[0] = saved_a;
			AudioEngine::audioSampleTimer = saved_timer;
			clear_stamps();
		});

		it("has no mode in the service", _ {
			CvRig r;
			expect(std::holds_alternative<deluge::ui::scenes::bind::NoControlsFocus>(r.rig.knobs.focus().owner)).to_be_true();
			expect(r.rig.knobs.mode()).to_equal(-1);
		});

		it("has no mode in the view", _ {
			CvRig r;
			view.setActiveModControllableTimelineCounter(r.f.clip, false);
			expect(view.knobMode()).to_equal(-1);
			view.releaseActiveModControllable();
		});

		it("toggles no VU meter and shows nothing on mod button 0", _ {
			CvRig r;
			CountingPresentation out;
			r.rig.knobs.section_button(0, true, out);
			r.rig.knobs.section_button(0, false, out);
			expect(r.rig.knobs.focus().vu_meter).to_be_false();
			expect(out.shown).to_equal(0);
		});

		it("takes no turn", _ {
			CvRig r;
			CountingPresentation out;
			r.rig.knobs.turn(0, 3, 1000, out);
			r.rig.knobs.turn(1, -3, 1000, out);
			expect(out.shown).to_equal(0);
		});

		it("takes no push", _ {
			CvRig r;
			CountingPresentation out;
			r.rig.knobs.push(0, true, {}, out);
			r.rig.knobs.push(1, true, {.shift = true}, out);
			expect(out.shown).to_equal(0);
		});

		it("names no address for a knob", _ {
			CvRig r;
			expect(r.rig.knobs.knob_address(0).has_value()).to_be_false();
		});

		it("declares both indicators at zero", _ {
			CvRig r;
			deluge::ui::widgets::core::LedFrame frame;
			r.rig.knobs.leds(frame);
			for (int32_t k = 0; k < 2; k++) {
				const deluge::ui::widgets::core::KnobIndicator* port = frame.indicator_or_null(static_cast<deluge::ui::widgets::core::IndicatorId>(k));
				expect(port).not_().to_be_null("the column's indicator");
				if (port != nullptr) {
					for (int32_t seg = 0; seg < 4; seg++) {
						expect(static_cast<int>(port->levels[seg])).to_equal(0);
					}
				}
			}
		});
	});

	it("takes a gold turn, a mod button and a gold push only after the scene declines them", _ {
		// All three of `View`'s base-class-fallback hooks, against the real service event set.
		// Legacy dispatches every one of them through `getCurrentUI()` and reaches `view` only
		// through `UI`'s own forwarding bodies: `modEncoderAction` (`interpretEncoders` in `hid/encoder_input.cpp`,
		// then `UI::modEncoderAction` in `gui/ui/ui.cpp`), `modButtonAction` (`Buttons::buttonAction`,
		// then `UI::modButtonAction`) and `modEncoderButtonAction` (`Buttons::buttonAction`, then
		// `UI::modEncoderButtonAction`). The scene gets each first and the service only after it
		// declines; `tests/unit/ui/shell/router_spec.cpp` holds the ordering proof
		// with a shared call log; this example pins WHICH events reach the service at all.
		//
		// The pushes are built as `UiEvent::Kind::EncoderButton`, which is what `InputLowering`
		// actually emits for `ButtonId::ModEnc0`/`ModEnc1` (`InputLowering::on_button`). Plain
		// `Kind::Button` events would be a shape the lowering never produces.
		RouteRig r;

		// A scene that claims -- one of the six/two/five overriding UIs -- is the end of the line
		// for all three shapes.
		r.router.route(gold_turn(EncoderRole::GoldLower, 1), r.ctx());
		expect(r.scene.calls).to_equal(1);
		expect(r.mod_knobs.calls).to_equal(0);

		r.router.route(encoder_button_event(ButtonId::ModEnc0, true), r.ctx());
		expect(r.scene.calls).to_equal(2);
		expect(r.mod_knobs.calls).to_equal(0);
		expect(r.scene.last.button).to_equal(ButtonId::ModEnc0);

		r.router.route(button_event(ButtonId::Mod0, true), r.ctx());
		expect(r.scene.calls).to_equal(3);
		expect(r.mod_knobs.calls).to_equal(0);

		// A scene that declines -- a UI inheriting `UI`'s forward-to-`view` body -- drops all three
		// through to the service.
		r.scene.claim = false;
		r.router.route(gold_turn(EncoderRole::GoldLower, 1), r.ctx());
		expect(r.scene.calls).to_equal(4);
		expect(r.mod_knobs.calls).to_equal(1);

		r.router.route(button_event(ButtonId::Mod0, true), r.ctx());
		expect(r.scene.calls).to_equal(5);
		expect(r.mod_knobs.calls).to_equal(2);
		expect(r.mod_knobs.last.button).to_equal(ButtonId::Mod0);

		r.router.route(encoder_button_event(ButtonId::ModEnc1, true), r.ctx());
		expect(r.scene.calls).to_equal(6);
		expect(r.mod_knobs.calls).to_equal(3);
		expect(r.mod_knobs.last.button).to_equal(ButtonId::ModEnc1);

		// And the SELECT encoder's push is still not one of these: legacy's tail handles it itself
		// and its hook has no `view` fallback, so it must reach the scene and then `global_controls`,
		// never `mod_knobs`.
		r.router.route(encoder_button_event(ButtonId::SelectEnc, true), r.ctx());
		expect(r.scene.calls).to_equal(7);
		expect(r.mod_knobs.calls).to_equal(3);
	});

	it("claims exactly the events it is the View fallback for, and nothing else", _ {
		// Defence in depth behind the routing example above: what `ModKnobs::handle` reports
		// `handled` for, called directly. Legacy consumes all three unconditionally -- the mod
		// button and the push both end in `Buttons::buttonAction`'s `dealtWith`
		// (`Buttons::buttonAction`) and the turn is the last thing `interpretEncoders` does
		// with it -- so `handled` is true even on the arms that do nothing.
		spec::SongFixture f = spec::make_synth_song();
		KnobRig rig;
		rig.set_song(f.song);
		DirectCtx c{0};
		expect(rig.knobs.handle(encoder_button_event(ButtonId::ModEnc0, true), c.ctx).handled).to_be_true();
		expect(rig.knobs.handle(button_event(ButtonId::Mod4, true), c.ctx).handled).to_be_true();
		// ...and not the rest.
		expect(rig.knobs.handle(encoder_button_event(ButtonId::SelectEnc, true), c.ctx).handled).to_be_false();
		expect(rig.knobs.handle(button_event(ButtonId::Back, true), c.ctx).handled).to_be_false();
		expect(rig.knobs.handle(gold_turn(EncoderRole::Select, 1), c.ctx).handled).to_be_false();
		expect(rig.knobs.handle(gold_turn(EncoderRole::GoldLower, 0), c.ctx).handled).to_be_false();
	});

	// --- The buttons, the mode, the LEDs and the indicators -------------------------

	it("a mod button changes the knob owner's mode in its record: a track's and the song's", _ {
		StubDisplay stub;
		display = &stub;
		spec::SongFixture f = spec::make_synth_song();
		currentSong = f.song;
		KnobRig rig;
		rig.set_song(f.song);
		rig.set_target_with_timeline(f.clip, false);
		rig.knobs.mod_button_action(5, true);
		rig.knobs.mod_button_action(5, false);
		const deluge::project::DocOutput* record = f.song->project.output_structure.find(*f.instrument->id());
		expect(record).not_().to_be_null();
		if (record != nullptr) {
			expect(record->mod_knob_mode).to_equal(5);
		}

		rig.set_target_without_timeline(&f.song->live.master_channel, &f.song->paramManager);
		rig.knobs.mod_button_action(3, true);
		rig.knobs.mod_button_action(3, false);
		expect(f.song->project.mod_knob_mode).to_equal(uint8_t{3});
		display = nullptr;
	});

	it("a push cycles the filter knob and the record's view and controls follow", _ {
		StubDisplay stub;
		display = &stub;
		spec::SongFixture f = spec::make_synth_song();
		currentSong = f.song;
		KnobRig rig;
		rig.set_song(f.song);
		rig.set_target_with_timeline(f.clip, false);
		rig.knobs.mod_button_action(1, true);
		rig.knobs.mod_button_action(1, false);
		UiEvent push{};
		push.kind = UiEvent::Kind::EncoderButton;
		push.button = ButtonId::ModEnc1;
		push.on = true;
		DirectCtx plain{0};
		(void)rig.knobs.handle(push, plain.ctx);
		const deluge::project::DocOutput* record = f.song->project.output_structure.find(*f.instrument->id());
		// The knob cycles to the high-pass frequency, and its partner to the high-pass resonance.
		const deluge::project::ModKnobControl upper{.button = 1, .knob = 1};
		const deluge::project::ModKnobControl lower{.button = 1, .knob = 0};
		std::optional<deluge::plugin::ParamId> upper_target;
		std::optional<deluge::plugin::ParamId> lower_target;
		for (const deluge::project::ControllerMapping& mapping : record->controls) {
			if (mapping.control == upper) {
				upper_target = mapping.target;
			}
			if (mapping.control == lower) {
				lower_target = mapping.target;
			}
		}
		expect(upper_target).to_equal(std::optional{deluge::instruments::classic::param::kHpfFrequency});
		expect(lower_target).to_equal(std::optional{deluge::instruments::classic::param::kHpfResonance});

		rig.set_target_without_timeline(&f.song->live.master_channel, &f.song->paramManager);
		const uint8_t before = f.song->project.master_effects.filter_type;
		rig.knobs.mod_button_action(1, true);
		rig.knobs.mod_button_action(1, false);
		(void)rig.knobs.handle(push, plain.ctx);
		expect(before).to_equal(uint8_t{0});                                  // low-pass
		expect(f.song->project.master_effects.filter_type).to_equal(uint8_t{1}); // high-pass
		display = nullptr;
		currentSong = nullptr;
	});

	it("declares the eight mod LEDs exactly as View::setModLedStates writes them, in every mode", _ {
		// The mode -> LED table, asserted BY IDENTITY: all eight declarations, on every one of the
		// eight modes, with the port driven into that mode through `mod_button_action` and its answer read from the
		// `LedFrame` it declared into, against the table `View::setModLedStates` was recorded writing (`kRecordedModLeds`).
		// Nothing here counts lit LEDs; a table with the right number of entries and the wrong ones set fails on the
		// first mode.
		StubDisplay stub;
		display = &stub;

		spec::SongFixture f = spec::make_synth_song();
		currentSong = f.song;

		KnobRig rig;
		rig.set_song(f.song);
		rig.set_target_with_timeline(f.clip, false);
		// The stack must be non-empty for `declare_indicators` (legacy's `if (!getRootUI()) return`);
		// the LED half below does not depend on it, but leaving it empty would silently skip the
		// indicator declarations the next example checks.
		expect(rig.stack.push({deluge::ui::scenes::overlay::kParamPopupSceneKind})).to_be_true();

		for (int32_t mode = 0; mode < 8; mode++) {
			rig.knobs.mod_button_action(mode, true);
			rig.knobs.mod_button_action(mode, false);

			// The port really did change mode -- otherwise every row below would compare two
			// unchanged tables and pass on a port that ignores the button entirely.
			expect(rig.knobs.mode()).to_equal(mode);

			deluge::ui::widgets::core::LedFrame frame;
			rig.knobs.leds(frame);
			for (int32_t i = 0; i < 8; i++) {
				const std::optional<ModLed> port = port_mod_led(frame, i);
				const std::string where = "mode=" + std::to_string(mode) + " led=" + std::to_string(i);
				if (!port.has_value()) {
					expect(where + " undeclared").to_equal(where);
					continue;
				}
				const ModLed recorded{(kRecordedModLeds[static_cast<size_t>(mode)] >> i & 1) != 0, false};
				if (*port != recorded) {
					expect(where + " port{lit=" + std::to_string(port->lit) + ",blink=" + std::to_string(port->blinking)
					       + "} recorded{lit=" + std::to_string(recorded.lit) + "}")
					    .to_equal(where);
				}
			}
		}
		display = nullptr;
		currentSong = nullptr;
	});

	it("blinks the volume mod LED when the VU meter is on, and only in the song context", _ {
		// `View::setModLedStates` -- the one row of the table that is not "on iff selected", and the only reason the VU
		// meter is ported at all. It needs three things at once: the song context, mod button 0 selected, and the meter
		// on. `mod_button_action` toggles the meter -- pressing the already-selected button 0 outside a clip minder
		// (`View::modButtonAction`, `origin/main:src/deluge/gui/views/view.cpp`) -- so this drives the toggle as well
		// as the LED it feeds. The recorded values are `View::modButtonAction`'s and `View::setModLedStates`'s on the
		// same presses.
		StubDisplay stub;
		display = &stub;

		// `itsTheSong` requires the knobs to point at a *song*, and a song's mod-knob mode lives in its record
		// (`project.mod_knob_mode`).
		spec::SongFixture pf = spec::make_synth_song();
		currentSong = pf.song;
		pf.song->persistentView.affectEntire = true; // so the SONG resolves a mod controllable (`Song::getActiveModControllable` in `song/song.cpp`)

		KnobRig rig;
		rig.set_song(pf.song);
		rig.set_target_with_timeline(pf.song, false); // the song, so `itsTheSong` is true
		expect(rig.knobs.display_vu_meter()).to_be_false();

		// Select a different mod button first, so the press that follows is a *selection* and not
		// already the toggle.
		rig.knobs.mod_button_action(1, true);
		expect(rig.knobs.mode()).to_equal(1);

		// First press of 0 selects it; it was not already selected, so no toggle.
		rig.knobs.mod_button_action(0, true);
		// Recorded from `View::modButtonAction`: a first press selects, the meter stays off, LED 0 is steady.
		expect(rig.knobs.display_vu_meter()).to_be_false();
		{
			deluge::ui::widgets::core::LedFrame frame;
			rig.knobs.leds(frame);
			expect(port_mod_led(frame, 0).value()).to_equal(ModLed{true, false});
		}

		// Second press: already selected, button 0, not a clip minder -> the meter turns on, and
		// mod LED 0 goes from steady to blinking.
		rig.knobs.mod_button_action(0, true);
		expect(rig.knobs.display_vu_meter()).to_be_true();
		{
			deluge::ui::widgets::core::LedFrame frame;
			currentSong = pf.song;
			rig.knobs.leds(frame);
			const ModLed port = port_mod_led(frame, 0).value();
			expect(port.blinking).to_be_true();
			// Recorded from `View::setModLedStates`: the meter on, LED 0 lit and blinking.
			expect(port).to_equal(ModLed{true, true});
			// And every other mod LED is still plain off -- by identity, not "at least one on".
			for (int32_t i = 1; i < 8; i++) {
				expect(port_mod_led(frame, i).value()).to_equal(ModLed{false, false});
			}
		}

		// Point the knobs at a CLIP instead: same meter flag, but `itsTheSong` is false, so the
		// blink arm's first clause fails and the LED is merely on.
		currentSong = pf.song;
		rig.set_target_with_timeline(pf.clip, false);
		expect(deluge::ui::scenes::services::is_clip_context(rig.knobs.focus())).to_be_true();
		// The clip's `SoundInstrument` carries its own mod-knob mode, which is not the song's, so
		// select button 0 on it. It was not already selected, so this is a plain selection and the
		// meter flag -- the thing under test -- is untouched.
		currentSong = pf.song;
		rig.knobs.mod_button_action(0, true);
		currentSong = pf.song;
		rig.knobs.mod_button_action(0, false);
		expect(rig.knobs.mode()).to_equal(0);
		expect(rig.knobs.display_vu_meter()).to_be_true();
		{
			deluge::ui::widgets::core::LedFrame frame;
			currentSong = pf.song;
			rig.knobs.leds(frame);
			const ModLed port = port_mod_led(frame, 0).value();
			expect(port.lit).to_be_true();
			expect(port.blinking).to_be_false(); // `itsTheSong` is false, so the blink arm cannot fire
		}
		display = nullptr;
		currentSong = nullptr;
	});

	it("turns every mod LED off in the automation editor, and declares no indicator there", _ {
		// `View::setModLedStates` (`views/view.cpp`) and `View::modButtonAction`
		// (`origin/main:src/deluge/gui/views/view.cpp`). Legacy's condition is
		// `(getRootUI() == &automationView) && automationView.inAutomationEditor()`, which no spec process can produce
		// -- there is no root UI here at all -- so this arm is driven through the injected `ViewState` the port carries
		// in its place, and it is the ONE row of the table whose legacy side is asserted from the source rather than
		// from a live comparison. Named here rather than left implicit.
		StubDisplay stub;
		display = &stub;

		spec::SongFixture f = spec::make_synth_song();
		currentSong = f.song;
		KnobRig rig;
		rig.set_song(f.song);
		rig.set_target_with_timeline(f.clip, false);
		expect(rig.stack.push({deluge::ui::scenes::overlay::kParamPopupSceneKind})).to_be_true();
		rig.knobs.mod_button_action(3, true);
		rig.knobs.mod_button_action(3, false);
		expect(rig.knobs.mode()).to_equal(3);

		{
			deluge::ui::widgets::core::LedFrame frame;
			rig.knobs.leds(frame);
			expect(port_mod_led(frame, 3).value()).to_equal(ModLed{true, false});
			expect(frame.indicator_or_null(deluge::ui::widgets::core::IndicatorId::Lower)).not_().to_be_null();
		}

		ModKnobs::ViewState state;
		state.in_automation_editor = true;
		rig.knobs.set_view_state(state);
		{
			deluge::ui::widgets::core::LedFrame frame;
			rig.knobs.leds(frame);
			for (int32_t i = 0; i < 8; i++) {
				expect(port_mod_led(frame, i).value()).to_equal(ModLed{false, false});
			}
			// `View::setKnobIndicatorLevels`'s early return (`views/view.cpp`): legacy leaves both columns exactly as
			// they were, which here is "declares nothing".
			expect(frame.indicator_or_null(deluge::ui::widgets::core::IndicatorId::Lower)).to_be_null();
			expect(frame.indicator_or_null(deluge::ui::widgets::core::IndicatorId::Upper)).to_be_null();
		}

		// `View::modButtonAction` ignores the mod button in the automation editor -- and the mod button itself is
		// ignored, so the mode does not move.
		rig.knobs.mod_button_action(5, true);
		expect(rig.knobs.mode()).to_equal(3);
		// ...except mod button 0 on arranger automation view (`View::modButtonAction`).
		state.automation_on_arranger = true;
		rig.knobs.set_view_state(state);
		rig.knobs.mod_button_action(0, true);
		expect(rig.knobs.mode()).to_equal(0);
		display = nullptr;
		currentSong = nullptr;
	});

	it("levels both knob indicators exactly as the recorded View::setKnobIndicatorLevel does", _ {
		// The indicator half, by identity: all four segment brightnesses of both columns, compared
		// against what `View::setKnobIndicatorLevel` was recorded handing `deluge_control_set_indicator`. That covers
		// the whole chain -- the param read, the clamp, the `kKnobPosOffset` shift, the patch-cable fold, and
		// `indicator_leds`' own brightness arithmetic, which the port re-implements as `ui::knob_indicator_levels`.
		//
		// Driven across all eight mod-knob modes because the mode is what decides WHICH param each
		// gold knob addresses, and across a turn on each knob so the levels are not all the fixture
		// default.
		StubDisplay stub;
		display = &stub;

		spec::SongFixture f = spec::make_synth_song();
		currentSong = f.song;

		KnobRig rig;
		rig.set_song(f.song);
		rig.set_target_with_timeline(f.clip, false);
		expect(rig.stack.push({deluge::ui::scenes::overlay::kParamPopupSceneKind})).to_be_true();

		const auto found = indicator_disagreements(rig, f.song, "synth", knob_recorded::kSynth);
		report_indicators(found);
		expect(found.empty()).to_be_true();
		display = nullptr;
		currentSong = nullptr;
	});

	it("levels a kit row's, a kit's chain's and an audio track's indicators as the recorded view does", _ {
		StubDisplay stub;
		display = &stub;
		std::vector<std::string> found;
		{
			spec::SongFixture kit = spec::make_kit_song();
			currentSong = kit.song;
			SoundDrum* drum = spec::add_drum<SoundDrum>(kit);
			static_cast<Kit*>(kit.instrument)->selectedDrum = drum;
			kit.clip->affectEntire = false;
			KnobRig rig;
			expect(rig.stack.push({deluge::ui::scenes::overlay::kParamPopupSceneKind})).to_be_true();
			rig.set_song(kit.song);
			rig.set_target_with_timeline(kit.clip, false);
			auto more = indicator_disagreements(rig, kit.song, "kit row", knob_recorded::kKitRow);
			found.insert(found.end(), more.begin(), more.end());
		}
		{
			spec::SongFixture kit = spec::make_kit_song();
			kit.clip->affectEntire = true;
			currentSong = kit.song;
			KnobRig rig;
			expect(rig.stack.push({deluge::ui::scenes::overlay::kParamPopupSceneKind})).to_be_true();
			rig.set_song(kit.song);
			rig.set_target_with_timeline(kit.clip, false);
			auto more = indicator_disagreements(rig, kit.song, "kit chain", knob_recorded::kKitChain);
			found.insert(found.end(), more.begin(), more.end());
		}
		{
			spec::AudioSongFixture audio = spec::make_audio_clip_song();
			currentSong = audio.song;
			KnobRig rig;
			expect(rig.stack.push({deluge::ui::scenes::overlay::kParamPopupSceneKind})).to_be_true();
			rig.set_song(audio.song);
			rig.set_target_with_timeline(audio.clip, false);
			auto more = indicator_disagreements(rig, audio.song, "audio", knob_recorded::kAudio);
			found.insert(found.end(), more.begin(), more.end());
		}
		report_indicators(found);
		expect(found.empty()).to_be_true();
		display = nullptr;
		currentSong = nullptr;
	});

	it("levels the song's indicators as the recorded view does, its compressor view pushed in", _ {
		StubDisplay stub;
		display = &stub;
		spec::SongFixture song = spec::make_synth_song();
		song.song->persistentView.affectEntire = true;
		currentSong = song.song;
		KnobRig rig;
		expect(rig.stack.push({deluge::ui::scenes::overlay::kParamPopupSceneKind})).to_be_true();
		rig.set_song(song.song);
		rig.set_target_with_timeline(song.song, false);
		const auto found = indicator_disagreements(rig, song.song, "song", knob_recorded::kSong, [&](DirectCtx& c) {
			// The compressor button, its upper knob pushed in to edit the compressor, the lower pushed to its attack.
			rig.knobs.mod_button_action(4, true);
			rig.knobs.mod_button_action(4, false);
			UiEvent push{};
			push.kind = UiEvent::Kind::EncoderButton;
			push.on = true;
			push.button = ButtonId::ModEnc1;
			(void)rig.knobs.handle(push, c.ctx);
			push.button = ButtonId::ModEnc0;
			(void)rig.knobs.handle(push, c.ctx);
		}, knob_recorded::kSongCompressor);
		report_indicators(found);
		expect(found.empty()).to_be_true();
		display = nullptr;
		currentSong = nullptr;
	});

	it("levels a MIDI track's indicators as the recorded view does, its CCs unassigned and assigned", _ {
		StubDisplay stub;
		display = &stub;
		std::vector<std::string> found;
		for (const bool assigned : {false, true}) {
			spec::SongFixture midi = spec::make_midi_song();
			// Mode 2's knobs send nothing: the knobs show them at their bipolar midpoint, which lights nothing.
			currentSong = midi.song;
			const deluge::project::TrackAddress track{*midi.instrument->id()};
			(void)deluge::edits::modKnobCCFor(track, 4, {.kind = deluge::project::ModKnobCc::Kind::NONE});
			(void)deluge::edits::modKnobCCFor(track, 5, {.kind = deluge::project::ModKnobCc::Kind::NONE});
			if (assigned) {
				using Cc = deluge::project::ModKnobCc;
				(void)deluge::edits::modKnobCCFor(track, 0, {.kind = Cc::Kind::NUMBER, .number = 74});
				(void)deluge::edits::modKnobCCFor(track, 1, {.kind = Cc::Kind::BEND});
				(void)deluge::edits::modKnobCCFor(track, 2, {.kind = Cc::Kind::AFTERTOUCH});
				(void)deluge::edits::modKnobCCFor(track, 3, {.kind = Cc::Kind::NUMBER, .number = Cc::kYAxisNumber});
			}
			KnobRig rig;
			expect(rig.stack.push({deluge::ui::scenes::overlay::kParamPopupSceneKind})).to_be_true();
			rig.set_song(midi.song);
			rig.set_target_with_timeline(midi.clip, false);
			auto more = indicator_disagreements(rig, midi.song, assigned ? "midi assigned" : "midi unassigned",
			                                    assigned ? knob_recorded::kMidiAssigned : knob_recorded::kMidiUnassigned);
			found.insert(found.end(), more.begin(), more.end());
		}
		report_indicators(found);
		expect(found.empty()).to_be_true();
		display = nullptr;
		currentSong = nullptr;
	});

	it("clears both indicator columns when the knobs point at nothing, as clearKnobIndicatorLevels does", _ {
		// `View::setKnobIndicatorLevels` (`views/view.cpp`). `indicator_leds::clearKnobIndicatorLevels`
		// (`hid/led/indicator_leds.cpp`) is `setKnobIndicatorLevel(i, 0)` for both, so the
		// legacy side is driven through exactly that and read back from the same shadow.
		KnobRig rig;
		expect(rig.stack.push({deluge::ui::scenes::overlay::kParamPopupSceneKind})).to_be_true();
		expect(std::holds_alternative<std::monostate>(rig.knobs.focus().owner)).to_be_true();

		indicator_leds::clearKnobIndicatorLevels();

		deluge::ui::widgets::core::LedFrame frame;
		rig.knobs.leds(frame);
		for (int32_t k = 0; k < 2; k++) {
			const uint8_t* legacy = indicator_shadow(k);
			const deluge::ui::widgets::core::KnobIndicator* port = frame.indicator_or_null(static_cast<deluge::ui::widgets::core::IndicatorId>(k));
			expect(legacy).not_().to_be_null("the column's shadow");
			expect(port).not_().to_be_null("the column's indicator");
			if (legacy != nullptr && port != nullptr) {
				for (int32_t seg = 0; seg < 4; seg++) {
					expect(static_cast<int32_t>(port->levels[seg])).to_equal(static_cast<int32_t>(legacy[seg]));
				}
			}
		}
	});

	it("declares no indicator at all with an empty scene stack, as legacy declares none with no root UI", _ {
		// `View::setKnobIndicatorLevels` (`views/view.cpp`). The mod LEDs are unaffected -- that block is below the
		// early return in legacy and outside `declare_indicators` here -- so this also pins that the two halves of
		// `leds()` are gated separately.
		spec::SongFixture f = spec::make_synth_song();
		currentSong = f.song;
		KnobRig rig;
		rig.set_song(f.song);
		rig.set_target_with_timeline(f.clip, false);
		expect(rig.stack.depth()).to_equal(size_t(0));

		deluge::ui::widgets::core::LedFrame frame;
		rig.knobs.leds(frame);
		expect(frame.indicator_or_null(deluge::ui::widgets::core::IndicatorId::Lower)).to_be_null();
		expect(frame.indicator_or_null(deluge::ui::widgets::core::IndicatorId::Upper)).to_be_null();
		expect(port_mod_led(frame, 0).has_value()).to_be_true();
		currentSong = nullptr;
	});

	it("handles a gold-knob push the way View::modEncoderButtonAction does", _ {
		// `View::modEncoderButtonAction` and its two helpers (`origin/main:src/deluge/gui/views/view.cpp`). The push
		// arrives as an `EncoderButton` event, which is what `InputLowering` actually emits for `ButtonId::ModEnc0`
		// (`InputLowering::on_button`) -- and which the router routes here, after the scene
		// (`tests/unit/ui/shell/router_spec.cpp`).
		StubDisplay stub;
		display = &stub;
		spec::SongFixture f = spec::make_synth_song();
		InstrumentClip* port_clip = add_synth_clip(f.song, "MODPUSH");
		currentSong = f.song;

		KnobRig rig;
		rig.set_song(f.song);
		rig.set_target_with_timeline(port_clip, false);
		view.setActiveModControllableTimelineCounter(f.clip, false);

		UiEvent push{};
		push.kind = UiEvent::Kind::EncoderButton;
		push.button = ButtonId::ModEnc0;
		push.on = true;

		// Claimed unconditionally: legacy's tail always reaches `dealtWith`
		// (`buttonAction` in `hid/buttons.cpp`).
		DirectCtx plain{0};
		const Result r = rig.knobs.handle(push, plain.ctx);
		expect(r.handled).to_be_true();
		expect(r.actions.size()).to_equal(size_t(0)); // no shift: no "automation deleted" popup

		// Shift + push deletes the param's automation and raises the popup main's
		// `View::modEncoderButtonAction_deleteAutomation` (`origin/main:src/deluge/gui/views/view.cpp`) raises: there
		// is a param to delete on this fixture, so the action must be there.
		struct ShiftCtx {
			deluge::ui::shell::ButtonSet held{};
			deluge::ui::widgets::core::PadMask pads{};
			UiContext ctx;
			ShiftCtx() : ctx{held, pads, true, false, 0, deluge_profile()} {}
		} shifted;
		const Result deleted = rig.knobs.handle(push, shifted.ctx);
		expect(deleted.handled).to_be_true();
		expect(deleted.actions.size()).to_equal(size_t(1));
		expect(deleted.actions[0].kind).to_equal(ShellAction::Kind::Popup);
		expect(deleted.actions[0].popup_string)
		    .to_equal(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_AUTOMATION_DELETED));

		// LEARN held: `View::modEncoderButtonAction` returns immediately and still counts as dealt with.
		struct LearnCtx {
			deluge::ui::shell::ButtonSet held = learn_held();
			deluge::ui::widgets::core::PadMask pads{};
			UiContext ctx{held, pads, true, false, 0, deluge_profile()};
			static deluge::ui::shell::ButtonSet learn_held() {
				deluge::ui::shell::ButtonSet b{};
				b.set(ButtonId::Learn, true);
				return b;
			}
		} learning;
		const Result learn = rig.knobs.handle(push, learning.ctx);
		expect(learn.handled).to_be_true();
		expect(learn.actions.size()).to_equal(size_t(0)); // no popup: the shift arm never ran
		display = nullptr;
		currentSong = nullptr;
	});

	it("arms notifyParamAutomationOccurred's two timers exactly when legacy does", _ {
		StubDisplay stub;
		display = &stub;

		spec::SongFixture f = spec::make_synth_song();
		KnobRig rig;
		rig.set_song(f.song);
		rig.set_target_with_timeline(f.clip, false);
		rig.knobs.tick(5000, 0);

		// An owner the knobs are NOT pointed at arms nothing (`View::notifyParamAutomationOccurred`).
		const deluge::project::ParamAddress other{.owner = deluge::project::OwnerId::song()};
		rig.knobs.notify_param_automation_occurred(other, true);
		expect(rig.knobs.automation_display_pending()).to_be_false();
		expect(rig.knobs.next_timer().has_value()).to_be_false();

		// The one they are pointed at arms both, 25ms out (`View::notifyParamAutomationOccurred` in `views/view.cpp`).
		const deluge::project::ParamAddress ours = *deluge::ui::scenes::bind::knob_owner(rig.knobs.focus());
		rig.knobs.notify_param_automation_occurred(ours, false);
		expect(rig.knobs.automation_display_pending()).to_be_true();
		expect(rig.knobs.automation_updates_mod_levels()).to_be_false();
		expect(rig.knobs.next_timer().value()).to_equal(uint32_t(25));

		// A second call inside the window does not re-arm, but does upgrade the payload
		// (`View::notifyParamAutomationOccurred`).
		rig.knobs.tick(5010, 10);
		rig.knobs.notify_param_automation_occurred(ours, true);
		expect(rig.knobs.automation_updates_mod_levels()).to_be_true();
		expect(rig.knobs.next_timer().value()).to_equal(uint32_t(15));

		// And it disarms on its own deadline, with no input.
		rig.knobs.tick(5025, 15);
		expect(rig.knobs.automation_display_pending()).to_be_false();
		expect(rig.knobs.next_timer().has_value()).to_be_false();
	});

	context("over the ports' fakes", _ {
		using FeedbackCall = bind::test::RecordingMidiFeedback::Call;

		it("sends a turned param's CC and the knob's new position when MIDI follow's feedback is on", _ {
			FakeKnobs k;
			k.fakes.host.midi_feedback.on = true;
			k.rig.knobs.turn(1, 3, 1000, k.out); // from the fake's unheld param, which reads 0, to 3
			expect(k.fakes.host.controls_sink.param_writes.size()).to_equal(size_t{1});
			expect(k.fakes.host.midi_feedback.calls.size()).to_equal(size_t{1});
			if (k.fakes.host.midi_feedback.calls.size() == 1) {
				const FeedbackCall& sent = k.fakes.host.midi_feedback.calls[0];
				expect(sent.kind).to_equal(FeedbackCall::Kind::SEND);
				expect(sent.cc).to_equal(kLpfFrequencyCc);
				expect(sent.knob_pos).to_equal(int32_t{3});
			}
		});

		it("sends nothing for a turn when MIDI follow's feedback is off", _ {
			FakeKnobs k;
			k.rig.knobs.turn(1, 1, 1000, k.out);
			expect(k.fakes.host.controls_sink.param_writes.size()).to_equal(size_t{1});
			expect(k.fakes.host.midi_feedback.calls.empty()).to_be_true();
		});

		it("sends every automated param's CC on a new mod region when the feedback automation row is on", _ {
			FakeKnobs k;
			k.fakes.host.midi_feedback.on = true;
			(void)k.fakes.host.device.write(deluge::project::device_path::kFollowFeedbackAutomation,
			                                std::to_underlying(deluge::project::FeedbackAutomation::LOW));
			k.rig.knobs.set_mod_region();
			expect(k.fakes.host.midi_feedback.calls.size()).to_equal(size_t{1});
			if (k.fakes.host.midi_feedback.calls.size() == 1) {
				expect(k.fakes.host.midi_feedback.calls[0].kind).to_equal(FeedbackCall::Kind::SEND_ALL);
				expect(k.fakes.host.midi_feedback.calls[0].automation).to_be_true();
			}
		});

		it("sends nothing on a new mod region when the feedback automation row is DISABLED", _ {
			FakeKnobs k;
			k.fakes.host.midi_feedback.on = true;
			k.rig.knobs.set_mod_region();
			expect(k.fakes.host.midi_feedback.calls.empty()).to_be_true();
		});

		it("grabs a held region's values on a stopped clip, and not while a clock runs", _ {
			FakeKnobs stopped;
			stopped.rig.knobs.set_mod_region(0, 96, 0);
			expect(stopped.grabs()).to_equal(size_t{1});

			FakeKnobs running;
			running.fakes.host.transport.clocks = true;
			running.rig.knobs.set_mod_region(0, 96, 0);
			expect(running.grabs()).to_equal(size_t{0});
		});

		it("writes and sends nothing for a turn once the set is taken away", _ {
			FakeKnobs k;
			k.rig.knobs.set_ports(nullptr);
			k.fakes.host.midi_feedback.on = true;
			k.rig.knobs.turn(1, 1, 1000, k.out);
			expect(k.fakes.host.controls_sink.param_writes.empty()).to_be_true();
			expect(k.fakes.host.midi_feedback.calls.empty()).to_be_true();
		});
	});
});
// clang-format on

DELUGE_SPEC(mod_knobs_spec, mod_knobs_spec_body)
