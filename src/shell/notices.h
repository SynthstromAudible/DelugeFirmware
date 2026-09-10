#pragma once
#include "notify/notice_queue.h"
#include "shell/profile.h"
#include "shell/scene.h"
#include "shell/scene_stack.h"
#include "shell/service.h"
#include "ui/core/state.h"
#include "ui/oled/popup.h"

#include <cstdint>
#include <string_view>

namespace deluge::ui::shell {

/// @name Legacy PopupType, mirrored numerically
///
/// `src/deluge/hid/display/display.h:13-36` (the `enum class PopupType`, no explicit values):
/// `NONE=0, GENERAL=1, LOADING=2, PROBABILITY=3, ITERANCE=4, FILL=5, SWING=6, TEMPO=7,
/// QUANTIZE=8, THRESHOLD_RECORDING_MODE=9, NOTIFICATION=10`.
///
/// `src/shell` may not include `hid/display` (the layer-rule ratchet's `ui-includes-outside`
/// only exempts `board_surfaces.cpp` for libdeluge headers, and this is not that file), so the
/// numbers are reproduced here rather than named from the real enum -- exactly as
/// `shell::Action::popup_type` already documents doing for the same reason. `Notice::popup_type`
/// (`notify/notice_queue.h:22`) is a bare `uint8_t` for the same boundary reason, and
/// `notify_drain.cpp`'s `popup_type_of()` folds 0 ("unspecified") to `GENERAL`, never to `NONE` --
/// `Notices::popup_type_of` below does the same.
/// @{
inline constexpr uint8_t kPopupTypeNone = 0;
inline constexpr uint8_t kPopupTypeGeneral = 1;
inline constexpr uint8_t kPopupTypeNotification = 10;
/// @}

/// @brief The name of legacy popup type \p popup_type, for `.state` "scene Popup/<type>" lines.
///
/// The eleven values the mirror above enumerates, spelled in this layer's `PascalCase` rather than
/// legacy's `SCREAMING_CASE`; a value outside that range has no name and is spelled by the caller.
/// @param popup_type The legacy `PopupType` value, as a bare `uint8_t`.
/// @return A static, NUL-terminated name, or nullptr for a value the mirror does not cover.
const char* popup_type_name(uint8_t popup_type);

/// @brief The name of notice kind \p k, for `.state` "notice <Kind> <arg>" lines.
/// @param k The kind to name.
/// @return A static, NUL-terminated name; never null (`Notice::Kind` has no unnamed values).
const char* notice_kind_name(notify::Notice::Kind k);

/// @brief A transient message drawn over whatever is beneath it: legacy's `OLED::popupText`/
///        `displayPopup`, ported to the shell's `Scene` vocabulary.
///
/// Owned by the application (typically as part of its scene table) and driven entirely through
/// `show()`/`expired()` by `Notices`; a `PopupScene` never reads the notice queue itself.
class PopupScene final : public Scene {
public:
	SceneKind kind() const override { return SceneKind::Popup; }

	/// @brief Replace the popup's contents and (re)arm its timeout.
	/// @param text       The text to show; copied into a 48-byte buffer (`Notice::text`'s own
	///                    size), truncated if longer.
	/// @param popup_type The legacy `PopupType` value, as a bare `uint8_t` -- see the numbering
	///                    above. Selects `Framed` vs. `Notification` in `render_oled`.
	/// @param now_ms     The tick's wall clock, used to compute the expiry deadline.
	/// @param timeout_ms How long until this popup should be popped; 0 means persistent (never
	///                    expires on its own -- `expired()` always reports false).
	void show(std::string_view text, uint8_t popup_type, uint32_t now_ms, uint32_t timeout_ms);

	/// @brief Has this popup's timeout elapsed?
	/// @param now_ms The tick's wall clock.
	/// @return False for a persistent popup (`timeout_ms == 0` at the last `show()`); otherwise
	///         true once \p now_ms reaches the deadline `show()` computed.
	bool expired(uint32_t now_ms) const;

	/// @brief The text last shown, for `.state` "notice" lines and specs.
	/// @return A NUL-terminated pointer into this scene's own 48-byte buffer.
	const char* text() const { return text_; }

	/// @brief The legacy `PopupType` the last `show()` selected, for `.state` "scene Popup/<type>"
	///        lines and specs.
	/// @return The type value, already folded (0 -> `General`) by whoever called `show()`.
	uint8_t popup_type() const { return popup_type_; }

