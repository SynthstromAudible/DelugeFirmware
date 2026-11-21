#pragma once

/// @file
/// @brief The app's ports, which a menu opened in the app binds its target nodes through, and the app's hooks a scene
/// calls. Declared here and defined by the app, as the scenes link the app: in gui/ui/shell_host/ (the ports by
/// ports.cpp, the waveforms by waveforms.cpp, the rest by menu_hooks.cpp).

#include "scenes/bind/ports/ports.h"
#include "scenes/bind/ports/waveforms.h"
#include "scenes/services/favourites.h"

#include <cstdint>
#include <string>
#include <string_view>

class Song;

namespace deluge::scenes::menu {

/// @brief Whether @p song is the song the app plays: the audio clip scene's guard over a song the app's ports do not
///        act on.
bool host_binds(const ::Song& song);
/// @brief The app's ports, which every scene's context takes; valid for the program's life.
[[nodiscard]] const deluge::scenes::bind::Ports& host_ports();
/// @brief Set the song's tempo from the audio clip @p clip's length, as a held tempo encoder and a pad do: a command to
///        the running transport, which no record holds.
void host_grab_tempo(deluge::project::ClipAddress clip);

/// @brief A flash, community-features or full reset ran: the app re-applies, on the guest and the open menu, the reset
///        settings main reads live.
void host_settings_reset();

/// @brief The app's waveforms port (`gui::shell_host::waveforms`). For the sample browser's reads alone, until the
///        browser takes its ports.
const deluge::scenes::bind::Waveforms& host_waveforms();
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
/// @brief Clear the slow flash's tick squares: what main's recorder does as it opens.
void host_clear_tick_squares();
/// @brief Forget the undo history: what main's sample browser does as it opens.
void host_forget_undo();
/// @brief The favourites bank the app's browsers share.
deluge::scenes::services::Favourites& host_favourites();

} // namespace deluge::scenes::menu
