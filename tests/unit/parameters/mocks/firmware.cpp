// External firmware services reachable through collection vtables. These tests
// exercise ownership, not playback/UI: fail if any such service is invoked.
#include "CppUTest/TestHarness.h"
#include "gui/ui/ui.h"
#include "gui/views/view.h"
#include "io/midi/midi_engine.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/clip.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/patch/patch_cable.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "storage/flash_storage.h"
#include <cstdlib>
#include <source_location>

namespace {
// The default argument captures the calling adapter, including its class name.
[[noreturn]] void unexpectedService(std::source_location caller = std::source_location::current()) {
	FAIL((SimpleString("Unexpected firmware service in parameter ownership test: ") + caller.function_name())
	         .asCharString());
	std::abort();
}
} // namespace
// These adapters must retain the production member-function signatures.
// NOLINTBEGIN(readability-convert-member-functions-to-static)
// Only static service instances use these constructors; their storage is zero-initialized.
// Playback/UI methods below fail before reading any service state.
// NOLINTBEGIN(cppcoreguidelines-pro-type-member-init,bugprone-throwing-static-initialization)
ActionLogger::ActionLogger() = default;
PlaybackHandler::PlaybackHandler() = default;
View::View() = default;
ActionLogger actionLogger;
PlaybackHandler playbackHandler;
View view;
Song* currentSong = nullptr;
// NOLINTEND(cppcoreguidelines-pro-type-member-init,bugprone-throwing-static-initialization)
namespace AudioEngine {
// ResizeableArray writes this under allocation pressure. Firmware provides it in
// audio_engine.cpp; host tests need storage without linking the rendering engine.
bool bypassCulling = false;
uint32_t audioSampleTimer = 0;
bool mustUpdateReverbParamsBeforeNextRender = false;
} // namespace AudioEngine
namespace FlashStorage {
// Matches the production extern array declaration.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
uint8_t defaultBendRange[2] = {2, 48};
Polarity defaultPatchCablePolarity = Polarity::BIPOLAR;
} // namespace FlashStorage
// Matches the production patcher global, including its name and array type.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,readability-identifier-naming)
int32_t rangeFinalValues[kMaxNumPatchCables]{};
void copyModelStack(void* /*unused*/, void const* /*unused*/, int32_t /*unused*/) {
	unexpectedService();
}
bool isUIModeActive(uint32_t /*unused*/) {
	unexpectedService();
}
Action* ActionLogger::getNewAction(ActionType /*unused*/, ActionAddition /*unused*/) {
	unexpectedService();
}
bool PlaybackHandler::isCurrentlyRecording() {
	unexpectedService();
}
uint32_t PlaybackHandler::getTimePerInternalTick() {
	unexpectedService();
}
uint32_t PlaybackHandler::getTimePerInternalTickInverse(bool /*unused*/) {
	unexpectedService();
}
bool Clip::isActiveOnOutput() {
	unexpectedService();
}
void View::notifyParamAutomationOccurred(ParamManager* /*unused*/, bool /*unused*/) {
	unexpectedService();
}
PatchCableAcceptance Sound::maySourcePatchToParam(PatchSource /*unused*/, uint8_t /*unused*/,
                                                  ParamManager* /*unused*/) {
	unexpectedService();
}
void Sound::notifyValueChangeViaLPF(int32_t /*unused*/, bool /*unused*/,
                                    ModelStackWithThreeMainThings const* /*unused*/, int32_t /*unused*/,
                                    int32_t /*unused*/, bool /*unused*/) {
	unexpectedService();
}
void Sound::recalculatePatchingToParam(uint8_t /*unused*/, ParamManagerForTimeline* /*unused*/) {
	unexpectedService();
}
void Action::recordParamChangeDefinitely(ModelStackWithAutoParam const* /*unused*/, bool /*unused*/) {
	unexpectedService();
}
bool Action::containsConsequenceParamChange(ParamCollection* /*unused*/, int32_t /*unused*/) {
	unexpectedService();
}
void Action::recordParamChangeIfNotAlreadySnapshotted(ModelStackWithAutoParam const* /*unused*/, bool /*unused*/) {
	unexpectedService();
}
int32_t ModelStackWithNoteRow::getLoopLength() const {
	unexpectedService();
}
int32_t ModelStackWithNoteRow::getLastProcessedPos() const {
	unexpectedService();
}
int32_t ModelStackWithNoteRow::getPosAtWhichPlaybackWillCut() const {
	unexpectedService();
}
// Static host service storage only; no MIDI methods are exercised.
// NOLINTBEGIN(cppcoreguidelines-pro-type-member-init,bugprone-throwing-static-initialization)
MidiEngine::MidiEngine() = default;
MidiEngine midiEngine;
// NOLINTEND(cppcoreguidelines-pro-type-member-init,bugprone-throwing-static-initialization)
void MidiEngine::sendCC(MIDISource /*unused*/, int32_t /*unused*/, int32_t /*unused*/, int32_t /*unused*/,
                        int32_t /*unused*/) {
	unexpectedService();
}
int32_t MIDIInstrument::getOutputMasterChannel() {
	unexpectedService();
}
bool Song::isOutputActiveInArrangement(Output* /*unused*/) {
	unexpectedService();
}
// Only used by the zero-initialized static MidiEngine adapter.
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
LearnedMIDI::LearnedMIDI() = default;
// NOLINTEND(readability-convert-member-functions-to-static)