	void render_oled(SurfaceRole role, ui::oled::OledFrame& frame) override;
	/// @brief Never transparent on `Main`: a shown popup draws its whole box (border or cleared
	///        interior, per style) over whatever the scene below left there, so the shell need
	///        not re-render the scene below just to keep this surface current. Every other
	///        surface is untouched, so the scene below should still draw it.
	bool oled_transparent(SurfaceRole role) const override;
	/// @brief A grid overlay that paints nothing.
	///
	/// Legacy's popups, console lines and working animation are OLED-only: `sendMainImage`
	/// composites them into the display image (`hid/display/oled.cpp:364-376`) and the pads carry
	/// on rendering whatever view is underneath. This is how the shell says the same thing --
	/// `Shell::render_grid` picks its base as the topmost scene that is *not* a grid overlay, so
	/// without this a pushed notice would become the base, and (having no `render_grid` of its
	/// own) would leave the pads black.
	///
	/// @note Being an overlay is *only* the "I am not the base" half. Spec D §7 goes on to dim what
	///       is beneath an overlay, which a notice must not do -- see `dims_grid()` immediately
	///       below, which is the half this scene opts out of.
	/// @return Always true.
	bool is_grid_overlay() const override { return true; }
	/// @brief Composites nothing, so dims nothing: legacy's popups, console lines and working
	///        animation leave the pads exactly as the view beneath painted them
	///        (`Scene::dims_grid`).
	/// @return Always false.
	bool dims_grid() const override { return false; }
	/// @brief Never takes the input of the scene it is drawn over.
	///
	/// Legacy routes nothing to a popup, a console line or the working animation: they are images,
	/// not UIs. See `Scene::input_transparent`.
	/// @return Always true.
	bool input_transparent() const override { return true; }
	Result handle(const UiEvent&, const UiContext&) override { return Result::unhandled(); }

private:
	/// @brief `Framed` for every legacy `PopupType` except `NOTIFICATION` -- exactly
	///        `OLED::setupPopup`'s own branch (`oled.cpp:134`: draws the rounded border unless
	///        `type == PopupType::NOTIFICATION`). `PopupStyle::Permanent` is never selected here:
	///        it is `drawPermanentPopupLookingText`'s own, separate call path (`session_view.cpp`,
	///        `automation_view.cpp`, `stem_export.cpp`), never reached through `Notice`/
	///        `notify_drain.cpp`, so a persistent (`timeout_ms == 0`) popup still draws `Framed`/
	///        `Notification` rather than switching style.
	static ui::oled::PopupStyle style_for(uint8_t popup_type);

	char text_[48] = {};
	uint8_t popup_type_ = kPopupTypeGeneral;
	uint32_t timeout_ms_ = 0;
	uint32_t deadline_ms_ = 0;
};

/// @brief The scrolling console log strip: legacy's `OLED::consoleText`, reduced to "show up to
///        three most-recent lines, each gone `kConsoleLineMs` after it was appended" -- no
///        pixel-by-pixel scroll-off animation (see `kConsoleLineMs` for why that duration, not
///        the animation's own frame interval, is the port's unit).
class ConsoleScene final : public Scene {
public:
	/// How many most-recent lines are kept; legacy's `MAX_NUM_CONSOLE_ITEMS` is 4, but the shell
	/// keeps 3 (per the brief driving this port).
	static constexpr size_t kMaxLines = 3;

	SceneKind kind() const override { return SceneKind::Console; }

	/// @brief Append one line, evicting the oldest once more than `kMaxLines` are live.
	/// @param line       The text to show; copied into a 48-byte buffer, truncated if longer.
	/// @param now_ms     The tick's wall clock; this line stops drawing at `now_ms + kConsoleLineMs`.
	/// @param popup_type The notice's own `Notice::popup_type` -- carried per line so `cancel()`
	///                   can tell which lines it is entitled to touch. See `cancel()`'s own doc.
	void append(std::string_view line, uint32_t now_ms, uint8_t popup_type);

	/// @brief Does any line still have time left to show?
	///
	/// The mirror of `PopupScene::expired()`/`WorkingScene::armed()`: what `Notices::tick()` polls
	/// to remove this scene from the stack once every line it was ever shown has expired (review
	/// round 1, I4 -- `Kind::Console`'s own `drain()` case pushes this scene but, unlike `Popup`
	/// and `Working`, nothing previously ever removed it again, leaking one stack entry for the rest
	/// of the process's life the first time any console notice fired). Extracted from
	/// `render_oled`'s own per-line skip test so `tick()` does not need a frame to ask the same
	/// question.
	/// @param now_ms The tick's wall clock.
	/// @return True if at least one line is `active` and not yet past its `expire_ms`.
	bool any_active(uint32_t now_ms) const;

