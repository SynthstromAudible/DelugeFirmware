#pragma once
#include <cstdlib>
#include <string>

// Create a fresh temp dir and point DELUGE_SD_ROOT at it (the passthrough resolves every path under it).
inline std::string fresh_root() {
	char tmpl[] = "/tmp/deluge_pt_XXXXXX";
	const char* dir = mkdtemp(tmpl);
	std::string root = (dir != nullptr) ? dir : "/tmp";
	setenv("DELUGE_SD_ROOT", root.c_str(), 1);
	return root;
}
