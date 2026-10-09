/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "scenarios/stale_ui_scenario.h"

#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/ui/browser/browser.h"
#include "gui/ui/load/instrument_load.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/button.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/encoder_input.h"
#include "hid/encoders.h"
#include "hid/matrix/matrix_driver.h"
#include "io/file.hpp"
#include "io/midi/midi_engine.h"
#include "model/action/action_logger.h"
#include "model/clip/clip.h"
#include "model/clip/clip_array.h"
#include "model/instrument/instrument.h"
#include "model/instrument/melodic_instrument.h"
#include "model/model_stack.h"
#include "model/output.h"
#include "model/song/song.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"
#include "playback/render_requests.h"
#include "util/misc.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>

// See staging_release_scenario.cpp: the app's printf alias has no provider in this harness.
#ifdef printf
#undef printf
#endif

// MIDI follow's stored clips, one per note, for the note-off (io/midi/midi_follow.cpp).
extern Clip* clipForLastNoteReceived[kMaxMIDIValue + 1];

// The harness's DIN note script (audio/host.rs).
extern "C" {
int32_t deluge_host_midi_script_start(const char* path);
uint32_t deluge_host_midi_script_released(void);
}

namespace {

/// A UI that counts every call made into it after it has left the UI stack. Anything that reaches it then is acting
/// on a UI the user can no longer see, against whatever song it was opened over.
class StaleProbeUI final : public UI {
public:
	int32_t stale = 0;
	int32_t calls = 0;

