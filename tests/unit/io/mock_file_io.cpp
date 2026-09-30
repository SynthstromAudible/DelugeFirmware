// In-memory fake backing include/libdeluge/file_io.h's deluge_efatfs_* C-ABI,
// for host-only deluge::io tests. Not a real filesystem -- no FatFS, no disk
// I/O, no real handle-table locking. Mirrors the mock_diskio.cpp/mock_display.cpp
// pattern already used in tests/support/mocks/, one layer higher
// (file_io.h instead of diskio.h).
#include "mock_file_io.h"

extern "C" {
#include "libdeluge/file_io.h"
}

#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

struct MockEntry {
	bool is_directory = false;
	std::vector<uint8_t> data; // unused for directories
	DelugeTimestamp modified_time{};
	bool is_read_only = false;
	bool is_hidden = false;
	bool is_system = false;
	bool is_archive = false;
};

std::map<std::string, MockEntry> g_entries;

// Open file/directory handle tables, keyed by a packed `uint32_t` handle
// `(generation << 16) | slot_index` handed back to callers -- mirrors the
// real efatfs backend's generation-guarded Slot/HandleTable in
// efatfs_core.rs (lowest free slot starting at 0), so a handle whose slot
// was freed and later reused for something else fails `checkout` cleanly
// instead of silently aliasing. A claimed slot's generation is at least 1, so
// no handle this table hands out is DELUGE_HANDLE_NONE, and checking out
// DELUGE_HANDLE_NONE fails like any other stale handle.

struct OpenFile {
	std::string path;
	uint32_t position = 0;
};

struct OpenDir {
	struct ChildInfo {
		std::string name;
		MockEntry entry; // snapshotted at open time
	};
	std::vector<ChildInfo> children;
	size_t index = 0;
};

std::vector<std::optional<OpenFile>> g_open_files;
std::vector<uint32_t> g_open_files_gen;
std::vector<std::optional<OpenDir>> g_open_dirs;
std::vector<uint32_t> g_open_dirs_gen;

// Claims the lowest free slot in `table`, bumping that slot's generation
// (parallel `gens`, indexed the same as `table`) and returning the packed
// handle `(generation << 16) | index`. Mirrors efatfs_core.rs's
// Slot::generation, bumped on every claim so a handle from a prior tenant of
// this index can never alias the new occupant.
template <typename T>
uint32_t allocate_slot(std::vector<std::optional<T>>& table, std::vector<uint32_t>& gens, T value) {
	for (size_t i = 0; i < table.size(); ++i) {
		if (!table[i]) {
			table[i] = std::move(value);
			gens[i] += 1;
			return (gens[i] << 16) | static_cast<uint32_t>(i);
		}
	}
	table.push_back(std::move(value));
	gens.push_back(1);
	return (1u << 16) | static_cast<uint32_t>(table.size() - 1);
}

// Returns the live entry for `handle`, or nullptr if the slot is out of
// range, free, or its generation no longer matches (a stale handle whose
// slot was reused). Mirrors efatfs_core.rs's Slot/checkout generation guard
// so a bad handle fails cleanly instead of aborting the test binary.
template <typename T>
T* checkout(std::vector<std::optional<T>>& table, std::vector<uint32_t>& gens, uint32_t handle) {
	uint32_t index = handle & 0xFFFFu;
	uint32_t gen = handle >> 16;
	if (index >= table.size() || !table[index].has_value() || gens[index] != gen) {
		return nullptr;
	}
	return &table[index].value();
}

// Frees `handle`'s slot and bumps its generation, invalidating any copies of
// this handle still held elsewhere. No-op if `handle` doesn't check out (bad,
// freed, or stale) -- close on a bad handle is a silent no-op, matching the
// real backend.
template <typename T>
void release_slot(std::vector<std::optional<T>>& table, std::vector<uint32_t>& gens, uint32_t handle) {
	if (checkout(table, gens, handle) == nullptr) {
		return;
	}
	uint32_t index = handle & 0xFFFFu;
	table[index].reset();
	gens[index] += 1;
}

// FAT attribute bits (mirrors file.cpp's kFatAttr* constants -- see
// efatfs_core.rs's DirEntryInfo doc).
constexpr uint8_t kFatAttrReadOnly = 0x01;
constexpr uint8_t kFatAttrHidden = 0x02;
constexpr uint8_t kFatAttrSystem = 0x04;
constexpr uint8_t kFatAttrArchive = 0x20;

// Packs a DelugeTimestamp into deluge_efatfs_dir_read's `out_modified`
// convention (`(dos_date << 16) | dos_time`) -- the inverse of file.cpp's
// unpack_fat_datetime.
uint32_t pack_fat_datetime(const DelugeTimestamp& ts) {
	auto dos_date =
	    static_cast<uint16_t>((((ts.year - 1980) & 0x7F) << 9) | ((ts.month & 0x0F) << 5) | (ts.day & 0x1F));
	auto dos_time =
	    static_cast<uint16_t>(((ts.hour & 0x1F) << 11) | ((ts.minute & 0x3F) << 5) | ((ts.second / 2) & 0x1F));
	return (static_cast<uint32_t>(dos_date) << 16) | dos_time;
}

