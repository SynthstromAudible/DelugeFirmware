#pragma once
#include <expected>
#include <span>
#include <string_view>

extern "C" {
#include "ff.h"
}

namespace FatFS {
enum class Error {
	DISK_ERR = FR_DISK_ERR,
	INT_ERR = FR_INT_ERR,
	NOT_READY = FR_NOT_READY,
	NO_FILE = FR_NO_FILE,
	NO_PATH = FR_NO_PATH,
	INVALID_NAME = FR_INVALID_NAME,
	DENIED = FR_DENIED,
	EXIST = FR_EXIST,
	INVALID_OBJECT = FR_INVALID_OBJECT,
	WRITE_PROTECTED = FR_WRITE_PROTECTED,
	INVALID_DRIVE = FR_INVALID_DRIVE,
	NOT_ENABLED = FR_NOT_ENABLED,
	NO_FILESYSTEM = FR_NO_FILESYSTEM,
	MKFS_ABORTED = FR_MKFS_ABORTED,
	TIMEOUT = FR_TIMEOUT,
	LOCKED = FR_LOCKED,
	NOT_ENOUGH_CORE = FR_NOT_ENOUGH_CORE,
	TOO_MANY_OPEN_FILES = FR_TOO_MANY_OPEN_FILES,
	INVALID_PARAMETER = FR_INVALID_PARAMETER
};

using FileAccessMode = uint8_t;

class File {
public:
	File(File&) = default;  // Copy constructor
	File(File&&) = default; // Move constructor
	~File() { (void)close(); }

	/* Open or create a file */
	static std::expected<File, Error> open(std::string_view path, FileAccessMode mode);

	/* Close an open file object */
	std::expected<void, Error> close();

	/* Read data from the file */
	std::expected<std::span<std::byte>, Error> read(std::span<std::byte> buffer);

	/* Write data to the file */
	std::expected<unsigned int, Error> write(std::span<std::byte> buffer);

	/* Move file pointer of the file object */
	std::expected<void, Error> lseek(size_t offset);

	/* Truncate the file */
	std::expected<void, Error> truncate();

	/* Flush cached data of the writing file */
	std::expected<void, Error> sync();

#if FF_FS_READONLY == 0 && FF_USE_STRFUNC >= 1
	/* Put a character to the file */
	int putc(char c);

	/* Put a string to the file */
	int puts(const char* str);

	/* Put a formatted string to the file */
	int printf(const char* str, ...);

	/* Get a string from the file */
	char* gets(char* buff, int len);
#endif

#if FF_USE_FORWARD
	/* Forward data to the stream */
	std::expected<size_t, Error> forward(std::function<size_t(std::span<std::byte>)> func, size_t bytes_to_forward);
#endif

#if FF_USE_EXPAND
	/* Allocate a contiguous block to the file */
	std::expected<void, Error> expand(size_t, FileAccessMode opt);
#endif

	[[nodiscard]] constexpr bool eof() const { return file_.fptr == file_.obj.objsize; }
	[[nodiscard]] constexpr bool error() const { return file_.err != 0; }

	[[nodiscard]] constexpr uintptr_t tell() const { return file_.fptr; }
	[[nodiscard]] constexpr size_t size() const { return file_.obj.objsize; }

	constexpr auto rewind() { return lseek(0); }

private:
	File() = default;

	FIL file_;
};

using FileInfo = FILINFO;

class Directory {
public:
	Directory(Directory&) = default;  // Copy constructor
	Directory(Directory&&) = default; // Move constructor
	~Directory() { (void)close(); }

	/* Open a directory */
	static std::expected<Directory, Error> open(std::string_view path);

	/* Close an open directory */
	std::expected<void, Error> close();

	/* Read a directory item */
	[[nodiscard]] std::expected<FileInfo, Error> read() const;

#if FF_USE_FIND >= 1 and FF_FS_MINIMIZE <= 1
	/* Find first file */
	std::expected<FileInfo, Error> find_first(std::string_view path, std::string_view pattern);

	/* Find next file */
	std::expected<FileInfo, Error> find_next();
#endif

	std::expected<void, Error> rewind();

	// constexpr unmount(auto path) { f_mount(0, path, 0); }

private:
	Directory() = default;

	DIR dir_;

	friend std::expected<Directory, Error> mkdir_and_open(std::string_view path);
};

/* Create a sub directory */
std::expected<void, Error> mkdir(std::string_view path);

std::expected<Directory, Error> mkdir_and_open(std::string_view path);

#if FF_FS_READONLY == 0 and FF_FS_MINIMIZE == 0
/* Delete an existing file or directory */
std::expected<void, Error> unlink(std::string_view path);

/* Rename/Move a file or directory */
std::expected<void, Error> rename(std::string_view path_old, std::string_view path_new);

/* Get file status */
std::expected<FileInfo, Error> stat(std::string_view path);
#endif

#if FF_USE_CHMOD
/* Change attribute of a file/dir */
std::expected<void, Error> chmod(std::string_view path, BYTE attr, BYTE mask);

/* Change timestamp of a file/dir */
std::expected<void, Error> utime(std::string_view path, const FILINFO* fno);
#endif

#if FF_FS_RPATH >= 1
/* Change current directory */
std::expected<void, Error> chdir(std::string_view path);

/* Change current drive */
FRESULT chdrive(std::string_view path);
#endif

#if FF_FS_RPATH == 2
/* Get current directory */
std::expected<void, Error> cwd(char* buff, size_t len);
#endif

#if FF_FS_READONLY == 0 && FF_FS_MINIMIZE == 0
/* Get number of free clusters on the drive */
std::expected<void, Error> getfree(std::string_view path, DWORD* nclst, FATFS** fatfs);
#endif

#if FF_USE_LABEL
namespace Volume {
/* Get volume label / volume serial number */
std::expected<std::pair<std::string_view, uint32_t>, Error> get_label(std::string_view path);

/* Set volume label */
std::expected<void, Error> set_label(std::string_view label);
} // namespace Volume
#endif

/* Mount/Unmount a logical drive */
std::expected<void, Error> mount(FATFS* fs, std::string_view path, BYTE opt);

#if FF_USE_MKFS
/* Create a FAT volume */
std::expected<void, Error> mkfs(std::string_view path, const MKFS_PARM* opt, void* work, UINT len);
#endif

#if FF_MULTI_PARTITION
/* Divide a physical drive into some partitions */
std::expected<void, Error> fdisk(BYTE pdrv, const LBA_t ptbl[], void* work);
#endif

#if FF_CODE_PAGE == 0
/* Set current code page */
std::expected<void, Error> set_code_page(WORD cp);
#endif

} // namespace FatFS
