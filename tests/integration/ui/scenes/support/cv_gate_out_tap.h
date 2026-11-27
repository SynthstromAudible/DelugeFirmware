#pragma once

/// @file
/// @brief The CV and gate writes the app sent, one per line (`cv<channel>=<value>`, `gate<channel>=<0|1>`, the pin
///        level): the host's output service is flushed, so writes not yet due go out too (`deluge_host_out_flush`,
///        `src/platform/device/src/timed_output/host.rs`), then the log it kept is taken (`deluge_host_out_take`).

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

extern "C" void deluge_host_out_flush();
extern "C" uint32_t deluge_host_out_take(uint8_t* dst, uint32_t cap);

namespace deluge::ui::scenes::spec::cv_gate_out {

/// @brief Every CV and gate write since the last take, oldest first, each line ending in a newline ("" when none).
inline std::string take() {
	deluge_host_out_flush();
	std::string out;
	std::array<uint8_t, 512> chunk{};
	while (uint32_t n = deluge_host_out_take(chunk.data(), chunk.size())) {
		out.append(reinterpret_cast<const char*>(chunk.data()), n);
	}
	return out;
}

/// @brief How many lines of @p log start with @p prefix.
inline size_t count(std::string_view log, std::string_view prefix) {
	size_t n = 0;
	size_t at = 0;
	while (at < log.size()) {
		size_t end = log.find('\n', at);
		if (log.substr(at, end - at).starts_with(prefix)) {
			n++;
		}
		at = end == std::string_view::npos ? log.size() : end + 1;
	}
	return n;
}

} // namespace deluge::ui::scenes::spec::cv_gate_out