	ActionResult padAction(int32_t, int32_t, int32_t) override {
		note();
		return ActionResult::DEALT_WITH;
	}
	ActionResult buttonAction(deluge::hid::Button, bool) override {
		note();
		return ActionResult::NOT_DEALT_WITH;
	}
	ActionResult horizontalEncoderAction(int32_t) override {
		note();
		return ActionResult::DEALT_WITH;
	}
	ActionResult verticalEncoderAction(int32_t) override {
		note();
		return ActionResult::DEALT_WITH;
	}
	void selectEncoderAction(int8_t) override { note(); }
	void modEncoderAction(int32_t, int32_t) override { note(); }
	void modButtonAction(uint8_t, bool) override { note(); }
	void modEncoderButtonAction(uint8_t, bool) override { note(); }
	void graphicsRoutine() override { note(); }
	ActionResult timerCallback() override {
		note();
		return ActionResult::DEALT_WITH;
	}
	ActionResult exitUI() override {
		note();
		return ActionResult::DEALT_WITH;
	}
	void scrollFinished() override { note(); }
	bool renderMainPads(uint32_t, RGB image[][kDisplayWidth + kSideBarWidth], uint8_t[][kDisplayWidth + kSideBarWidth],
	                    bool) override {
		if (image != nullptr) {
			note();
		}
		return true;
	}
	void renderOLED(deluge::hid::display::oled_canvas::Canvas&) override { note(); }
	UIType getUIType() override { return UIType::NONE; }

private:
	void note() {
		calls++;
		if (!isUIOpen(this)) {
			stale++;
		}
	}
};

StaleProbeUI g_probe;

enum class Stage : uint8_t {
	Loading,       ///< The blank song's preset load.
	Listing,       ///< The load browser lists SONGS, as a song loaded from it leaves it.
	InertRun,      ///< inert_while_empty: a probe UI on top, MIDI NEXT_SONG, input and timers during the load.
	EditorSetup,   ///< retired_song: the loaded song is given a synth clip.
	EditorRun,     ///< ...the sound editor on top, NEXT_SONG, and the swap watched.
	AuditionSetup, ///< held_audition: an audition pad held on the clip view when NEXT_SONG arrives.
	AuditionRun,   ///< ...the note must end when the UIs are taken down, not at the swap.
	NoteSetup,     ///< tapped_note: a note pad tapped on the clip view when NEXT_SONG arrives.
	NoteRun,       ///< ...the undo action its release records must not outlive the outgoing song.
	HoldStart,     ///< hold_load_to_launch: LOAD pressed in the load browser and held through the load.
	HoldRelease,   ///< ...released once the load waits on it; the swap must arm and the load end.
	FailedRun,     ///< failed_load: NEXT_SONG onto a song that cannot be read; the surviving song gets its UI back.
	ArrangeSetup,  ///< arrangement_record: an audio clip recorded into the arranger, the knobs on its recording.
	ArrangeRun,    ///< ...the render moves the recording on to a new clip at the loop end; the knobs must follow.
	Report,
	Done,
};

/// Steps the blank song's preset load is given. A step is about two virtual milliseconds.
constexpr int32_t kMaxLoadSteps = 20000;
/// Steps a listing, or a whole song load, is given to settle.
constexpr int32_t kMaxSettleSteps = 20000;

constexpr const char* kSongA = "SONGS/SUA.XML";
constexpr const char* kSongB = "SONGS/SUB.XML";
constexpr const char* kSongText = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<song\n\tfirmwareVersion=\"c1.3.0\"\n"
                                  "\tearliestCompatibleFirmware=\"4.1.0-alpha\"\n>\n</song>\n";
/// A song whose parse fails after the load has taken every UI down: its one clip names an instrument it does not
/// have, which the song's read reports as corrupt.
constexpr const char* kUnreadableSongText =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<song\n\tfirmwareVersion=\"c1.3.0\"\n"
    "\tearliestCompatibleFirmware=\"4.1.0-alpha\"\n>\n\t<sessionClips>\n"
    "\t\t<instrumentClip instrumentPresetName=\"SUMISSING\" />\n\t</sessionClips>\n</song>\n";
constexpr uint8_t kNextSongNote = 0x30;
/// The clip view's audition column.
constexpr int32_t kAuditionX = kDisplayWidth + 1;

Stage g_stage = Stage::Loading;
bool g_stepDone = false;
int32_t g_failures = 0;
int32_t g_steps = 0;

std::string g_scriptPath;
int32_t g_events = 0;
Song* g_songBefore = nullptr;
Clip* g_clipBefore = nullptr;
/// Compared by address only: once the old song is freed, nothing it held may be dereferenced.
const void* g_modBefore = nullptr;
bool g_loadSeen = false;

/// inert_while_empty: what was delivered while the stack was empty, and what reached a UI from before the load.
int32_t g_emptySteps = 0;
int32_t g_staleCurrent = 0;
bool g_repeatsTried = false;
bool g_repeatsMoved = false;
/// The armed swap's LOADING popup, and whether a knob turn left it up.
bool g_popupTried = false;
bool g_popupKept = false;
/// A mod encoder button pressed before the swap and released after it.
bool g_modButtonPressed = false;
bool g_modButtonReleased = false;

/// retired_song: steps seen before and after the swap with the stack empty, and what they found.
int32_t g_preSwapSteps = 0;
int32_t g_postSwapSteps = 0;
int32_t g_staleEditor = 0;
int32_t g_staleView = 0;

/// hold_load_to_launch.
bool g_unarmedSeen = false;

/// held_audition: the instrument auditioned (compared, and read only while its song is current), and what the first
/// step with the stack empty found.
Instrument* g_auditioned = nullptr;
bool g_auditionChecked = false;
bool g_padStillPressed = false;
bool g_stillAuditioning = false;
/// arrangement_record: the session clip recorded from, the arrangement-only clip the knobs were put on, and what the
/// render moved the recording on to.
Clip* g_recordedFrom = nullptr;
Clip* g_recordingInto = nullptr;
Clip* g_movedTo = nullptr;

/// View::renderStackWrites() when the running leg began, so a leg's line says which leg the render wrote in.
int32_t g_stackWritesAtLegStart = 0;

/// retired_song, swap_work: the note MIDI follow is given a stored clip of the outgoing song for, the swap's
/// instrument-load notifications before the leg, and what the first interaction-tier entry after the swap found.
constexpr int32_t kPlantedNote = 60;
int32_t g_madeCurrentAtLegStart = 0;
bool g_swapWorkChecked = false;
int32_t g_staleFollowClips = 0;
uint32_t g_modeAfterSwap = 0;
bool g_animationAfterSwap = false;
int32_t g_madeCurrentAfterSwap = 0;
/// swap_work, dropped_swap: a note an external MIDI keyboard auditions on the outgoing song's instrument while the load
/// waits on its armed swap, and whether the outgoing song was still auditioning once the swap's interaction side ran.
constexpr int32_t kExternalNote = 72;
constexpr int32_t kExternalChannel = 1;
bool g_externalAuditioned = false;
int32_t g_outgoingAuditioning = -1;
/// dropped_swap: the queue is flooded at the swap, so only the swap's own slot carries it.
int32_t g_overflowsAtLegStart = 0;
bool g_dropAuditioned = false;
int32_t g_dropOutgoingAuditioning = -1;

/// The note pad tapped just before NEXT_SONG: its release deletes the note and records an undo action.
constexpr int32_t kNoteX = 0;
constexpr int32_t kNoteY = 3;
bool g_noteTapped = false;

void fail(const char* marker, const char* what) {
	printf("%s FAIL %s\n", marker, what);
	g_failures++;
}

bool stackEmpty() {
	return getRootUI() == nullptr;
}

/// An external MIDI keyboard's note on @p output (a melodic instrument of the current song), as MIDI input auditions
/// it; returns whether it is auditioning.
bool auditionFromExternalMidi(Output* output) {
	if (output == nullptr || output->type != OutputType::SYNTH) {
		return false;
	}
	auto* instrument = static_cast<MelodicInstrument*>(output);
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	instrument->beginAuditioningForNote(modelStack, kExternalNote, 100, zeroMPEValues, kExternalChannel);
	return instrument->isNoteAuditioning(kExternalNote);
}

/// MIDI follow's stored clips that are not one of the current song's: each would send a note-off into another song.
/// Compared by address only.
int32_t staleFollowClips() {
	int32_t stale = 0;
	for (Clip* stored : clipForLastNoteReceived) {
		if (stored == nullptr) {
			continue;
		}
		const bool ofCurrentSong = std::ranges::contains(currentSong->sessionClips, stored)
		                           || std::ranges::contains(currentSong->arrangementOnlyClips, stored);
		stale += ofCurrentSong ? 0 : 1;
	}
	return stale;
}

void writeSong(const char* path, std::string_view text = kSongText) {
	if (auto file = deluge::io::File::open(path, DELUGE_FILE_WRITE_CREATE); file.has_value()) {
		(void)file->write(std::as_bytes(std::span{text.data(), text.size()}));
	}
}

/// The DIN script: one press of the note the learned NEXT_SONG command listens for.
std::string writeNextSongScript() {
	std::error_code error;
	const std::filesystem::path path =
	    std::filesystem::temp_directory_path(error) / ("deluge-stale-ui-" + std::to_string(::getpid()) + ".notes");
	if (error) {
		return {};
	}
	FILE* file = std::fopen(path.c_str(), "w");
	if (file == nullptr) {
		return {};
	}
	std::fprintf(file, "0 90 %02X 64\n2000 80 %02X 00\n", kNextSongNote, kNextSongNote);
	std::fclose(file);
	return path.string();
}

/// The clip view in front of the song's first clip.
Clip* frontClip(const char* marker) {
	Clip* clip = currentSong->sessionClips.empty() ? nullptr : currentSong->sessionClips[0];
	if (clip == nullptr || clip->output == nullptr || clip->output->type != OutputType::SYNTH) {
		fail(marker, "the song has no synth clip");
		return nullptr;
	}
	currentSong->setCurrentClip(clip);
	changeRootUI(&instrumentClipView);
	return clip;
}

/// A load is over once its completion has run and the song it brought in is in front of a root UI.
bool loadOver() {
	return !loadSongUI.loadInFlight() && currentSong != g_songBefore && !stackEmpty();
}

/// Every kind of input and UI timer the app delivers, through the app's own dispatchers.
void deliverEverything() {
	using namespace deluge::hid;
	(void)Buttons::buttonAction(button::SYNTH, true);
	(void)Buttons::buttonAction(button::SYNTH, false);
	(void)matrixDriver.padAction(3, 3, 64);
	(void)matrixDriver.padAction(3, 3, 0);
	encoders::functionEncoderAt(1).applyEdges(4); // scrollX
	encoders::functionEncoderAt(0).applyEdges(4); // scrollY
	encoders::modEncoderAt(0).applyEdges(1);
	(void)encoders::interpretEncoders();
	uiTimerManager.setTimer(TimerName::UI_SPECIFIC, 1);
	uiTimerManager.setTimer(TimerName::BACK_MENU_EXIT, 1);
}

void stepLoading() {
	if (currentSong->blankPresetPending || deluge::gui::instrumentLoad.acquisitionInFlight()) {
		if (++g_steps > kMaxLoadSteps) {
			fail("SU-SETUP", "the blank song's preset load never finished");
			g_stage = Stage::Report;
		}
		return;
	}
	if (frontClip("SU-SETUP") == nullptr) {
		g_stage = Stage::Report;
		return;
	}
	(void)deluge::io::mkdir("SONGS");
	writeSong(kSongA);
	writeSong(kSongB);
	currentSong->setSongFullPath(kSongA);
	(void)openUI(&loadSongUI);
	g_steps = 0;
	g_stage = Stage::Listing;
}

/// The browser's listing is what NEXT_SONG walks. It is left the way a song loaded from the browser leaves it.
void stepListing() {
	if (Browser::isListingInProgress()) {
		loadSongUI.graphicsRoutine();
		if (++g_steps > kMaxSettleSteps) {
			fail("SU-SETUP", "the load browser's listing never finished");
			g_stage = Stage::Report;
		}
		return;
	}
	for (int32_t t = 0; currentUIMode == UI_MODE_VERTICAL_SCROLL && t < kDisplayHeight * 4; t++) {
		(void)loadSongUI.timerCallback();
	}
	FileItem* const item = Browser::getCurrentFileItem();
	const bool listed = item != nullptr && !item->isFolder;
	closeUI(&loadSongUI);
	currentUIMode = UI_MODE_NONE;
	if (!listed) {
		fail("SU-SETUP", "the load browser did not list a song for NEXT_SONG to move on to, so nothing was tested");
		g_stage = Stage::Report;
		return;
	}
	auto& command = midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::NEXT_SONG)];
	command.cable = nullptr;
	command.channelOrZone = 0;
	command.noteOrCC = kNextSongNote;

	// inert_while_empty: NEXT_SONG moves on only while the song plays.
	playbackHandler.setupPlaybackUsingInternalClock(0, false);
	if (frontClip("SU-INERT") == nullptr || !openUI(&g_probe) || getCurrentUI() != &g_probe) {
		fail("SU-INERT", "the probe UI could not be opened over the clip view");
		g_stage = Stage::Report;
		return;
	}
	g_probe.stale = 0;
	g_probe.calls = 0;
	g_songBefore = currentSong;
	g_scriptPath = writeNextSongScript();
	g_events = g_scriptPath.empty() ? -1 : deluge_host_midi_script_start(g_scriptPath.c_str());
	if (g_events <= 0) {
		fail("SU-INERT", "the DIN script could not be written or armed");
		g_stage = Stage::Report;
		return;
	}
	g_steps = 0;
	g_stage = Stage::InertRun;
}

