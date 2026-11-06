#pragma once

/// @file
/// @brief The app's write sink and runtime facts, which a menu opened in the app binds its target nodes through.
/// Declared here and defined by the app (gui/ui/shell_bind_host.cpp), as the scenes link the app.

#include "scenes/bind/runtime_facts.h"
#include "scenes/bind/write_sink.h"
#include "scenes/services/waveforms.h"

#include <string>
#include <string_view>

class Song;

namespace deluge::scenes::menu {

/// @brief Whether the app's sink and facts act on @p song: they resolve slots in the song the app plays, so a menu over
///        any other song binds nothing through them.
bool host_binds(const ::Song& song);
/// @brief The sink that applies a menu's writes as their targets' Edits.
deluge::scenes::bind::WriteSink& host_write_sink();
/// @brief The facts a menu needs that the model does not hold, answered from the running app.
const deluge::scenes::bind::RuntimeFacts& host_runtime_facts();
/// @brief Set the song's tempo from the audio clip @p clip's length, as a held tempo encoder and a pad do: a command to
///        the running transport, which no record holds.
void host_grab_tempo(deluge::project::ClipAddress clip);
/// @brief The waveforms of the song the app plays.
const deluge::scenes::services::Waveforms& host_waveforms();
/// @brief The file the sample browser last started to preview or load (legacy's `SampleBrowser::lastFilePathLoaded`),
///        which a sample browser opens beside when its holder names no file in SAMPLES/.
std::string host_last_sample_path();
/// @brief Stop the preview's sound and drop the sample file @p path from memory, unless the song uses it: the sample
///        browser's Shift+SAVE, before it offers to delete the file (`SampleBrowser::buttonAction`).
/// @return False when the song uses the file, which may then not be deleted.
bool host_release_sample_file(std::string_view path);

} // namespace deluge::scenes::menu