	/// @brief Deactivate every line carrying @p popup_type immediately -- the port of
	///        `OLED::removePopup()` (`hid/display/oled.cpp:253-258`) as `HorizontalMenu::
	///        endSession`/`switchVisiblePage` call it (`gui/menu_item/horizontal_menu.cpp:587-589`,
	///        `:363-365`): "cancel whatever notification is showing right now", not "let the lines
	///        already up run out their own timers".
	///
	/// **Scoped to @p popup_type, not every line -- fix round 2's correction.** Legacy's
	/// `cancelPopup()`/`removePopup()` only ever removes the *popup* slot
	/// (`OLED::displayNotification`'s `PopupType::NOTIFICATION`, `oled.cpp:768`); it has no
	/// equivalent call that touches `consoleText()`'s own lines at all -- Undo/Redo
	/// (`action_logger.cpp:827`, `:840`) and the startup banner are `consoleText` too, and legacy
	/// never clears those when a horizontal menu's notification is cancelled. The port's first cut
	/// of this method cleared every line regardless of what raised it, because `Kind::Console`
	/// carries both under one `Notice::Kind` where legacy has two separate display calls -- this
	/// parameter is what tells them apart on the one call that needs to, matching `append`'s own
	/// per-line `popup_type` against the canceller's.
	///
	/// Setting `active` false rather than clearing the line outright keeps the text (harmless --
	/// nothing reads an inactive line) and matches `any_active`'s own test, so a `Notices::tick()`
	/// running immediately after sees the same "nothing left to show" this call already decided --
	/// for the lines it touched. A line whose `popup_type` differs is untouched: still active,
	/// still counted by `any_active()`, still keeping `SceneKind::Console` on the stack.
	/// @param popup_type Only lines appended with this `Notice::popup_type` are deactivated.
	void cancel(uint8_t popup_type);

	void render_oled(SurfaceRole role, ui::oled::OledFrame& frame) override;
	/// @brief Always transparent: the console draws only its own strip and leaves the rest of the
	///        surface for the scene beneath, which must still be drawn.
	bool oled_transparent(SurfaceRole) const override { return true; }
	/// @brief A grid overlay that paints nothing.
	///
	/// Legacy's popups, console lines and working animation are OLED-only: `sendMainImage`
	/// composites them into the display image (`hid/display/oled.cpp:364-376`) and the pads carry
	/// on rendering whatever view is underneath. This is how the shell says the same thing --
	/// `Shell::render_grid` picks its base as the topmost scene that is *not* a grid overlay, so
	/// without this a pushed notice would become the base, and (having no `render_grid` of its
	/// own) would leave the pads black.
	///
	/// @note Being an overlay is *only* the "I am not the base" half. Spec D §7 goes on to dim what
	///       is beneath an overlay, which a notice must not do -- see `dims_grid()` immediately
	///       below, which is the half this scene opts out of.
	/// @return Always true.
	bool is_grid_overlay() const override { return true; }
	/// @brief Composites nothing, so dims nothing: legacy's popups, console lines and working
	///        animation leave the pads exactly as the view beneath painted them
	///        (`Scene::dims_grid`).
	/// @return Always false.
	bool dims_grid() const override { return false; }
	/// @brief Never takes the input of the scene it is drawn over.
	///
	/// Legacy routes nothing to a popup, a console line or the working animation: they are images,
	/// not UIs. See `Scene::input_transparent`.
	/// @return Always true.
	bool input_transparent() const override { return true; }
	Result handle(const UiEvent&, const UiContext&) override { return Result::unhandled(); }

private:
	struct Line {
		char text[48] = {};
		uint32_t expire_ms = 0;
		bool active = false;
		uint8_t popup_type = 0; // the raising `Notice::popup_type` -- see `cancel()`'s own doc.
	};
	/// Index 0 is the most recently appended line; `append` shifts the rest down.
	Line lines_[kMaxLines]{};
};

/// @brief The "busy, please wait" corner animation: legacy's `updateWorkingAnimation`/
///        `displayWorkingAnimation`, drawn through the toolkit's `ui::oled::working()`.
class WorkingScene final : public Scene {
public:
	SceneKind kind() const override { return SceneKind::Working; }