/// While the load has the stack empty, deliver every kind of input and timer; none may reach the probe.
void stepInertRun() {
	g_loadSeen = g_loadSeen || loadSongUI.loadInFlight();
	if (loadSongUI.loadInFlight() && stackEmpty()) {
		g_emptySteps++;
		if (getCurrentUI() == &g_probe) {
			g_staleCurrent++;
		}
		deliverEverything();
		// The armed swap's loop count is the select encoder's to change, whichever UI the load took down.
		if (!g_repeatsTried && currentUIMode == UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED) {
			g_repeatsTried = true;
			const int32_t before = session.numRepeatsTilLaunch;
			deluge::hid::encoders::functionEncoderAt(3).applyEdges(4);
			(void)deluge::hid::encoders::interpretEncoders();
			g_repeatsMoved = session.numRepeatsTilLaunch == before + 1;
			// That put the LOADING popup up; a knob turn on the still-playing song must leave it there.
			g_popupTried = true;
			deluge::hid::encoders::modEncoderAt(0).applyEdges(1);
			(void)deluge::hid::encoders::interpretEncoders();
			g_popupKept = display->hasPopupOfType(PopupType::LOADING);
		}
		// A mod encoder button held across the swap: pressed on the playing song, released once it is swapped out.
		const bool swapped = currentSong != g_songBefore;
		if (!swapped && !g_modButtonPressed) {
			g_modButtonPressed = true;
			(void)Buttons::buttonAction(deluge::hid::button::MOD_ENCODER_0, true);
		}
		else if (swapped && g_modButtonPressed && !g_modButtonReleased) {
			g_modButtonReleased = true;
			(void)Buttons::buttonAction(deluge::hid::button::MOD_ENCODER_0, false);
		}
	}
	if (!loadOver()) {
		if (++g_steps > kMaxSettleSteps) {
			fail("SU-INERT", "the MIDI NEXT_SONG load never ended");
			g_stage = Stage::Report;
		}
		return;
	}
	printf("SU-INERT loadSeen=%d emptySteps=%d staleCurrent=%d probeStale=%d probeCalls=%d modButton=%d/%d steps=%d\n",
	       g_loadSeen ? 1 : 0, g_emptySteps, g_staleCurrent, g_probe.stale, g_probe.calls, g_modButtonPressed ? 1 : 0,
	       g_modButtonReleased ? 1 : 0, g_steps);
	if (!g_loadSeen || g_emptySteps == 0) {
		fail("SU-INERT", "no step saw the load with the UI stack empty, so nothing was tested");
	}
	if (!g_modButtonReleased) {
		fail("SU-INERT", "no mod encoder button was held across the swap, so that was not tested");
	}
	if (g_staleCurrent != 0) {
		fail("SU-INERT", "getCurrentUI() reported a UI from before the load while the stack was empty");
	}
	if (g_probe.stale != 0) {
		fail("SU-INERT", "input or a UI timer reached a UI the load had taken down");
	}
	printf("SU-REPEATS tried=%d moved=%d repeats=%d\n", g_repeatsTried ? 1 : 0, g_repeatsMoved ? 1 : 0,
	       static_cast<int>(session.numRepeatsTilLaunch));
	if (!g_repeatsTried) {
		fail("SU-REPEATS", "the swap was never armed while the stack was empty, so nothing was tested");
	}
	else if (!g_repeatsMoved) {
		fail("SU-REPEATS", "the select encoder did not change the armed swap's loop count");
	}
	printf("SU-POPUP tried=%d kept=%d\n", g_popupTried ? 1 : 0, g_popupKept ? 1 : 0);
	// The knobs went on adjusting the outgoing song while it played, before and across the swap; an action they
	// logged would revert into that song after the load freed it.
	const bool undoEmpty = actionLogger.firstAction[0] == nullptr && actionLogger.firstAction[1] == nullptr;
	printf("SU-KNOBUNDO undoEmpty=%d\n", undoEmpty ? 1 : 0);
	if (!undoEmpty) {
		fail("SU-KNOBUNDO", "an action logged by a knob turn on the outgoing song survived the load");
	}
	if (g_popupTried && !g_popupKept) {
		fail("SU-POPUP", "a knob turn during the load replaced its LOADING popup");
	}
	if (isUIOpen(&g_probe)) {
		fail("SU-INERT", "the probe UI is still on the stack after the load");
	}
	g_steps = 0;
	g_stage = Stage::EditorSetup;
}

