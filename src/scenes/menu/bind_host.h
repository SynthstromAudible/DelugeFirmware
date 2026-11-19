#pragma once

/// @file
/// @brief The app's write sink, runtime facts and ports, which a menu opened in the app binds its target nodes through.
/// Declared here and defined by the app (gui/ui/shell_bind_host.cpp), as the scenes link the app.

#include "controls/sink.h"
#include "scenes/bind/ports/ports.h"
#include "scenes/bind/runtime_facts.h"
#include "scenes/bind/write_sink.h"
#include "scenes/services/favourites.h"
#include "scenes/services/waveforms.h"

#include <cstdint>
#include <string>
#include <string_view>

class Song;

namespace deluge::scenes::menu {

/// @brief Whether the app's sink and facts act on @p song: they resolve slots in the song the app plays, so a menu over
///        any other song binds nothing through them.
bool host_binds(const ::Song& song);
/// @brief The sink that applies a menu's writes as their targets' Edits.
deluge::scenes::bind::WriteSink& host_write_sink();
/// @brief The same sink as the controls' interface: the gold knobs' writes (@ref host_write_sink is the menus').
deluge::controls::Sink& host_controls_sink();
/// @brief The facts a menu needs that the model does not hold, answered from the running app.
const deluge::scenes::bind::RuntimeFacts& host_runtime_facts();
/// @brief The app's ports, which every scene's context takes; valid for the program's life.
[[nodiscard]] const deluge::scenes::bind::Ports& host_ports();
/// @brief Set the song's tempo from the audio clip @p clip's length, as a held tempo encoder and a pad do: a command to
///        the running transport, which no record holds.
void host_grab_tempo(deluge::project::ClipAddress clip);

/// @brief A flash, community-features or full reset ran: the app re-applies, on the guest and the open menu, the reset
///        settings main reads live.
void host_settings_reset();

/// @name Settings writes main applies at once
/// A settings item's setter stores the value, then calls one of these for the half this layer may not perform, as
/// main's `writeCurrentValue` makes both its statements at once. Each re-reads what the setter stored.
/// @{
/// @brief Both screensavers take the stored mode and timeout (`Screensaver::settingsChanged`).
void host_screensaver_settings_changed();
/// @brief The pads' dimmer takes `FlashStorage::defaultPadBrightness` (`PadLEDs::setBrightnessLevel`).
void host_pad_brightness_changed();
/// @brief Main's canvas flag and the guest's panels take the stored menu highlighting.
void host_highlighting_changed();
/// @brief Main's canvas flag and the guest's panels take the stored rounded-corners setting.
void host_rounded_corners_changed();
/// @brief The guest's shift machine takes the stored sticky-shift setting; turned off, both shift latches release.
void host_shift_sticky_changed();
/// @brief Every pad re-renders, whatever the UI (`renderingNeededRegardlessOfUI`): a pad colour changed.
void host_pad_colours_changed();
/// @}

/// @brief The waveforms of the song the app plays.
const deluge::scenes::services::Waveforms& host_waveforms();
/// @brief The file the sample browser last started to preview or load (`deluge::model::lastFilePathLoaded`),
///        which a sample browser opens beside when its holder names no file in SAMPLES/.
std::string host_last_sample_path();
/// @brief Stop the preview's sound and drop the sample file @p path from memory, unless the song uses it: the sample
///        browser's Shift+SAVE, before it offers to delete the file (`SampleBrowser::buttonAction`).
/// @return False when the song uses the file, which may then not be deleted.
bool host_release_sample_file(std::string_view path);
/// @brief How an opener ends a held audition, each as its legacy opener does.
enum class AuditionEnd : uint8_t {
	ClipViewWhenAuditioning,   ///< `AudioRecorder::opened`, `SampleBrowser::opened`
	ClipViewForInstrumentClip, ///< `EditName::beginSession`
	KeyboardWhenAuditioning,   ///< `LoopPoint::beginSession`, `FileSelector::beginSession`
};
/// @brief End a held audition the way @p how names, before a scene opens over it: the scene claims every pad, so the
///        held pad's release never reaches the view that started the note.
void host_end_audition(AuditionEnd how);
/// @brief Clear the slow flash's tick squares: what main's recorder does as it opens, and its flash-status item when
///        it leaves the slow flash.
void host_clear_tick_squares();
/// @brief Forget the undo history: what main's sample browser does as it opens.
void host_forget_undo();
/// @brief The favourites bank the app's browsers share.
deluge::scenes::services::Favourites& host_favourites();

} // namespace deluge::scenes::menu