	/// @brief Arm the animation and (re)anchor its clock.
	/// @param word   Legacy's direction switch: `"Loading"` runs right-to-left, anything else
	///               (`"Saving"`) left-to-right; copied into an 8-byte buffer, truncated if
	///               longer. See `ui::oled::working` -- nothing is printed.
	/// @param now_ms The tick's wall clock; the animation clock's own anchor is derived from this
	///               by `ui::oled::working` on the next `render_oled`, not stored eagerly here.
	void arm(std::string_view word, uint32_t now_ms);
	/// @brief Disarm the animation. Does not touch the scene stack -- see `Notices::tick`, which
	///        is what pops this scene once `armed()` goes false.
	void clear();
	/// @brief Is the animation currently armed?
	/// @return True from `arm()` until the next `clear()`.
	bool armed() const { return armed_; }

	void render_oled(SurfaceRole role, ui::oled::OledFrame& frame) override;
	/// @brief Always transparent: the animation draws only its own corner box.
	bool oled_transparent(SurfaceRole) const override { return true; }
	/// @brief A grid overlay that paints nothing.
	///
	/// Legacy's popups, console lines and working animation are OLED-only: `sendMainImage`
	/// composites them into the display image (`hid/display/oled.cpp:364-376`) and the pads carry
	/// on rendering whatever view is underneath. This is how the shell says the same thing --
	/// `Shell::render_grid` picks its base as the topmost scene that is *not* a grid overlay, so
	/// without this a pushed notice would become the base, and (having no `render_grid` of its
	/// own) would leave the pads black.
	///
	/// @note Being an overlay is *only* the "I am not the base" half. Spec D §7 goes on to dim what
	///       is beneath an overlay, which a notice must not do -- see `dims_grid()` immediately
	///       below, which is the half this scene opts out of.
	/// @return Always true.
	bool is_grid_overlay() const override { return true; }
	/// @brief Composites nothing, so dims nothing: legacy's popups, console lines and working
	///        animation leave the pads exactly as the view beneath painted them
	///        (`Scene::dims_grid`).
	/// @return Always false.
	bool dims_grid() const override { return false; }
	/// @brief Never takes the input of the scene it is drawn over.
	///
	/// Legacy routes nothing to a popup, a console line or the working animation: they are images,
	/// not UIs. See `Scene::input_transparent`.
	/// @return Always true.
	bool input_transparent() const override { return true; }
	Result handle(const UiEvent&, const UiContext&) override { return Result::unhandled(); }

private:
	char word_[8] = {};
	bool armed_ = false;
	Blink blink_{};
};

/// @brief Bridges `notify::NoticeQueue` to the shell's scene stack: the tick-context service that
///        drains raised notices into the three overlay scenes above, and polls them for
///        expiry/clearing every tick.
///
/// A `Service` rather than a `Scene`: `Notices` is not itself pushed or popped, has no render
/// surface of its own, and -- unlike a `Scene` -- is allowed to call `SceneStack::push`/`pop`
/// directly, which is exactly what draining a notice and expiring a popup need to do.
class Notices final : public Service {
public:
	/// @param stack   The scene stack `Notices` pushes/pops the popup and working scenes on.
	/// @param profile The board profile `text`/`error_text` resolve string/error ids through.
	/// @param popup   The popup scene `drain()`/`tick()`/`show_popup()` drive.
	/// @param console The console scene `drain()` appends to.
	/// @param working The working scene `drain()`/`tick()` arm, clear, and pop.
	Notices(SceneStack& stack, const BoardProfile& profile, PopupScene& popup, ConsoleScene& console,
	        WorkingScene& working)
	    : stack_(stack), profile_(profile), popup_(popup), console_(console), working_(working) {}

