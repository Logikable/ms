#include "src/save_lock.h"

#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace ms {

#ifdef _WIN32
SaveLock::SaveLock(const std::string& save_path) {
  std::string path = save_path + ".lock";
  // No sharing: every other open of the file fails until this handle closes.
  HANDLE handle =
      CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                  OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    taken_elsewhere_ = GetLastError() == ERROR_SHARING_VIOLATION;
    return;
  }
  handle_ = handle;
}

SaveLock::~SaveLock() {
  if (handle_ != nullptr) {
    CloseHandle(static_cast<HANDLE>(handle_));
  }
}
#else
SaveLock::SaveLock(const std::string& save_path) {
  std::string path = save_path + ".lock";
  int fd = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
  if (fd < 0) {
    return;
  }
  if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
    taken_elsewhere_ = errno == EWOULDBLOCK;
    close(fd);
    return;
  }
  fd_ = fd;
}

SaveLock::~SaveLock() {
  if (fd_ >= 0) {
    close(fd_);
  }
}
#endif

}  // namespace ms