/// The song NEXT_SONG brought in has no clips; give it the built-in default synth clip a new song starts with. The
/// preset that would replace that synth is not asked for: its arrival would swap the clip's output mid-leg.
void stepEditorSetup() {
	if (currentSong->sessionClips.empty()) {
		(void)currentSong->ensureAtLeastOneSessionClip();
		currentSong->blankPresetPending = false;
	}
	Clip* clip = frontClip("SU-RETIRED");
	if (clip == nullptr) {
		g_stage = Stage::Report;
		return;
	}
	if (!playbackHandler.isEitherClockActive()) {
		playbackHandler.setupPlaybackUsingInternalClock(0, false);
	}
	if (!soundEditor.setup(clip) || !openUI(&soundEditor) || getCurrentUI() != &soundEditor) {
		fail("SU-RETIRED", "the sound editor could not be opened on the synth clip");
		g_stage = Stage::Report;
		return;
	}
	g_songBefore = currentSong;
	g_clipBefore = clip;
	g_modBefore = clip->output->toModControllable();
	g_stackWritesAtLegStart = View::renderStackWrites();
	// swap_work: a note-on MIDI follow sent to the outgoing song's clip, whose note-off has not come yet.
	clipForLastNoteReceived[kPlantedNote] = clip;
	g_madeCurrentAtLegStart =
	    deluge::playback::interactionOwnedWrites(deluge::playback::InteractionOwned::InstrumentLoadNotified);
	(void)deluge::playback::outgoingAuditioningAfterLastSwap();
	g_loadSeen = false;
	currentSong->loadNextSong();
	g_steps = 0;
	g_stage = Stage::EditorRun;
}