uint8_t pack_fat_attrs(const MockEntry& entry) {
	uint8_t attrs = 0;
	if (entry.is_read_only) {
		attrs |= kFatAttrReadOnly;
	}
	if (entry.is_hidden) {
		attrs |= kFatAttrHidden;
	}
	if (entry.is_system) {
		attrs |= kFatAttrSystem;
	}
	if (entry.is_archive) {
		attrs |= kFatAttrArchive;
	}
	return attrs;
}

std::map<std::string, DelugeStatus> g_injected;

// DELUGE_OK when no injection applies, otherwise the injected failure.
DelugeStatus injectedFor(const char* path) {
	if (path == nullptr) {
		return DELUGE_OK;
	}
	auto it = g_injected.find(std::string{path});
	return (it == g_injected.end()) ? DELUGE_OK : it->second;
}

} // namespace

void mock_file_io_reset() {
	g_entries.clear();
	g_open_files.clear();
	g_open_files_gen.clear();
	g_open_dirs.clear();
	g_open_dirs_gen.clear();
	g_injected.clear();
}

void mock_file_io_inject_status(const char* path, DelugeStatus status) {
	g_injected[std::string{path}] = status;
}

void mock_file_io_clear_injections() {
	g_injected.clear();
}

extern "C" {

DelugeStatus deluge_efatfs_file_open(const char* path, uint8_t mode, DelugeHandle* out_handle) noexcept {
	if (DelugeStatus injected = injectedFor(path); injected != DELUGE_OK) {
		return injected;
	}
	std::string p(path);
	if (static_cast<DelugeFileOpenMode>(mode) == DELUGE_FILE_READ) {
		auto it = g_entries.find(p);
		if (it == g_entries.end() || it->second.is_directory) {
			return DELUGE_ERR_NOT_FOUND;
		}
		*out_handle = allocate_slot(g_open_files, g_open_files_gen, OpenFile{p, 0});
		return DELUGE_OK;
	}
	// DELUGE_FILE_WRITE_CREATE / DELUGE_FILE_WRITE_CREATE_NEW: create, truncating if it exists.
	g_entries[p] = MockEntry{};
	*out_handle = allocate_slot(g_open_files, g_open_files_gen, OpenFile{p, 0});
	return DELUGE_OK;
}

DelugeStatus deluge_efatfs_file_read_exact(DelugeHandle handle, void* dst, uint32_t count,
                                           uint32_t* out_read) noexcept {
	auto* file = checkout(g_open_files, g_open_files_gen, handle);
	if (file == nullptr) {
		return DELUGE_ERR_PARAM;
	}
	auto& entry = g_entries.at(file->path);
	uint32_t available =
	    entry.data.size() > file->position ? static_cast<uint32_t>(entry.data.size()) - file->position : 0;
	uint32_t real = count < available ? count : available;
	std::memcpy(dst, entry.data.data() + file->position, real);
	file->position += real;
	*out_read = real;
	return DELUGE_OK;
}

DelugeStatus deluge_efatfs_file_write(DelugeHandle handle, const void* src, uint32_t count,
                                      uint32_t* out_written) noexcept {
	auto* file = checkout(g_open_files, g_open_files_gen, handle);
	if (file == nullptr) {
		return DELUGE_ERR_PARAM;
	}
	auto& entry = g_entries.at(file->path);
	if (file->position + count > entry.data.size()) {
		entry.data.resize(file->position + count);
	}
	std::memcpy(entry.data.data() + file->position, src, count);
	file->position += count;
	*out_written = count;
	return DELUGE_OK;
}

DelugeStatus deluge_efatfs_file_seek(DelugeHandle handle, uint32_t offset) noexcept {
	auto* file = checkout(g_open_files, g_open_files_gen, handle);
	if (file == nullptr) {
		return DELUGE_ERR_PARAM;
	}
	file->position = offset;
	return DELUGE_OK;
}

DelugeStatus deluge_efatfs_file_size(DelugeHandle handle, uint32_t* out_size) noexcept {
	auto* file = checkout(g_open_files, g_open_files_gen, handle);
	if (file == nullptr) {
		return DELUGE_ERR_PARAM;
	}
	*out_size = static_cast<uint32_t>(g_entries.at(file->path).data.size());
	return DELUGE_OK;
}

void deluge_efatfs_file_close(DelugeHandle handle) noexcept {
	release_slot(g_open_files, g_open_files_gen, handle);
}

DelugeStatus deluge_efatfs_dir_open(const char* path, DelugeHandle* out_handle) noexcept {
	if (DelugeStatus injected = injectedFor(path); injected != DELUGE_OK) {
		return injected;
	}
	std::string prefix(path);
	// Root (the empty path) always exists; any other path must name a real
	// directory entry -- otherwise this would report every nonexistent path as
	// successfully "open" with zero children, which would make it impossible
	// to ever observe NOT_FOUND from a directory-existence check.
	//
	// This mirrors real efatfs's Dir::open_dir, which distinguishes the two
	// failure shapes (crates/embedded-fatfs/src/dir.rs:252-256): Error::NotFound
	// when the path names no entry at all, vs. Error::InvalidInput when it names
	// an entry that exists but isn't a directory. The latter maps to
	// DELUGE_ERR_PARAM, which the policy layer classifies as Undeterminable, not
	// Absent -- conflating the two here would let a spec "prove" a safe absence
	// that the real device would never report.
	if (!prefix.empty()) {
		auto it = g_entries.find(prefix);
		if (it == g_entries.end()) {
			return DELUGE_ERR_NOT_FOUND;
		}
		if (!it->second.is_directory) {
			return DELUGE_ERR_PARAM;
		}
	}
	if (!prefix.empty() && prefix.back() != '/') {
		prefix += '/';
	}
	OpenDir dir;
	for (auto& [p, entry] : g_entries) {
		if (p.size() > prefix.size() && p.compare(0, prefix.size(), prefix) == 0) {
			std::string rest = p.substr(prefix.size());
			if (rest.find('/') == std::string::npos) {
				dir.children.push_back({rest, entry});
			}
		}
	}
	*out_handle = allocate_slot(g_open_dirs, g_open_dirs_gen, std::move(dir));
	return DELUGE_OK;
}

DelugeStatus deluge_efatfs_dir_read(DelugeHandle handle, char* out_name, uint32_t out_name_cap, bool* out_is_dir,
                                    uint32_t* out_size, uint32_t* out_modified, uint8_t* out_attrs,
                                    bool* out_has_entry) noexcept {
	auto* dir = checkout(g_open_dirs, g_open_dirs_gen, handle);
	if (dir == nullptr) {
		return DELUGE_ERR_PARAM;
	}
	if (dir->index >= dir->children.size()) {
		*out_has_entry = false;
		return DELUGE_OK;
	}
	const auto& child = dir->children[dir->index++];
	std::strncpy(out_name, child.name.c_str(), out_name_cap - 1);
	out_name[out_name_cap - 1] = 0;
	*out_is_dir = child.entry.is_directory;
	*out_size = static_cast<uint32_t>(child.entry.data.size());
	*out_modified = pack_fat_datetime(child.entry.modified_time);
	*out_attrs = pack_fat_attrs(child.entry);
	*out_has_entry = true;
	return DELUGE_OK;
}

void deluge_efatfs_dir_close(DelugeHandle handle) noexcept {
	release_slot(g_open_dirs, g_open_dirs_gen, handle);
}

DelugeStatus deluge_efatfs_mkdir(const char* path) noexcept {
	if (DelugeStatus injected = injectedFor(path); injected != DELUGE_OK) {
		return injected;
	}
	std::string p(path);
	auto it = g_entries.find(p);
	if (it != g_entries.end()) {
		// embedded-fatfs's create_dir is idempotent: calling it on an existing
		// directory succeeds (unlike C-FatFS's f_mkdir, which reports EXIST).
		// A pre-existing FILE at this path is still a hard conflict.
		return it->second.is_directory ? DELUGE_OK : DELUGE_ERR_EXISTS;
	}
	g_entries[p] = MockEntry{.is_directory = true};
	return DELUGE_OK;
}

DelugeStatus deluge_efatfs_unlink(const char* path) noexcept {
	std::string p(path);
	auto it = g_entries.find(p);
	if (it == g_entries.end()) {
		return DELUGE_ERR_NOT_FOUND;
	}
	g_entries.erase(it);
	return DELUGE_OK;
}

DelugeStatus deluge_efatfs_rename(const char* old_path, const char* new_path) noexcept {
	if (DelugeStatus injected = injectedFor(old_path); injected != DELUGE_OK) {
		return injected;
	}
	std::string o(old_path), n(new_path);
	auto it = g_entries.find(o);
	if (it == g_entries.end()) {
		return DELUGE_ERR_NOT_FOUND;
	}
	g_entries[n] = std::move(it->second);
	g_entries.erase(it);
	return DELUGE_OK;
}

DelugeStatus deluge_efatfs_set_time(const char* path, uint16_t year, uint8_t month, uint8_t day, uint8_t hour,
                                    uint8_t minute, uint8_t second) noexcept {
	std::string p(path);
	auto it = g_entries.find(p);
	if (it == g_entries.end()) {
		return DELUGE_ERR_NOT_FOUND;
	}
	it->second.modified_time =
	    DelugeTimestamp{.year = year, .month = month, .day = day, .hour = hour, .minute = minute, .second = second};
	return DELUGE_OK;
}

} // extern "C"