	/// @brief Pop every notice off \p queue and apply it.
	///
	/// `Notice::Kind::Error` resolves to a popup through `show_popup()`, always at `General`,
	/// ignoring \p n.popup_type -- legacy's `displayError` reaches `displayPopup` with its default
	/// `type = GENERAL` (`display.h:48-49`) -- *unless* \p n.error_code is legacy's `Error::NONE`
	/// or `Error::ABORTED_BY_USER`, which `OLED::displayError` (`oled.cpp:1173-1180`) suppresses
	/// entirely (`return;` before ever reaching `displayPopup`); this drops the notice the same
	/// way, showing nothing. `Kind::Text` also resolves to a popup, but not always through the
	/// plain `show_popup()` path: legacy's `notify_drain.cpp:137` computes
	/// `flashes = (severity == Warn) ? 0 : 3`, and `OLED::displayPopup`'s inline body
	/// (`oled.h:156`) turns that into `persistent = !numFlashes`, so a `Severity::Warn` text notice
	/// is persistent (`oled.cpp:642`, `unsetTimer(TimerName::DISPLAY)`) while every other severity
	/// gets the ordinary `timeout_for()` duration (`oled.cpp:637`) -- this drains that distinction
	/// through to `PopupScene::show`'s own `timeout_ms == 0` persistence, via the private helper
	/// `show_popup_with_timeout()` both this case and the public `show_popup()` below share.
	/// `Kind::Console` appends to the console scene; `Kind::Working`/`Kind::ClearWorking` arm/clear
	/// the working scene (and push it, on the way up, the first time it is armed);
	/// `Kind::CancelPopup` excises the popup scene by kind, wherever it sits -- including a
	/// still-persistent one, exactly as legacy's `cancelPopup()`/`removePopup()`
	/// (`oled.cpp:253-260`) does not consult the timer state either. This is `SceneStack::remove`,
	/// not `pop`: a popup is `input_transparent()`, so an application `Push` can legitimately land
	/// above a live one, and cancelling the popup must not take that scene down with it -- it excises
	/// just the popup and closes the gap, leaving whatever else is on the stack exactly where it was.
	/// A no-op when no popup is stacked at all.
	///
	/// `Kind::CancelConsole` is scoped, not a console-wide `CancelPopup` -- fix round 2's
	/// correction, `ConsoleScene::cancel(uint8_t)`'s own doc has the full argument. In short:
	/// `console_.cancel(n.popup_type)` deactivates only the lines this notice's own `popup_type`
	/// matches, and `stack_.remove(SceneKind::Console)` runs only once `!console_.any_active(
	/// now_ms)` -- an unrelated `consoleText` line (Undo/Redo, the startup banner) is untouched and
	/// keeps the scene up for as long as it has left. Added for `MenuScene`'s horizontal-page
	/// notification (`HorizontalMenu::endSession`, `gui/menu_item/horizontal_menu.cpp:587-589`;
	/// `switchVisiblePage`, `:363-365`), raised with `ui::shell::kPopupTypeNotification` -- the one
	/// raiser of `Kind::Console` this file knows of that ever needs to take its own notice back
	/// before its timer would have, and legacy's own `cancelPopup()`/`removePopup()`
	/// (`oled.cpp:253-260`) is exactly this scoped too: it clears the *popup* slot
	/// (`PopupType::NOTIFICATION`), never `consoleText()`'s own lines, which is why this cancel
	/// must not simply clear every `Kind::Console` line either.
	///
	/// Every string id and error code here is resolved through `BoardProfile::text_or_digits` /
	/// `error_text_or_digits`, so a profile with no l10n bridge shows the id's digits rather than
	/// an empty popup box.
	/// @param queue  The notice queue to drain.
	/// @param now_ms The tick's wall clock.
	void drain(notify::NoticeQueue& queue, uint32_t now_ms);

	/// @brief Show (or update) the popup scene at the ordinary (non-persistent) timeout computed
	///        from \p popup_type.
	///
	/// The path the shell (outside this class) is meant to call from a
	/// `Router::pending_popup_string()`/`pending_popup_type()` pair once it has resolved the
	/// string id through the profile -- `Action::Kind::Popup` and a non-`Warn` `Notice::Kind::Text`
	/// both end up drawing the same popup, through the same underlying mechanism
	/// (`show_popup_with_timeout()`). `drain()`'s `Kind::Text` case does **not** call this
	/// overload -- it calls `show_popup_with_timeout()` directly, so it can pass 0 for a `Warn`
	/// notice instead of always computing `timeout_for(popup_type)` -- see `drain()`'s own doc.
	///
	/// If the popup scene is already stacked anywhere -- top or not, a scene may legitimately sit
	/// above a live popup -- this only replaces its text/type/timeout in place (no second push);
	/// otherwise it pushes the popup scene.
	/// @param text       The resolved text to show.
	/// @param popup_type The legacy `PopupType` value, as a bare `uint8_t`; 0 is folded to
	///                    `General`, exactly as `notify_drain.cpp`'s `popup_type_of` does.
	/// @param now_ms     The tick's wall clock.
	void show_popup(std::string_view text, uint8_t popup_type, uint32_t now_ms);