/// Once the outgoing song has been swapped out it is on its way to being freed; nothing the empty stack reaches may
/// still point into it.
void stepEditorRun() {
	g_loadSeen = g_loadSeen || loadSongUI.loadInFlight();
	if (loadSongUI.loadInFlight() && stackEmpty()) {
		const bool swapped = currentSong != g_songBefore;
		(swapped ? g_postSwapSteps : g_preSwapSteps)++;
		// swap_work: while the load waits on the armed swap, an external keyboard plays the outgoing song's synth.
		if (!swapped && !g_externalAuditioned && currentUIMode == UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED) {
			g_externalAuditioned = auditionFromExternalMidi(g_clipBefore->output);
		}
		if (getCurrentUI() == &soundEditor) {
			g_staleEditor++;
		}
		if (swapped
		    && (view.activeModControllableModelStack.getTimelineCounterAllowNull() == g_clipBefore
		        || view.activeModControllableModelStack.modControllable == g_modBefore)) {
			g_staleView++;
		}
		// swap_work: the first interaction-tier entry after an armed swap the render made. Each is one, as every
		// app-loop job is, so what it finds is what any MIDI handler or other job would.
		if (swapped && !g_swapWorkChecked) {
			g_swapWorkChecked = true;
			g_staleFollowClips = staleFollowClips();
			g_modeAfterSwap = currentUIMode;
			g_animationAfterSwap = deluge::hid::display::OLED::isWorkingAnimationPresent();
			g_madeCurrentAfterSwap =
			    deluge::playback::interactionOwnedWrites(deluge::playback::InteractionOwned::InstrumentLoadNotified)
			    - g_madeCurrentAtLegStart;
			g_outgoingAuditioning = deluge::playback::outgoingAuditioningAfterLastSwap();
		}
	}
	if (!loadOver()) {
		if (++g_steps > kMaxSettleSteps) {
			fail("SU-RETIRED", "the NEXT_SONG load never ended");
			g_stage = Stage::Report;
		}
		return;
	}
	printf("SU-RETIRED loadSeen=%d preSwap=%d postSwap=%d staleEditor=%d staleView=%d renderWrites=%d steps=%d\n",
	       g_loadSeen ? 1 : 0, g_preSwapSteps, g_postSwapSteps, g_staleEditor, g_staleView,
	       View::renderStackWrites() - g_stackWritesAtLegStart, g_steps);
	if (g_preSwapSteps == 0 || g_postSwapSteps == 0) {
		fail("SU-RETIRED", "no step saw the stack empty on both sides of the swap, so nothing was tested");
	}
	if (g_staleEditor != 0) {
		fail("SU-RETIRED", "input would have reached the sound editor on the outgoing song while the stack was empty");
	}
	if (g_staleView != 0) {
		fail("SU-RETIRED", "the view still pointed into the swapped-out song");
	}
	const bool loadingMode = g_modeAfterSwap == UI_MODE_LOADING_SONG_NEW_SONG_PLAYING;
	printf("SU-SWAPWORK checked=%d staleFollowClips=%d loadingMode=%d animation=%d madeCurrent=%d "
	       "externalAudition=%d outgoingAuditioning=%d\n",
	       g_swapWorkChecked ? 1 : 0, g_staleFollowClips, loadingMode ? 1 : 0, g_animationAfterSwap ? 1 : 0,
	       g_madeCurrentAfterSwap, g_externalAuditioned ? 1 : 0, g_outgoingAuditioning);
	if (!g_swapWorkChecked) {
		fail("SU-SWAPWORK", "no step saw the load in flight after the swap, so nothing was tested");
	}
	else {
		if (g_staleFollowClips != 0) {
			fail("SU-SWAPWORK", "MIDI follow still stored a swapped-out song's clip for a note-off");
		}
		if (!loadingMode) {
			fail("SU-SWAPWORK", "the swap did not put the load in its new-song-playing mode");
		}
		if (!g_animationAfterSwap) {
			fail("SU-SWAPWORK", "the swap did not show the loading animation");
		}
		if (g_madeCurrentAfterSwap <= 0) {
			fail("SU-SWAPWORK", "the instrument load was not told a song was made current");
		}
		if (!g_externalAuditioned) {
			fail("SU-SWAPWORK", "no external MIDI audition was started on the outgoing song, so that was not tested");
		}
		else if (g_outgoingAuditioning != 0) {
			fail("SU-SWAPWORK", "the outgoing song was still auditioning after the swap's interaction side ran");
		}
	}
	g_steps = 0;
	g_stage = Stage::AuditionSetup;
}

/// An audition pad held on the clip view, on a playing song, when NEXT_SONG arrives.
void stepAuditionSetup() {
	if (currentSong->sessionClips.empty()) {
		(void)currentSong->ensureAtLeastOneSessionClip();
		currentSong->blankPresetPending = false;
	}
	Clip* clip = frontClip("SU-AUDITION");
	if (clip == nullptr) {
		g_stage = Stage::Report;
		return;
	}
	if (!playbackHandler.isEitherClockActive()) {
		playbackHandler.setupPlaybackUsingInternalClock(0, false);
	}
	g_auditioned = static_cast<Instrument*>(clip->output);
	(void)matrixDriver.padAction(kAuditionX, 0, 64);
	if (!instrumentClipView.auditionPadIsPressed[0] || !g_auditioned->isAnyAuditioningHappening()) {
		fail("SU-AUDITION", "the audition pad did not start an audition, so nothing was tested");
		(void)matrixDriver.padAction(kAuditionX, 0, 0);
		g_stage = Stage::Report;
		return;
	}
	g_songBefore = currentSong;
	g_loadSeen = false;
	// dropped_swap: the render's swap finds the request queue full.
	g_overflowsAtLegStart = deluge::playback::renderRequestOverflows();
	deluge::playback::floodRenderRequestsAtNextSwap();
	(void)deluge::playback::outgoingAuditioningAfterLastSwap();
	currentSong->loadNextSong();
	g_steps = 0;
	g_stage = Stage::AuditionRun;
}

