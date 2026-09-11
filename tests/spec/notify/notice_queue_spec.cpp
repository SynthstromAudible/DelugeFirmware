#include "../support/spec_main.h"
#include "cppspec.hpp"
#include "notify/notice_queue.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace deluge::notify;

// clang-format off
describe notice_queue("notify::NoticeQueue", $ {
	it("pops in push order", _ {
		NoticeQueue q;
		Notice a{}; a.kind = Notice::Kind::Console; std::strcpy(a.text, "Undo");
		Notice b{}; b.kind = Notice::Kind::Error; b.error_code = 42;
		expect(q.push(a)).to_be_true();
		expect(q.push(b)).to_be_true();
		Notice out{};
		expect(q.pop(out)).to_be_true();
		expect(std::string(out.text)).to_equal("Undo");
		expect(q.pop(out)).to_be_true();
		expect(int(out.error_code)).to_equal(42);
		expect(q.pop(out)).to_be_false();
	});

	it("overwrites the oldest when full and says so", _ {
		NoticeQueue q;
		for (size_t i = 0; i < kNoticeCapacity; i++) {
			Notice n{}; n.error_code = int16_t(i);
			expect(q.push(n)).to_be_true();
		}
		Notice last{}; last.error_code = 99;
		expect(q.push(last)).to_be_false();
		Notice out{};
		expect(q.pop(out)).to_be_true();
		expect(int(out.error_code)).to_equal(1);        // 0 was overwritten
		expect(q.size()).to_equal(kNoticeCapacity - 1);
	});

	it("round-trips a text that exactly fills the buffer", _ {
		// Drain first: the global queue is process-wide and shared with every other spec in
		// this driver binary, so a prior example that called a notice_*() free function could
		// leave entries behind. This example only cares about the notice it pushes itself.
		Notice drained{};
		while (notices().pop(drained)) {}
		// 47 characters: the most Notice::text can hold alongside its NUL.
		const char* longest = "Transpose Interval: 12 Semitones, unclipped :-)";
		expect(std::strlen(longest)).to_equal(sizeof(Notice::text) - 1);
		notice_text(longest, Notice::Kind::Text, 3);
		Notice out{};
		expect(notices().pop(out)).to_be_true();
		expect(std::string(out.text)).to_equal(std::string(longest));
		expect(int(out.popup_type)).to_equal(3);
	});

	it("counts a drop when the global queue overflows behind a notice_* helper", _ {
		// The notice_*() helpers cannot return a failure -- their callers are model code with
		// nowhere to put it -- so they count it instead. dropped_notices() is a process-wide
		// counter that never resets, so this measures the delta, not the absolute value.
		Notice drained{};
		while (notices().pop(drained)) {}
		const uint32_t before = dropped_notices();

		// Exactly fills the ring: no drop yet.
		for (size_t i = 0; i < kNoticeCapacity; i++) {
			notice_error_code(int16_t(i));
		}
		expect(dropped_notices()).to_equal(before);

		// One past capacity overwrites the oldest, and the helper counts it.
		notice_error_code(99);
		expect(dropped_notices()).to_equal(before + 1);

		// The overwrite really happened: the oldest (0) is gone and the queue still holds
		// kNoticeCapacity - 1 entries plus the newcomer.
		Notice out{};
		expect(notices().pop(out)).to_be_true();
		expect(int(out.error_code)).to_equal(1);
		while (notices().pop(drained)) {}
	});

	it("copies and truncates text with a NUL", _ {
		Notice drained{};
		while (notices().pop(drained)) {}
		// 60 characters, i.e. past the 47 Notice::text can hold.
		notice_text("THIS PRESET NAME IS VERY MUCH LONGER THAN FORTY-SEVEN CHARS.", Notice::Kind::Text, 3);
		Notice out{};
		expect(notices().pop(out)).to_be_true();
		expect(std::strlen(out.text)).to_equal(sizeof(Notice::text) - 1);
		expect(std::string(out.text)).to_equal("THIS PRESET NAME IS VERY MUCH LONGER THAN FORTY");
		expect(int(out.popup_type)).to_equal(3);
	});

	it("notice_notification carries legacy's (title, value) pair, each half NUL-terminated", _ {
		// `HorizontalMenu::displayNotification`'s call, `display->displayNotification(
		// menuItem->getName(), notificationValueBuf.data())` (`horizontal_menu.cpp:342`).
		Notice drained{};
		while (notices().pop(drained)) {}
		notice_notification("Probability", "100%");
		Notice out{};
		expect(notices().pop(out)).to_be_true();
		expect(out.kind == Notice::Kind::Notification).to_be_true();
		expect(std::string(out.notification_title())).to_equal("Probability");
		expect(std::string(out.notification_value())).to_equal("100%");
		expect(int(out.popup_type)).to_equal(0);
		expect(notices().pop(out)).to_be_false(); // one notice, not a title notice and a value notice
	});

	it("notice_notification with an empty value is a title-only notification", _ {
		Notice drained{};
		while (notices().pop(drained)) {}
		notice_notification("Probability", "");
		Notice out{};
		expect(notices().pop(out)).to_be_true();
		expect(std::string(out.notification_title())).to_equal("Probability");
		expect(std::string(out.notification_value())).to_equal("");
	});

	it("notice_notification gives the value priority when the pair does not fit, as legacy's "
	   "truncation loop does", _ {
		// `OLED::displayNotification` shortens `titleBuf` until the value fits beside it and never
		// shortens the value (`hid/display/oled.cpp:760-766`).
		Notice drained{};
		while (notices().pop(drained)) {}
		const std::string title(40, 'T');
		const std::string value(20, 'V');
		notice_notification(title, value);
		Notice out{};
		expect(notices().pop(out)).to_be_true();
		expect(std::string(out.notification_value())).to_equal(value);
		// 48 bytes: the title, its NUL, the 20-byte value and its NUL.
		expect(std::string(out.notification_title())).to_equal(std::string(sizeof(Notice::text) - 2 - 20, 'T'));
	});
});

DELUGE_SPEC(notify_notice_queue_spec, notice_queue)
