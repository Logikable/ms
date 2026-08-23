/* A real server on the loopback for a test to talk to.
 *
 * It steps on a thread of its own, which is the one rule the server has: only
 * that thread ever touches it. A test drives the other end through a client
 * and waits for what it expects to come back.
 */
#ifndef MS_SERVER_TEST_SERVER_H_
#define MS_SERVER_TEST_SERVER_H_

#include <atomic>
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "server/server.h"
#include "src/net/socket.h"
#include "src/protos/boss.pb.h"

namespace ms {

// One fight for a test's parties to name: Normal Zakum, open at any level.
inline std::map<std::string, Boss> TestBosses() {
  std::map<std::string, Boss> bosses;
  Boss& zakum = bosses["zakum"];
  zakum.set_name("Zakum");
  zakum.add_difficulties()->set_name("Normal");
  return bosses;
}

class TestServer {
 public:
  explicit TestServer(std::map<std::string, Boss> bosses = TestBosses())
      : bosses_(std::move(bosses)) {
  }
  ~TestServer() {
    Stop();
  }

  // Listens on a port the OS picks and starts stepping. False if no socket
  // could be had.
  bool Start() {
    if (!StartSockets()) {
      return false;
    }
    std::optional<Socket> listener = Listen(0);
    if (!listener.has_value()) {
      return false;
    }
    port_ = LocalPort(*listener);
    running_ = true;
    thread_ = std::thread([this, socket = std::move(*listener)]() mutable {
      Server server(std::move(socket), bosses_, 3);
      while (running_) {
        server.Step(std::chrono::steady_clock::now(),
                    std::chrono::milliseconds(5));
      }
    });
    return true;
  }

  void Stop() {
    running_ = false;
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  int port() const {
    return port_;
  }

 private:
  std::map<std::string, Boss> bosses_;
  std::atomic<bool> running_{false};
  int port_ = 0;
  std::thread thread_;
};

}  // namespace ms

#endif  // MS_SERVER_TEST_SERVER_H_