void stepAuditionRun() {
	g_loadSeen = g_loadSeen || loadSongUI.loadInFlight();
	if (!g_auditionChecked && loadSongUI.loadInFlight() && stackEmpty() && currentSong == g_songBefore) {
		g_auditionChecked = true;
		g_padStillPressed = instrumentClipView.auditionPadIsPressed[0] != 0 || matrixDriver.isPadPressed(kAuditionX, 0);
		g_stillAuditioning = g_auditioned->isAnyAuditioningHappening();
	}
	if (loadSongUI.loadInFlight() && stackEmpty()) {
		if (currentSong == g_songBefore) {
			if (g_auditionChecked && !g_dropAuditioned
			    && currentUIMode == UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED) {
				g_dropAuditioned = auditionFromExternalMidi(g_auditioned);
			}
		}
		else if (g_dropOutgoingAuditioning < 0) {
			g_dropOutgoingAuditioning = deluge::playback::outgoingAuditioningAfterLastSwap();
		}
	}
	if (!loadOver()) {
		if (++g_steps > kMaxSettleSteps) {
			fail("SU-AUDITION", "the NEXT_SONG load never ended");
			g_stage = Stage::Report;
		}
		return;
	}
	// The finger lifts once the new song's UI is up.
	(void)matrixDriver.padAction(kAuditionX, 0, 0);
	printf("SU-AUDITION checked=%d padStillPressed=%d stillAuditioning=%d steps=%d\n", g_auditionChecked ? 1 : 0,
	       g_padStillPressed ? 1 : 0, g_stillAuditioning ? 1 : 0, g_steps);
	if (!g_auditionChecked) {
		fail("SU-AUDITION", "no step saw the stack empty before the swap, so nothing was tested");
	}
	if (g_padStillPressed || g_stillAuditioning) {
		fail("SU-AUDITION", "an audition held when the load took the UIs down was still sounding or pressed after it");
	}
	const int32_t overflows = deluge::playback::renderRequestOverflows() - g_overflowsAtLegStart;
	printf("SU-SWAPDROP overflows=%d externalAudition=%d outgoingAuditioning=%d loadEnded=1\n", overflows,
	       g_dropAuditioned ? 1 : 0, g_dropOutgoingAuditioning);
	if (overflows == 0 || !g_dropAuditioned) {
		fail("SU-SWAPDROP", "the swap did not overflow the queue with an external audition on, so nothing was tested");
	}
	else if (g_dropOutgoingAuditioning != 0) {
		fail("SU-SWAPDROP", "a swap whose place the full queue lost did not stop the outgoing song's auditioning");
	}
	g_steps = 0;
	g_stage = Stage::NoteSetup;
}

/// A note on the clip, then a tap on it that NEXT_SONG arrives in the middle of: the load's release of that pad is a
/// short tap on an existing note, which deletes it and records an undo action against the outgoing song.
void stepNoteSetup() {
	if (currentSong->sessionClips.empty()) {
		(void)currentSong->ensureAtLeastOneSessionClip();
		currentSong->blankPresetPending = false;
	}
	if (frontClip("SU-UNDO") == nullptr) {
		g_stage = Stage::Report;
		return;
	}
	if (!playbackHandler.isEitherClockActive()) {
		playbackHandler.setupPlaybackUsingInternalClock(0, false);
	}
	(void)matrixDriver.padAction(kNoteX, kNoteY, 64);
	(void)matrixDriver.padAction(kNoteX, kNoteY, 0);
	(void)matrixDriver.padAction(kNoteX, kNoteY, 64);
	g_noteTapped = matrixDriver.isPadPressed(kNoteX, kNoteY);
	g_songBefore = currentSong;
	g_loadSeen = false;
	currentSong->loadNextSong();
	g_steps = 0;
	g_stage = Stage::NoteRun;
}

void stepNoteRun() {
	g_loadSeen = g_loadSeen || loadSongUI.loadInFlight();
	if (!loadOver()) {
		if (++g_steps > kMaxSettleSteps) {
			fail("SU-UNDO", "the NEXT_SONG load never ended");
			g_stage = Stage::Report;
		}
		return;
	}
	(void)matrixDriver.padAction(kNoteX, kNoteY, 0);
	const bool undoEmpty = actionLogger.firstAction[0] == nullptr && actionLogger.firstAction[1] == nullptr;
	printf("SU-UNDO loadSeen=%d noteTapped=%d undoEmpty=%d steps=%d\n", g_loadSeen ? 1 : 0, g_noteTapped ? 1 : 0,
	       undoEmpty ? 1 : 0, g_steps);
	if (!g_noteTapped) {
		fail("SU-UNDO", "no note pad was held when NEXT_SONG arrived, so nothing was tested");
	}
	else if (!undoEmpty) {
		fail("SU-UNDO", "an undo action recorded against the outgoing song survived the load");
	}
	// hold_load_to_launch: the load browser, on the song now current.
	(void)openUI(&loadSongUI);
	g_steps = 0;
	g_stage = Stage::HoldStart;
}

