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
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "server/server.h"
#include "src/net/socket.h"
#include "src/protos/boss.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {

// The one monster a test's fights are made of. Deep enough that a client
// swinging at it for a few seconds cannot finish it, so a test can watch a
// fight in progress rather than only its end.
inline constexpr char kTestMob[] = "zakum";
inline constexpr int64_t kTestMobHp = 100000000;

// One fight for a test's parties to name: Normal Zakum, open at any level,
// one monster standing in a room with three places to stand.
inline std::map<std::string, Boss> TestBosses() {
  std::map<std::string, Boss> bosses;
  Boss& zakum = bosses["zakum"];
  zakum.set_name("Zakum");
  BossDifficulty* normal = zakum.add_difficulties();
  normal->set_name("Normal");
  normal->set_time_limit_seconds(300);
  BossPhase* phase = normal->add_phases();
  Spawn* spawn = phase->add_spawns();
  spawn->set_mob(kTestMob);
  spawn->add_spots()->set_x(1);
  for (int i = 0; i < 3; ++i) {
    phase->add_player_spots()->set_x(i);
  }
  return bosses;
}

inline std::map<std::string, Mob> TestMobs() {
  std::map<std::string, Mob> mobs;
  mobs[kTestMob].set_name("Zakum");
  mobs[kTestMob].set_max_hp(kTestMobHp);
  return mobs;
}

class TestServer {
 public:
  explicit TestServer(std::map<std::string, Boss> bosses = TestBosses(),
                      std::map<std::string, Mob> mobs = TestMobs())
      : bosses_(std::move(bosses)), mobs_(std::move(mobs)) {
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
      Server server(std::move(socket), bosses_, mobs_, 3);
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
  std::map<std::string, Mob> mobs_;
  std::atomic<bool> running_{false};
  int port_ = 0;
  std::thread thread_;
};

}  // namespace ms

#endif  // MS_SERVER_TEST_SERVER_H_
