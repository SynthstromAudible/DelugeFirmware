#pragma once

#include "scenes/browser/browser_scene.h"

#include <optional>
#include <utility>
#include <vector>

/// @file
/// @brief A browser host that performs nothing and records what the browser asks for, for the scenes' own specs.

namespace deluge::scenes::spec {

/// @brief A `BrowserHost` that records each request and performs none: an example reads what the browser asked for
///        and answers it by hand through `BrowserScene`'s answer methods, as the application's host would.
class FakeBrowserHost final : public deluge::scenes::browser::BrowserHost {
public:
	void perform(const deluge::scenes::browser::BrowserChoice& choice) override { performed_.push_back(choice); }

	/// @brief The last choice performed since the last take or clear, once; nothing when none was.
	std::optional<deluge::scenes::browser::BrowserChoice> take_choice() {
		if (performed_.empty()) {
			return std::nullopt;
		}
		std::optional<deluge::scenes::browser::BrowserChoice> last{std::move(performed_.back())};
		performed_.clear();
		return last;
	}
	/// @brief Every choice performed since the last take or clear, in order.
	[[nodiscard]] const std::vector<deluge::scenes::browser::BrowserChoice>& performed() const { return performed_; }
	/// @brief Forget every request.
	void clear() { performed_.clear(); }

private:
	std::vector<deluge::scenes::browser::BrowserChoice> performed_{};
};

/// @brief The process's one fake host. A scenes spec runs one file per process, so one instance serves its examples.
inline FakeBrowserHost& fake_browser_host() {
	static FakeBrowserHost host;
	return host;
}

/// @brief Hand the browser's requests to a host for this object's lifetime, putting back what was installed before.
class UseBrowserHost {
public:
	explicit UseBrowserHost(deluge::scenes::browser::BrowserHost& host)
	    : saved_(deluge::scenes::browser::browser_scene().installed_host()) {
		deluge::scenes::browser::browser_scene().set_host(&host);
	}
	~UseBrowserHost() { deluge::scenes::browser::browser_scene().set_host(saved_); }
	UseBrowserHost(const UseBrowserHost&) = delete;
	UseBrowserHost& operator=(const UseBrowserHost&) = delete;

private:
	deluge::scenes::browser::BrowserHost* saved_;
};

} // namespace deluge::scenes::spec