/// LOAD pressed in the browser starts the load, and held, keeps the swap waiting for its release.
void stepHoldStart() {
	if (Browser::isListingInProgress()) {
		loadSongUI.graphicsRoutine();
		if (++g_steps > kMaxSettleSteps) {
			fail("SU-HOLD", "the load browser's listing never finished");
			g_stage = Stage::Report;
		}
		return;
	}
	for (int32_t t = 0; currentUIMode == UI_MODE_VERTICAL_SCROLL && t < kDisplayHeight * 4; t++) {
		(void)loadSongUI.timerCallback();
	}
	g_songBefore = currentSong;
	g_loadSeen = false;
	(void)Buttons::buttonAction(deluge::hid::button::LOAD, true);
	g_steps = 0;
	g_stage = Stage::HoldRelease;
}

void stepHoldRelease() {
	g_loadSeen = g_loadSeen || loadSongUI.loadInFlight();
	if (!g_unarmedSeen && currentUIMode == UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_UNARMED) {
		g_unarmedSeen = true;
		(void)Buttons::buttonAction(deluge::hid::button::LOAD, false);
	}
	if (!loadOver()) {
		if (++g_steps > kMaxSettleSteps) {
			printf("SU-HOLD loadSeen=%d unarmedSeen=%d mode=%u steps=%d\n", g_loadSeen ? 1 : 0, g_unarmedSeen ? 1 : 0,
			       currentUIMode, g_steps);
			fail("SU-HOLD", "the load held on LOAD never ended once LOAD was released");
			if (Buttons::isButtonPressed(deluge::hid::button::LOAD)) {
				(void)Buttons::buttonAction(deluge::hid::button::LOAD, false);
			}
			g_stage = Stage::Report;
		}
		return;
	}
	printf("SU-HOLD loadSeen=%d unarmedSeen=%d steps=%d\n", g_loadSeen ? 1 : 0, g_unarmedSeen ? 1 : 0, g_steps);
	if (!g_loadSeen || !g_unarmedSeen) {
		fail("SU-HOLD", "the load never waited on the held LOAD button, so nothing was tested");
	}
	// failed_load: whichever song NEXT_SONG moves on to cannot be read.
	writeSong(kSongA, kUnreadableSongText);
	writeSong(kSongB, kUnreadableSongText);
	if (!playbackHandler.isEitherClockActive()) {
		playbackHandler.setupPlaybackUsingInternalClock(0, false);
	}
	g_songBefore = currentSong;
	g_loadSeen = false;
	currentSong->loadNextSong();
	g_loadSeen = loadSongUI.loadInFlight();
	printf("SU-FAILED-START inFlight=%d stackEmpty=%d playing=%d\n", g_loadSeen ? 1 : 0, stackEmpty() ? 1 : 0,
	       playbackHandler.isEitherClockActive() ? 1 : 0);
	g_steps = 0;
	g_stage = Stage::FailedRun;
}

/// The load fails once it has taken every UI down; the song that survives it must be put back in front of the user.
void stepFailedRun() {
	g_loadSeen = g_loadSeen || loadSongUI.loadInFlight();
	if (loadSongUI.loadInFlight()) {
		if (++g_steps > kMaxSettleSteps) {
			fail("SU-FAILED", "the load of an unreadable song never ended");
			g_stage = Stage::Report;
		}
		return;
	}
	const bool survived = currentSong == g_songBefore;
	const bool restored = !stackEmpty() && getCurrentUI() == getRootUI();
	printf("SU-FAILED loadSeen=%d survived=%d restored=%d steps=%d\n", g_loadSeen ? 1 : 0, survived ? 1 : 0,
	       restored ? 1 : 0, g_steps);
	if (!g_loadSeen || !survived) {
		fail("SU-FAILED", "the unreadable song did not fail to load with the old song kept, so nothing was tested");
	}
	else if (!restored) {
		fail("SU-FAILED", "after a failed load the surviving song has no UI to take input");
	}
	g_steps = 0;
	g_stage = Stage::ArrangeSetup;
}

/// The session view's own gesture for recording into the arranger -- RECORD held, SESSION pressed, the clock
/// stopped -- with a new audio clip launched. The knobs are put on the clip, and a knob turn then moves them on to the
/// arrangement-only clip the arranger records it into (View::getModelStackWithParam()).
void stepArrangeSetup() {
	using namespace deluge::hid;
	if (playbackHandler.isEitherClockActive()) {
		playbackHandler.endPlayback();
	}
	if (currentSong->sessionClips.empty()) {
		(void)currentSong->ensureAtLeastOneSessionClip();
		currentSong->blankPresetPending = false;
	}
	changeRootUI(&sessionView);
	// A row past the last clip: the new clip goes on the end.
	Clip* clip = sessionView.createNewAudioClipForHarness(1000);
	if (clip == nullptr || getCurrentUI() != &sessionView) {
		fail("SU-ARRANGE", "no audio clip in front of the session view, so nothing was tested");
		g_stage = Stage::Report;
		return;
	}
	clip->activeIfNoSolo = true;
	(void)Buttons::buttonAction(button::RECORD, true);
	(void)Buttons::buttonAction(button::SESSION_VIEW, true);
	(void)Buttons::buttonAction(button::SESSION_VIEW, false);
	(void)Buttons::buttonAction(button::RECORD, false);
	if (playbackHandler.recording != RecordingMode::ARRANGEMENT || !playbackHandler.isEitherClockActive()) {
		fail("SU-ARRANGE", "RECORD and SESSION did not start recording into the arranger, so nothing was tested");
		g_stage = Stage::Report;
		return;
	}
	view.setActiveModControllableTimelineCounter(clip);
	encoders::modEncoderAt(0).applyEdges(1);
	(void)encoders::interpretEncoders();
	Clip* into = static_cast<Clip*>(view.activeModControllableModelStack.getTimelineCounterAllowNull());
	if (into == nullptr || into == clip || into->beingRecordedFromClip != clip) {
		fail("SU-ARRANGE", "the knob turn did not move the knobs on to the clip the arranger records into");
		g_stage = Stage::Report;
		return;
	}
	g_recordedFrom = clip;
	g_recordingInto = into;
	g_stackWritesAtLegStart = View::renderStackWrites();
	g_steps = 0;
	g_stage = Stage::ArrangeRun;
}

