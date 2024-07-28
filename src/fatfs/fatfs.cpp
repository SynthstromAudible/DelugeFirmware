#include "fatfs.hpp"
#include "ff.h"
extern "C" {
FRESULT f_readdir_get_filepointer(
    DIR *dp,      /* Pointer to the open directory object */
    FILINFO *fno, /* Pointer to file information to return */
    FilePointer *filePointer);
}

#define FF_TRY(expr)                                                           \
  do {                                                                         \
    FRESULT error = (expr);                                                    \
    if (error != FR_OK) {                                                      \
      return std::unexpected(FatFS::Error(error));                             \
    }                                                                          \
  } while (0)

namespace FatFS {

std::expected<File, Error> File::open(std::string_view path,
                                      FileAccessMode mode) {
  File file{};
  FF_TRY(f_open(&file.file_, path.data(), mode));
  return file;
}

std::expected<void, Error> File::close() {
  FF_TRY(f_close(&file_));
  return {};
}

std::expected<std::span<std::byte>, Error>
File::read(std::span<std::byte> buffer) {
  unsigned int num_bytes_read = 0;
  FF_TRY(f_read(&file_, buffer.data(), buffer.size_bytes(), &num_bytes_read));
  return std::span{buffer.data(), num_bytes_read};
}

std::expected<unsigned int, Error> File::write(std::span<std::byte> buffer) {
  unsigned int num_bytes_written = 0;
  FF_TRY(
      f_write(&file_, buffer.data(), buffer.size_bytes(), &num_bytes_written));
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

std::expected<void, Error> File::lseek(size_t offset) {
  FF_TRY(f_lseek(&file_, offset));
  return {};
}

std::expected<Directory, Error> Directory::open(std::string_view path) {
  Directory dir{};
  FF_TRY(f_opendir(&dir.dir_, path.data()));
  return dir;
}

std::expected<void, Error> Directory::close() {
  FF_TRY(f_closedir(&dir_));
  return {};
}

std::expected<FileInfo, Error> Directory::read() {
  FileInfo info;
  FF_TRY(f_readdir(&dir_, &info));
  return info;
}

std::expected<std::pair<FileInfo, FilePointer>, Error>
Directory::read_and_get_filepointer() {
  FileInfo info;
  FilePointer fp;
  FF_TRY(f_readdir_get_filepointer(&dir_, &info, &fp));
  return std::make_pair(info, fp);
}

std::expected<void, Error> Directory::create_name(const char **path) {
  FF_TRY(::create_name(&dir_, path));
  return {};
}

std::expected<void, Error> Directory::find() {
  FF_TRY(dir_find(&dir_));
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
  Directory dir{};
  FF_TRY(f_mkdir_and_open(&dir.dir_, path.data()));
  return dir;
}

#if FF_FS_READONLY == 0 and FF_FS_MINIMIZE == 0
std::expected<void, Error> unlink(std::string_view path) {
  FF_TRY(f_unlink(path.data()));
  return {};
}

std::expected<void, Error> rename(std::string_view path_old,
                                  std::string_view path_new) {
  FF_TRY(f_rename(path_old.data(), path_new.data()));
  return {};
}

std::expected<FileInfo, Error> stat(std::string_view path) {
  FileInfo info;
  FF_TRY(f_stat(path.data(), &info));
  return info;
}
#endif

std::expected<bool, Error> Filesystem::mount(BYTE opt, char const* path) {
  FRESULT error = f_mount(this, path, opt);

  // From the fatfs docs for f_mount:
  // If the function with forced mounting (opt = 1) failed with FR_NOT_READY, it
  // means that the filesystem object has been registered successfully but the
  // volume is currently not ready to work. The volume mount process will be
  // attempted on subsequent file/directroy function.
  if (error == FR_OK) {
    return true;
  }
  if (error == FR_NOT_READY && opt == 1) {
    return false;
  }
// need a marker to say we have no filesystem
  return std::unexpected(FatFS::Error(error));
}

} // namespace FatFS
