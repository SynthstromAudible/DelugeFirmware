/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "model/instrument/rendering_drums_link.h"
#include <etl/intrusive_list.h>

class SoundDrum;

/// @brief The drums of one kit that are rendering now; the render walks them every block
///        (Kit::renderGlobalEffectableForClip).
///
/// Held in an intrusive list, so adding and removing a drum never allocates: the render adds and removes drums as
/// their voices start and end, and must not touch the heap. Every add and remove runs inside an
/// AudioExcludedSection (libdeluge/system.h) -- free on the audio tier, a mask or the host lock anywhere else -- so
/// no tier sees the list mid-splice, and reports to the structural-edit probe (util/structural_edit_probe.h).
///
/// @note Drums are kept in address order, so the render mixes them in the same order block to block.
/// @warning Only this class splices: ETL counts the elements, so nothing else may unlink a drum's link.
class RenderingDrums {
public:
	RenderingDrums() = default;
	RenderingDrums(const RenderingDrums&) = delete;
	RenderingDrums& operator=(const RenderingDrums&) = delete;

	/// @brief [task] [audio] Add @p drum. Does nothing if it is already in: both tiers add, and whichever runs
	///        second finds it in.
	void add(SoundDrum* drum);
	/// @brief [task] [audio] Remove @p drum. Does nothing if it is not in.
	void remove(SoundDrum* drum);
	/// @brief [task] [audio] Whether @p drum is in (O(1): its link says so).
	[[nodiscard]] bool contains(const SoundDrum* drum) const;
	[[nodiscard]] bool empty() const { return list_.empty(); }

	/// @brief [audio] Call @p f(SoundDrum*) on each drum.
	///
	/// A drum may remove itself as it renders: the walk goes on from the drum that followed it. If rendering a
	/// drum also removed the drum after it (a choke), the walk goes on from the current drum's live next if the
	/// current drum is still in, and otherwise ends: no removed link is followed.
	template <class F>
	void walk(F&& f) {
		Link* node = list_.first();
		while (node != nullptr) {
			Link* savedNext = list_.after(node);
			f(drumOf(node));
			if (node->is_linked()) {
				node = list_.after(node);
			}
			else if (savedNext != nullptr && savedNext->is_linked()) {
				node = savedNext;
			}
			else {
				break;
			}
		}
	}

private:
	using Link = RenderingDrumsLink;

	/// @brief The ETL list, with the O(1) splices its public interface keeps protected: unlinking a known element
	///        and reading raw links for the walk.
	class List : public etl::intrusive_list<SoundDrum, Link> {
	public:
		void unlinkElement(Link& link) { this->disconnect_link(link); }
		/// @brief The first link, or nullptr if empty.
		Link* first() { return empty() ? nullptr : this->get_head(); }
		/// @brief The link after @p link, or nullptr at the end.
		Link* after(Link* link) {
			Link* next = link->etl_next;
			return (next == &this->terminal_link) ? nullptr : next;
		}
	};

	static SoundDrum* drumOf(Link* link);

	List list_;
};
