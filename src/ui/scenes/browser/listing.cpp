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

#include "ui/scenes/browser/listing.h"

#include "base/notify/notice_queue.h"

#include <cassert>
#include <utility>

namespace deluge::ui::scenes::browser {

namespace {

/// @brief Should a failure of \p step be announced as a popup?
///
/// Every action but `Reload`: legacy's `runPendingListing` routes a failure to `onListingFailed`
/// (`Browser::onListingFailed`), which calls `display->displayError` -- except `reloadImpl`, which swallows
/// its read failure entirely and returns no error so the browser neither pops up nor closes
/// (`Browser::reloadImpl`, "Reload failure stays silent"). The seam keeps that distinction, but
/// unlike legacy it still reports the error in `ListingResult::error`, so a caller that *wants* to
/// announce a reload failure has the fact and can raise its own notice.
/// @param step The step that failed.
/// @return True to raise `notice_error_code`.
bool announces_errors(bind::ListingStep step) {
	return step != bind::ListingStep::RELOAD;
}

} // namespace

Listing::~Listing() {
	const State state = state_.load(std::memory_order_acquire);

	// The lifetime rule, enforced where it can be seen. A dispatched op holds `this` and cannot be
	// withdrawn, so destroying an in-flight seam hands the storage fiber a dangling pointer to
	// write `result_` and `state_` through. See the class docstring for why the answer is a rule
	// plus this assert rather than a heap-owned op context.
	assert(state != State::InFlight
	       && "a Listing was destroyed with a listing in flight -- the dispatched owner op still "
	          "holds `this`; own a Listing as a scene member, never on a stack frame");

	if (state != State::InFlight && catalogue_ != nullptr) {
		// Not in flight: if this instance was holding the catalogue (an uncollected `Ready`),
		// hand it back, or every later listing -- this layer's or legacy's -- is refused forever.
		// `release` is a no-op for a non-claimant, so this needs no state test of its own.
		catalogue_->release(this);
	}
	// In flight and past the assert (a release build): the owner is deliberately NOT released. A
	// jammed browser is a better outcome than letting a second listing race a fiber that is still
	// writing through a freed `this`.
}

RequestOutcome Listing::request(bind::ListingSpec req) {
	// This instance's own state first: an uncollected result is a more specific answer than "the
	// catalogue is busy", and it is the one the caller can actually act on.
	if (state_.load(std::memory_order_acquire) == State::Ready) {
		return RequestOutcome::ResultPending;
	}
	// No catalogue: nothing to list into, and nothing was claimed or announced.
	if (catalogue_ == nullptr) {
		return RequestOutcome::Dropped;
	}

	// Then the guard, which lives on the resource rather than here: the catalogue's `claim`. That
	// is what makes single flight a property of the shared catalogue rather than of this object --
	// the browser, marker editor and recorder scenes put three of these seams in the same process, AND legacy's
	// `Browser::beginListing` claims through the very same call,
	// so a legacy browser listing and a scenes listing cannot both believe they are alone
	// over one catalogue. A claim held by anyone -- this instance already in flight, another
	// seam, or a legacy browser -- is `Busy`.
	if (!catalogue_->claim(this)) {
		return RequestOutcome::Busy;
	}

	request_ = std::move(req);
	result_ = {};
	state_.store(State::InFlight, std::memory_order_release);

	// The busy indicator: `Notices::drain` arms `WorkingScene` and
	// pushes `SceneKind::Working` on this notice. "Working" is
	// legacy's own word here -- `beginListing` calls
	// `display->displayLoadingAnimationText("Working", /*delayed=*/true)` (`Browser::beginListing`).
	base::notify::notice_text("Working", base::notify::Notice::Kind::Working);

	if (!dispatch_(&Listing::trampoline, this)) {
		// The owner dropped it, so the op will never run. Undo everything the dispatch attempt
		// did: clear the in-flight state, release the claim and drop the working notice.
		state_.store(State::Idle, std::memory_order_release);
		catalogue_->release(this);
		base::notify::notice_kind(base::notify::Notice::Kind::ClearWorking);
		return RequestOutcome::Dropped;
	}
	return RequestOutcome::Accepted;
}

std::optional<ListingResult> Listing::take_result() {
	if (state_.load(std::memory_order_acquire) != State::Ready) {
		return std::nullopt;
	}
	ListingResult out = std::move(result_);
	result_ = {};
	state_.store(State::Idle, std::memory_order_release);
	// The catalogue is released HERE, not when the op finished. `ListingResult` counts the entries
	// rather than copying them -- they are still in the catalogue, and this caller has
	// not read them yet. Anything allowed to dispatch in that window -- another seam, or a legacy
	// browser -- would replace the vector underneath a caller that had just been told its listing
	// had landed. Legacy releases at the end of its op instead, and that is the same contract, not
	// a looser one: its consumer reads inside the op. See `bind::Catalogue::claim`.
	catalogue_->release(this);
	return out;
}

void Listing::set_dispatch(Dispatch dispatch) {
	// Idle only: an `InFlight` seam is waiting for an op the old dispatcher holds, and a `Ready`
	// one owes its caller a collect. Neither survives having the dispatcher changed underneath it.
	assert(state_.load(std::memory_order_acquire) == State::Idle
	       && "Listing::set_dispatch on a seam that is not Idle -- the pending op belongs to the old "
	          "dispatcher");
	dispatch_ = dispatch;
}

void Listing::set_catalogue(bind::Catalogue* catalogue) {
	if (catalogue_ == catalogue) {
		return; // a scene re-opened over the same ports, whatever its listing is doing
	}
	assert(state_.load(std::memory_order_acquire) == State::Idle
	       && "Listing::set_catalogue on a seam that is not Idle -- the pending op belongs to the old catalogue");
	// An idle seam holds no claim -- a claim lasts from an accepted request to its collect -- so the old
	// catalogue has nothing to give back.
	catalogue_ = catalogue;
}

void Listing::trampoline(void* self) {
	static_cast<Listing*>(self)->perform();
}

void Listing::perform() {
	bind::ListingOutcome outcome = catalogue_->list(request_);
	ListingResult out{};
	out.step = request_.step;
	out.error = outcome.error;
	out.entered_text = std::move(outcome.entered_text);
	out.dir = std::string{catalogue_->dir()};
	out.count = catalogue_->count();
	result_ = std::move(out);

	base::notify::notice_kind(base::notify::Notice::Kind::ClearWorking);
	if (result_.error.has_value() && announces_errors(result_.step)) {
		base::notify::notice_error_code(*result_.error);
	}

	state_.store(State::Ready, std::memory_order_release);
}

} // namespace deluge::ui::scenes::browser
