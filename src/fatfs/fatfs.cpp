#include "fatfs.hpp"

#define FF_TRY(expr)                                                                                                   \
	do {                                                                                                               \
		FRESULT error = (expr);                                                                                        \
		if (error != FR_OK) {                                                                                          \
			return std::unexpected(FatFS::Error(error));                                                               \
		}                                                                                                              \
	} while (0);

namespace FatFS {

std::expected<File, Error> File::open(std::string_view path, FileAccessMode mode) {
	File file;
	FF_TRY(f_open(&file.file_, path.data(), mode));
	return file;
}

std::expected<void, Error> File::close() {
	FF_TRY(f_close(&file_));
	return {};
}

std::expected<std::span<std::byte>, Error> File::read(std::span<std::byte> buffer) {
	unsigned int num_bytes_read = 0;
	FF_TRY(f_read(&file_, buffer.data(), buffer.size_bytes(), &num_bytes_read))
	return std::span{buffer.data(), num_bytes_read};
}

std::expected<unsigned int, Error> File::write(std::span<std::byte> buffer) {
	unsigned int num_bytes_written = 0;
	FF_TRY(f_write(&file_, buffer.data(), buffer.size_bytes(), &num_bytes_written));
	return num_bytes_written;
}

std::expected<void, Error> File::truncate() {
	FF_TRY(f_truncate(&file_));
	return {};
}

std::expected<void, Error> File::sync() {
	FF_TRY(f_sync(&file_));
	return {};
}

std::expected<void, Error> Directory::rewind() {
	FF_TRY(f_readdir(&dir_, nullptr));
	return {};
}

std::expected<void, Error> mkdir(std::string_view path) {
	FF_TRY(f_mkdir(path.data()));
	return {};
}

std::expected<Directory, Error> mkdir_and_open(std::string_view path) {
	Directory dir;
	FF_TRY(f_mkdir_and_open(&dir.dir_, path.data()));
	return dir;
}

#if FF_FS_READONLY == 0 and FF_FS_MINIMIZE == 0
std::expected<void, Error> unlink(std::string_view path) {
	FF_TRY(f_unlink(path.data()));
	return {};
}

std::expected<void, Error> rename(std::string_view path_old, std::string_view path_new) {
	FF_TRY(f_rename(path_old.data(), path_new.data()));
	return {};
}

std::expected<FileInfo, Error> stat(std::string_view path) {
	FileInfo info;
	FF_TRY(f_stat(path.data(), &info));
	return info;
}
#endif

} // namespace FatFS