/// At the clip's loop end the render clones the clip it records from into a new arrangement-only clip and records on
/// into that. Once the interaction tier has run, the knobs must be on the new clip, with its params.
void stepArrangeRun() {
	Clip* active = g_recordedFrom->output->getActiveClip();
	if (active == g_recordingInto || active == nullptr || active->beingRecordedFromClip != g_recordedFrom) {
		if (++g_steps > kMaxSettleSteps) {
			fail("SU-ARRANGE", "the recording never moved on to a new clip, so nothing was tested");
			g_stage = Stage::Report;
		}
		return;
	}
	g_movedTo = active;
	ModelStackWithThreeMainThings const& stack = view.activeModControllableModelStack;
	const bool followed = stack.getTimelineCounterAllowNull() == g_movedTo;
	const bool params = stack.paramManager == &g_movedTo->paramManager;
	const bool output = stack.modControllable == g_movedTo->output->toModControllable();
	printf("SU-ARRANGE moved=1 followed=%d params=%d output=%d renderWrites=%d steps=%d\n", followed ? 1 : 0,
	       params ? 1 : 0, output ? 1 : 0, View::renderStackWrites() - g_stackWritesAtLegStart, g_steps);
	if (!followed || !params || !output) {
		fail("SU-ARRANGE", "the knobs did not follow the recording on to the new clip");
	}
	playbackHandler.endPlayback();
	g_stage = Stage::Report;
}

void report() {
	if (playbackHandler.isEitherClockActive()) {
		playbackHandler.endPlayback();
	}
	const int32_t renderWrites = View::renderStackWrites();
	View::watchRenderStackWrites(false);
	printf("SU-STACK renderWrites=%d\n", renderWrites);
	if (renderWrites != 0) {
		fail("SU-STACK",
		     "a render block changed the view's active model stack, which only the interaction tier writes");
	}
	using deluge::playback::InteractionOwned;
	using deluge::playback::renderInteractionWrites;
	const int32_t followWrites = renderInteractionWrites(InteractionOwned::MidiFollowStoredClips);
	const int32_t animationWrites = renderInteractionWrites(InteractionOwned::LoadingAnimation);
	const int32_t loadWrites = renderInteractionWrites(InteractionOwned::InstrumentLoadNotified);
	const int32_t modeWrites = renderInteractionWrites(InteractionOwned::UiMode);
	const int32_t flagWrites = renderInteractionWrites(InteractionOwned::LoadSongFlag);
	const int32_t auditionWrites = renderInteractionWrites(InteractionOwned::Auditioning);
	deluge::playback::watchRenderInteractionWrites(false);
	printf("SU-SWAP renderWrites midiFollow=%d animation=%d instrumentLoad=%d uiMode=%d loadFlag=%d audition=%d\n",
	       followWrites, animationWrites, loadWrites, modeWrites, flagWrites, auditionWrites);
	if (followWrites + animationWrites + loadWrites + modeWrites + flagWrites + auditionWrites != 0) {
		fail("SU-SWAP", "the render wrote state only the interaction tier writes");
	}
	if (!g_scriptPath.empty()) {
		std::remove(g_scriptPath.c_str());
	}
	printf("SU-RESULT failures=%d\n", g_failures);
	g_stage = Stage::Done;
}

} // namespace

extern "C" {

void deluge_scenario_stale_ui_begin(void) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Loading;
	g_stepDone = false;
	g_failures = 0;
	g_steps = 0;
	// Every render block of the run, both armed swaps and the arranger recording among them.
	View::watchRenderStackWrites(true);
	deluge::playback::watchRenderInteractionWrites(true);
}

bool deluge_scenario_stale_ui_complete(void) {
	return g_stage == Stage::Done;
}

void deluge_scenario_stale_ui_dispatch_step(void) {
	g_stepDone = false;
	switch (g_stage) {
	case Stage::Loading:
		stepLoading();
		break;
	case Stage::Listing:
		stepListing();
		break;
	case Stage::InertRun:
		stepInertRun();
		break;
	case Stage::EditorSetup:
		stepEditorSetup();
		break;
	case Stage::EditorRun:
		stepEditorRun();
		break;
	case Stage::AuditionSetup:
		stepAuditionSetup();
		break;
	case Stage::AuditionRun:
		stepAuditionRun();
		break;
	case Stage::NoteSetup:
		stepNoteSetup();
		break;
	case Stage::NoteRun:
		stepNoteRun();
		break;
	case Stage::HoldStart:
		stepHoldStart();
		break;
	case Stage::HoldRelease:
		stepHoldRelease();
		break;
	case Stage::FailedRun:
		stepFailedRun();
		break;
	case Stage::ArrangeSetup:
		stepArrangeSetup();
		break;
	case Stage::ArrangeRun:
		stepArrangeRun();
		break;
	case Stage::Report:
		report();
		break;
	case Stage::Done:
		break;
	}
	g_stepDone = true;
}

bool deluge_scenario_stale_ui_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_stale_ui_failures(void) {
	return g_failures;
}

} // extern "C"
