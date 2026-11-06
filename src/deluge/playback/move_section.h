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
#pragma once

#include "libdeluge/system.h"
#include "memory/heaps.h"
#include "model/model_stack.h"

#include <cstdint>

/// How the interaction tier moves the playhead while the render can tick.
///
/// The render ticks the sequencer, so a move of playback position state -- a lane cursor, a clip's or note row's
/// position, the next-event schedule, the playback mode or state -- made on the interaction tier runs in one
/// MoveSection, which holds the render off. On the device that masks interrupts for the section's length, so only
/// what the render's own tick does may run inside: no allocation, no card, no undo, no wait. What a move sets off that
/// may do those (a delay param's notify can grow the delay's reserve or log an Edit) is recorded by MoveEffects inside
/// and run once the section ends. The render calls the same move code with no section: MoveSection does nothing there.
namespace deluge::playback {

/// @brief Effects a move must not make with the render held off, recorded in order and run when the section ends.
class MoveEffects {
public:
	/// A notify of a param its owner's delay reacts to (deluge::model::onDelayParamModified()).
	struct DelayNotify {
		ModelStackWithAutoParam stack; ///< copied: the stack's frame is gone when it runs
		int32_t oldValue;
		bool automatedBefore;
	};

	/// @brief [render] or [interaction] Whether a section on the interaction tier is collecting now.
	static bool collecting();
	/// @brief [interaction] Record @p notify to run when the section ends.
	/// @return False if the room is full: the caller then acts at once.
	static bool defer(const DelayNotify& notify);

#if defined(DELUGE_HOST)
	/// @brief Host only: run each recorded notify through @p runner instead of the delay's own; nullptr restores it.
	static void setRunnerForTest(void (*runner)(const DelayNotify&));
#endif

	/// While it lives, the interaction tier's effects are collected; the outermost one's end runs them, in order.
	class Scope {
	public:
		Scope();
		~Scope();
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;

	private:
		bool collecting_;
	};
};

/// @brief The time a section holds the render off, per site.
class MaskedSpan {
public:
	/// @param site a static name for the report
	explicit MaskedSpan(const char* site) : site_(site) {}
	MaskedSpan(const MaskedSpan&) = delete;
	MaskedSpan& operator=(const MaskedSpan&) = delete;

private:
	[[maybe_unused]] const char* site_;
};

/// @brief One playhead move on the interaction tier: its effects collected, the render held off, nothing allocated,
///        the span timed. The members end in reverse: the render runs again, then the collected effects.
///
/// On the render (which calls the same move code) it holds nothing off and collects nothing.
class MoveSection {
public:
	/// @param site a static name for the span report
	explicit MoveSection(const char* site) : span_(site) {}
	MoveSection(const MoveSection&) = delete;
	MoveSection& operator=(const MoveSection&) = delete;

private:
	MoveEffects::Scope effects_;
	MaskedSpan span_;
	AudioExcludedSection masked_;
	deluge::memory::NoAllocationScope noAllocation_;
};

/// Points in the interaction tier's playhead moves where the render may land between two steps that belong together:
/// a host harness runs a render block there to show the window is closed.
enum class InjectAt : uint8_t {
	StartBeforeClock, ///< playback start, after the clips are positioned and before the clock is published
	AfterMove,        ///< a reset, after the clips are positioned and before linear recording reads them
	LaunchBeforeMove, ///< a late launch, after the start position is read and before the clip is moved there
	kCount,
};

#if defined(DELUGE_HOST)
/// @brief Host only: run @p hook (a render block) the next time code reaches @p at, once; nullptr clears it.
void setRenderInjection(InjectAt at, void (*hook)());
/// @brief Host only: run the hook set for @p at, if any, and clear it.
void injectRenderAt(InjectAt at);
#else
inline void injectRenderAt(InjectAt /*at*/) {
}
#endif

} // namespace deluge::playback
