#pragma once

/// @file
/// @brief What the app's gold knobs assemble their pages from: the owners' published records and the device facts.

#include "controls/facts.h"
#include "controls/pages.h"
#include "plugin-api/remote_controls_publisher.h"

#include <span>

namespace deluge::scenes::bind {

/// @brief The owners' remote-controls records: the Deluge instrument's (a sound's pages) and the global effectable's
///        (a chain's).
/// @return The records, one per plugin id (see plugin::remote_controls_for).
[[nodiscard]] std::span<const plugin::RemoteControls> app_publishers();

/// @brief The app's page sources: its publishers, and the device facts @p facts answers.
/// @param facts The host's facts, which hold the alternative delay-knob setting.
/// @return The sources controls::current_page and controls::section_pages read.
[[nodiscard]] controls::PageSources app_page_sources(const controls::Facts& facts);

} // namespace deluge::scenes::bind
