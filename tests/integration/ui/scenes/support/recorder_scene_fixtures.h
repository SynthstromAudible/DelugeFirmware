#pragma once

/// @file
/// @brief The recorder's fixtures, which `recorder_scene_spec` and the menu oracle freezer share: a synth whose source
///        0 has one range for a take to land in, with a `Shell` over it, and the guards every example holds the
///        process's globals with.

#include "gui/ui/menu_view_of.h"
#include "gui/ui/shell_host/shell_host.h"
#include "model/edits/setting_edit.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/node_id.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/recorder/recorder_scene.h"
#include "ui/scenes/registry.h"

#include "ui/shell/profiles/deluge.h"
#include "ui/shell/shell.h"

#include "base/notify/dirty_set.h"
#include "base/notify/notice_queue.h"
#include "gui/l10n/l10n.h"
#include "hid/display/display.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/source_resources.h"
#include "storage/multi_range/multisample_range.h"

#include "support/song_fixture.h"
#include "support/torn_down_take.h"

// Before <cppspec.hpp>: it #defines `it`, which collides with every local named `it` in the project headers this
// drags in.
#include "support/card_access.h"

#include "support/menu_context.h"
#include "support/mocks/fake_surfaces.h"
#include "support/record_state.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>

namespace deluge::ui::scenes::spec::recorder_fx {

using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::recorder::kRecorderSceneKind;
using deluge::ui::scenes::recorder::recorder_scene;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;

/// @brief The minimum `Display` the model reaches for. `display` is null in a spec process
///        (`hid/display/display.cpp`'s definition), and `AudioEngine::getNewRecorder`'s failure paths and
///        `Kit::makeDrumNameUnique` both go through it.
struct RecorderDisplay final : deluge::hid::Display {
	constexpr size_t getNumBrowserAndMenuLines() override { return 3; }
	void displayPopup(char const*, int8_t, bool, uint8_t, int32_t, PopupType) override {}
	void popupText(char const*, PopupType) override {}
	void popupTextTemporary(char const*, PopupType) override {}
	void cancelPopup() override {}
	void freezeWithError(char const*) override {}
	void displayError(Error) override {}
	void removeWorkingAnimation() override { loading = false; } // one animation on the OLED (`oled.h`)
	void displayLoadingAnimationText(char const*, bool, bool) override { loading = true; }
	void removeLoadingAnimation() override { loading = false; }
	bool hasPopup() override { return false; }
	bool hasPopupOfType(PopupType) override { return false; }
	void consoleText(char const*) override {}
	void timerRoutine() override {}

	bool loading = false; ///< a loading animation is up
};

/// @brief A synth song whose source 0 has one multisample range for the recording to land in.
struct Rig {
	spec::SongFixture song = spec::make_synth_song();
	Sound* sound = nullptr;
	SourceResources* source = nullptr;
	MultisampleRange* range = nullptr;

	const BoardProfile& profile;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	Shell shell;

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	/// @brief Tear the stack down, so the scene's `on_exit` always runs.
	///
	/// **Scenes are process singletons.** An example that left the recorder armed would make the
	/// NEXT example's `on_enter` refuse (`recording()`) and cascade into a wall of
	/// unrelated failures. Popping
	/// here means every example starts from the same place whatever the one before it did, so a
	/// mutation kills the assertions that actually see it and no others.
	~Rig() {
		while (on_stack()) {
			shell.stack().pop(deluge::ui::shell::SceneKind::Null);
		}
		spec::settle_torn_down_take(); // a take ended with its screen keeps the next one out until it is freed
		currentSong = saved_song;
		menu_scene().reset_process_state(); // each example starts with the rows a boot has
	}

	explicit Rig(const DelugeBoard& board = deluge::ui::shell::kDelugeBoard, const BoardProfile& p = deluge_profile())
	    : profile(p), fake(board, p), shell(fake, p, reg, deluge::ui::shell::Services{}, ShellConfig{}) {
		currentSong = song.song; // the app's ports resolve addresses in the current song
		sound = deluge::model::effects_owner_of(deluge::model::owner_of(*song.instrument))->sound;
		source = &sound->sourceResources[0];
		deluge::test::set(*sound->state_, "osc1/type", "square"); // so "became SAMPLE" is a real observation
		sound->deriveConfig();
		range = static_cast<MultisampleRange*>(sound->getOrCreateFirstRange(0));
		deluge::edits::recordRanges(*sound, 0); // the range made here is the record's, as a gesture records it
	}

