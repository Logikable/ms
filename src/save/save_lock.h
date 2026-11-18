/* A lock on a save, held for as long as the game has it open.
 *
 * Two windows on one save would each write their own copy over the other's,
 * and the server would see one account online twice. The lock is the
 * operating system's, so it goes with the process: a crash never leaves a save
 * locked.
 */
#ifndef MS_SRC_SAVE_LOCK_H_
#define MS_SRC_SAVE_LOCK_H_

#include <string>

namespace ms {

class SaveLock {
 public:
  // Locks the save at `save_path`, through a file beside it: the save itself
  // is replaced by a rename on every write, which a lock on it wouldn't
  // survive.
  explicit SaveLock(const std::string& save_path);
  SaveLock(const SaveLock&) = delete;
  SaveLock& operator=(const SaveLock&) = delete;
  ~SaveLock();

  // Whether another process has this save open. Any other failure to lock
  // reports false: a folder the lock can't be made in shouldn't keep the game
  // from starting.
  bool taken_elsewhere() const {
    return taken_elsewhere_;
  }

 private:
#ifdef _WIN32
  void* handle_ = nullptr;
#else
  int fd_ = -1;
#endif
  bool taken_elsewhere_ = false;
};

}  // namespace ms

#endif  // MS_SRC_SAVE_LOCK_H_
