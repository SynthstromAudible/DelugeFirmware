#pragma once

/// @file
/// @brief The app's write sink and runtime facts, which a menu opened in the app binds its target nodes through.
/// Declared here and defined by the app (gui/ui/shell_bind_host.cpp), as the scenes link the app.

#include "scenes/bind/runtime_facts.h"
#include "scenes/bind/write_sink.h"
#include "scenes/services/waveforms.h"

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

} // namespace deluge::scenes::menu