	/// @brief Remove the popup once it has expired, the working scene once it has been cleared, and
	///        the console strip once every line it ever showed has expired (`ConsoleScene::any_active`).
	///
	/// All three are `SceneStack::remove`, for the same reason `drain()`'s `CancelPopup` case is: a
	/// popup, working animation or console strip is `input_transparent()`, so a scene pushed over any
	/// of them is there legitimately and must survive the notice's own expiry -- only the notice
	/// scene itself is excised, wherever it sits. The console case is review round 1's addition
	/// (I4): unlike `Popup`/`Working`, its `drain()` case had a push with no matching removal at all,
	/// leaking one stack entry, permanently, from the first console notice the process ever raised.
	/// @param now_ms The tick's wall clock.
	/// @param dt_ms  Unused: all three checks are level tests against the scenes' own state, not
	///               accumulators -- there is nothing to integrate.
	void tick(uint32_t now_ms, uint32_t dt_ms) override;

	/// @brief How long a popup of \p popup_type should stay up before `tick()` pops it.
	///
	/// Ported from `OLED::popupText` (`oled.cpp:605-644`): every non-persistent popup gets the
	/// same `TimerName::DISPLAY` timeout, 800ms, regardless of `PopupType`
	/// (`oled.cpp:637`; unset entirely for a persistent popup, `oled.cpp:642`). Every
	/// `Notice`-reachable popup call -- `displayPopup` (used by `Notice::Kind::Error` and
	/// `Kind::Text` alike, `notify_drain.cpp:130,139,142`), `popupText`, `popupTextTemporary` --
	/// funnels through this one routine (`oled.h:154-162`), so there is no per-`PopupType`
	/// duration to port: this table is flat by construction, not by omission. (The one *other*
	/// `DISPLAY`-timer setter tied to a specific type, `oled.cpp:794` in `Display::displayNotification`,
	/// sets 2000ms for `PopupType::NOTIFICATION` -- but `displayNotification` is a call path
	/// `Notice`/`notify_drain.cpp` never reaches, so it is not modelled here.)
	/// @param popup_type The legacy `PopupType` value; unused today (see above), kept for
	///                    interface symmetry and in case a future type gains a real duration.
	/// @return The timeout in milliseconds.
	uint32_t timeout_for(uint8_t popup_type) const;

	/// @brief The popup scene's current text, for `.state` "notice" lines.
	/// @return The text last shown, or an empty (but still valid) NUL-terminated string if none
	///         has been shown yet.
	const char* last_popup_text() const { return popup_.text(); }

	/// @brief The `Notice::Kind` that produced the popup `last_popup_text()` reports, for `.state`
	///        "notice <Kind> <arg>" lines.
	///
	/// Only the two kinds that can raise a popup are ever recorded here -- `Kind::Error` from
	/// `drain()`'s error case, `Kind::Text` from its text case and from the public `show_popup()`
	/// an `Action::Kind::Popup` reaches. A console line, a working animation or a cancel never
	/// touches it, since none of them is what a shown popup came from.
	/// @return The kind of the most recent popup; `Kind::Text` if no popup has been shown yet.
	notify::Notice::Kind last_notice_kind() const { return last_notice_kind_; }

private:
	/// @brief The body both `show_popup()` and `drain()`'s `Kind::Text` case share: show \p text
	///        at \p popup_type with an explicit \p timeout_ms (0 = persistent, per
	///        `PopupScene::show`), pushing the popup scene only if it is not already the stack's
	///        top.
	/// @param text       The resolved text to show.
	/// @param popup_type The legacy `PopupType` value, already folded (0 -> `General`) by the
	///                    caller.
	/// @param now_ms     The tick's wall clock.
	/// @param timeout_ms How long the popup should stay up; 0 for a persistent (`Warn`) notice.
	/// @param kind       What raised it, recorded for `last_notice_kind()`.
	void show_popup_with_timeout(std::string_view text, uint8_t popup_type, uint32_t now_ms, uint32_t timeout_ms,
	                             notify::Notice::Kind kind);

	SceneStack& stack_;
	const BoardProfile& profile_;
	PopupScene& popup_;
	ConsoleScene& console_;
	WorkingScene& working_;
	/// What raised the popup `popup_` currently holds; see `last_notice_kind()`.
	notify::Notice::Kind last_notice_kind_ = notify::Notice::Kind::Text;
};

} // namespace deluge::ui::shell