	MenuContext ctx() {
		MenuContext c = spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false,
		                             /*kit_global_fx=*/false);
		c.range_index = 0;
		return c;
	}

	/// @brief Open the recorder, the way `osc::AudioRecorder::beginSession` does.
	/// @param sound_writes Where the scene's sound edits go; the app's when null.
	void open(bind::SoundWrites* sound_writes = nullptr) {
		MenuContext c = ctx();
		if (sound_writes != nullptr) {
			c.ports.sound_writes = sound_writes;
		}
		recorder_scene().open(c);
		shell.init(SceneId{kRecorderSceneKind, 0});
	}

	/// @brief Open the recorder on @p c, a context the example has changed.
	void open_on(const MenuContext& c) {
		recorder_scene().open(c);
		shell.init(SceneId{kRecorderSceneKind, 0});
	}

	/// @brief The sound's slot.
	deluge::project::SlotAddress slot() { return *ctx().addresses.slot; }

	/// @brief Open the recorder ON TOP of the sound menu, which is where a node actually opens it.
	///
	/// The stack the LED example needs: `LedCompose::compose` runs bottom to top and the last
	/// writer wins, so "the recorder's SYNTH/KIT answer beats the menu's" is only a claim about a
	/// stack with both scenes on it.
	void open_over_menu() {
		const MenuContext c = ctx();
		menu_scene().open(*menu::MenuScene::roots()[static_cast<size_t>(MenuContext::Root::Sound)], c);
		shell.init(SceneId{deluge::ui::shell::SceneKind::FirstApp, 0});
		recorder_scene().open(c);
		(void)shell.stack().push(SceneId{kRecorderSceneKind, 0});
	}

	void tick(uint32_t at_ms) { shell.tick(at_ms); }

	/// @brief Press or release a button. `ButtonId` -> board matrix coordinate, so the shell's own lowering runs
	///        rather than a synthesised `UiEvent`.
	void button(ButtonId b, bool down, uint32_t at_ms) {
		const std::span<const ButtonId> matrix = profile.button_by_matrix;
		for (size_t i = 0; i < matrix.size(); i++) {
			if (matrix[i] != b) {
				continue;
			}
			const auto x = static_cast<uint8_t>(i % profile.button_cols);
			const auto y = static_cast<uint8_t>(i / profile.button_cols);
			shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(down ? 255 : 0)},
			                     at_ms);
			return;
		}
	}
	/// @brief Press a button down. `AudioRecorder::buttonAction` acts on the press and refuses the release.
	void press(ButtonId b, uint32_t at_ms) { button(b, true, at_ms); }
	/// @brief Release a button.
	void release(ButtonId b, uint32_t at_ms) { button(b, false, at_ms); }
	/// @brief Press or release grid pad (@p x, @p y).
	void pad(uint8_t x, uint8_t y, bool down, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_PAD, x, y, static_cast<int16_t>(down ? 64 : 0)}, at_ms);
	}
	/// @brief Open the sound menu on @p node with the horizontal-menus feature on, as the horizontal-menus card has it.
	void open_menu(const menu::Node& node) {
		MenuContext c = ctx();
		c.horizontal_menus = true;
		menu_scene().open(node, c);
		shell.init(SceneId{deluge::ui::shell::SceneKind::FirstApp, menu::node_id(node)});
	}
	/// @brief Is a scene of @p kind on the stack?
	bool has(deluge::ui::shell::SceneKind kind) {
		for (const deluge::ui::shell::Scene* s : shell.stack().scenes()) {
			if (s->kind() == kind) {
				return true;
			}
		}
		return false;
	}

	bool on_stack() {
		for (const deluge::ui::shell::Scene* s : shell.stack().scenes()) {
			if (s->kind() == kRecorderSceneKind) {
				return true;
			}
		}
		return false;
	}

	std::string oled() { return fake.pbm(0); }

	Song* const saved_song = currentSong;
};

// --- process-global guards -----------------------------------------------------------------
//
// **Every process global this spec touches is held in an RAII guard, and the guards REFUSE rather
// than repair.** `QuietAudio` (`support/card_access.h`) is the shape; the addition here is the
// refusal, and it is not fastidiousness -- a silently-repairing reset hides a broken assertion: a
// quiet-tick example covered for by an unrelated repaint, a `~Rig` that leaks into the next example,
// or a missing clear that `before_each` performs on the code's behalf.
//
// `before_each`/`after_each` take a `std::function<void()>` with no `self`
// (CppSpec's description header), so `expect` is not available in them. A named reason on
// stderr plus `std::abort()` is what "loudly" can mean there, and it fails the suite non-zero.

