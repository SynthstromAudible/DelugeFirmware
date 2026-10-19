#pragma once

/// @file
/// @brief The app's write sink and runtime facts, which a menu opened in the app binds its target nodes through.
/// Declared here and defined by the app (gui/ui/scenes_bind_host.cpp), as the scenes link the app.

#include "scenes/bind/runtime_facts.h"
#include "scenes/bind/write_sink.h"

namespace deluge::scenes::menu {

/// @brief The sink that applies a menu's writes as their targets' Edits.
deluge::scenes::bind::WriteSink& host_write_sink();
/// @brief The facts a menu needs that the model does not hold, answered from the running app.
const deluge::scenes::bind::RuntimeFacts& host_runtime_facts();

} // namespace deluge::scenes::menu
