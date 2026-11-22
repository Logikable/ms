#include "analysis/parallel.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace ms {
namespace {

// One ParallelFor call: its indices are handed out one at a time, because
// bodies vary in size. One branch may climb for two simulated days and another
// for one, and a thread given the slow half would finish alone.
struct Task {
  const std::function<void(int)>* body = nullptr;
  int count = 0;
  std::atomic<int> next{0};
  std::atomic<int> finished{0};
};

// One worker per core beside the caller, kept for the whole process. A call
// made from inside a body is helped by whichever workers are idle, so a sweep's
// slow branches pick up the cores its finished ones leave.
class Pool {
 public:
  static Pool& Get() {
    static Pool* const pool = new Pool();
    return *pool;
  }

  void Run(int count, const std::function<void(int)>& body) {
    std::shared_ptr<Task> task = std::make_shared<Task>();
    task->body = &body;
    task->count = count;
    {
      std::lock_guard<std::mutex> lock(mu_);
      tasks_.push_back(task);
    }
    wake_.notify_all();
    Work(*task);
    std::unique_lock<std::mutex> lock(mu_);
    tasks_.erase(std::find(tasks_.begin(), tasks_.end(), task));
    // Helpers may still be inside an index they took.
    done_.wait(lock, [&] { return task->finished.load() == count; });
  }

 private:
  Pool() {
    const unsigned int cores =
        std::max(1u, std::thread::hardware_concurrency());
    for (unsigned int i = 1; i < cores; ++i) {
      std::thread([this] { Loop(); }).detach();
    }
  }

  void Work(Task& task) {
    for (int i = task.next++; i < task.count; i = task.next++) {
      (*task.body)(i);
      if (++task.finished == task.count) {
        // Under the lock, so the caller can't miss it between its check and
        // its wait.
        std::lock_guard<std::mutex> lock(mu_);
        done_.notify_all();
      }
    }
  }

  // The newest task with indices left, which is the innermost call: finishing
  // it is what lets the call around it go on.
  std::shared_ptr<Task> Open() const {
    for (auto it = tasks_.rbegin(); it != tasks_.rend(); ++it) {
      if ((*it)->next.load() < (*it)->count) {
        return *it;
      }
    }
    return nullptr;
  }

  void Loop() {
    while (true) {
      std::shared_ptr<Task> task;
      {
        std::unique_lock<std::mutex> lock(mu_);
        wake_.wait(lock, [&] { return (task = Open()) != nullptr; });
      }
      Work(*task);
    }
  }

  std::mutex mu_;
  std::condition_variable wake_;
  std::condition_variable done_;
  std::vector<std::shared_ptr<Task>> tasks_;
};

}  // namespace

void ParallelFor(int count, const std::function<void(int)>& body) {
  if (count <= 0) {
    return;
  }
  if (count == 1) {
    body(0);
    return;
  }
  Pool::Get().Run(count, body);
}

}  // namespace ms