[[noreturn]] inline void spec_bail(const char* what) {
	std::fprintf(stderr, "recorder_scene_spec: %s\n", what);
	std::abort();
}

/// @brief Hold the foreground-recording publication clear for the scope -- and refuse to run, or to
///        finish, if it is not.
///
/// The foreground recording's channel and recorder (`AudioEngine::foregroundRecordingSource()` /
/// `foregroundRecorder()`) are process globals many subsystems read, and the host's take port holds its take for the
/// process, so an example that leaks one poisons the next; clearing it silently is what hid the leak in the first
/// place.
struct NoForegroundRecording {
	NoForegroundRecording() { check("a foreground recording was already published when an example started"); }
	~NoForegroundRecording() { check("an example finished with a foreground recording still published"); }
	NoForegroundRecording(const NoForegroundRecording&) = delete;
	NoForegroundRecording& operator=(const NoForegroundRecording&) = delete;
	static void check(const char* what) {
		if (AudioEngine::foregroundRecordingSource() != AudioInputChannel::NONE
		    || AudioEngine::foregroundRecorder() != nullptr || deluge::gui::shell_host::input_take() != nullptr) {
			spec_bail(what);
		}
	}
};

/// @brief Empty the notice queue and the dirty set on the way in.
///
/// @note **Deliberately not symmetric, and this is the convention rather than an oversight.** The
///       queue cannot be asserted empty on the way out: `~Rig` pops the stack, `RecorderScene::on_exit`
///       legitimately raises `ClearWorking`, and that notice is still queued when the example ends.
///       Draining on entry is isolation (it decides what the NEXT example starts from); asserting on
///       exit would be asserting something untrue.
struct DrainedNotices {
	DrainedNotices() {
		deluge::base::notify::Notice discard;
		while (deluge::base::notify::notices().pop(discard)) {}
		deluge::base::notify::dirty().drain();
	}
	~DrainedNotices() = default;
	DrainedNotices(const DrainedNotices&) = delete;
	DrainedNotices& operator=(const DrainedNotices&) = delete;
};

/// @brief Select the built-in English strings for the scope, and put the pointer back.
///
/// `Action::Kind::Popup` resolves through `ui::scenes::text_lookup` -> `l10n::getView`, which
/// dereferences `chosenLanguage` unguarded; a spec process leaves it null.
struct EnglishL10n {
	const deluge::l10n::Language* saved = deluge::l10n::chosenLanguage;
	EnglishL10n() { deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english; }
	~EnglishL10n() { deluge::l10n::chosenLanguage = saved; }
	EnglishL10n(const EnglishL10n&) = delete;
	EnglishL10n& operator=(const EnglishL10n&) = delete;
};

/// @brief Every guard an example needs, in one declaration.
///
/// Declare it FIRST in an example, before the `Rig`: members are destroyed in reverse declaration
/// order and the `Rig` (declared after) tears the scene stack down, so `RecorderScene::on_exit` has
/// already run by the time `NoForegroundRecording` checks.
struct Guards {
	/// Runs after every later local has torn its stack down (the global guest's among them), and before
	/// `publication` checks.
	~Guards() { spec::settle_torn_down_take(); }
	Guards() = default;
	Guards(const Guards&) = delete;
	Guards& operator=(const Guards&) = delete;

	deluge::ui::scenes::spec::QuietAudio quiet;
	deluge::ui::scenes::spec::NoSdRoot no_card;
	DrainedNotices notices;
	EnglishL10n l10n;
	NoForegroundRecording publication;
};

/// @brief Hold `AudioEngine::audioSampleTimer`, the clock the threshold screen blinks by, at a time in
///        milliseconds for the scope, and put it back.
struct AudioClockAt {
	uint32_t saved = AudioEngine::audioSampleTimer;
	explicit AudioClockAt(uint32_t ms) { set(ms); }
	~AudioClockAt() { AudioEngine::audioSampleTimer = saved; }
	void set(uint32_t ms) { AudioEngine::audioSampleTimer = static_cast<uint32_t>(uint64_t{ms} * kSampleRate / 1000); }
	AudioClockAt(const AudioClockAt&) = delete;
	AudioClockAt& operator=(const AudioClockAt&) = delete;
};

} // namespace deluge::ui::scenes::spec::recorder_fx
