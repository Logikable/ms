/* A real server on loopback for tests to talk to.
 *
 * The server runs on its own thread, and only that thread touches it. A test
 * connects a client and waits for the replies it expects.
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
#include "src/multiplayer/protocol.h"
#include "src/net/socket.h"
#include "src/protos/boss.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {

// The mob in every test fight. Its HP is high enough that a client cannot
// kill it in a few seconds, so tests can watch a fight in progress.
inline constexpr char kTestMob[] = "zakum";
inline constexpr int64_t kTestMobHp = 100000000;
// The test boss always drops this, so a test can check that exactly one
// player receives it.
inline constexpr char kTestDrop[] = "shard";

// Returns one test boss: Normal Zakum, open at any level, with one mob and
// three player spots.
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
  MobDrop* shard = normal->add_drops();
  shard->set_item(kTestDrop);
  shard->set_per_kill(1.0);
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
                      std::map<std::string, Mob> mobs = TestMobs(),
                      int protocol_version = kMultiplayerVersion)
      : bosses_(std::move(bosses)),
        mobs_(std::move(mobs)),
        protocol_version_(protocol_version) {
  }
  ~TestServer() {
    Stop();
  }

  // Listens on a port the OS picks and starts the server thread. Returns false
  // if it could not open a socket.
  bool Start() {
    if (!StartSockets()) {
      return false;
    }
    std::optional<Socket> listener = Listen(0);
    if (!listener.has_value()) {
      return false;
    }
    port_ = LocalPort(*listener);
    Serve(std::move(*listener));
    return true;
  }

  // Restarts on the same port with `protocol_version`, as a deploy would. Lets
  // a test check that a refused client gets in after the update.
  bool RestartSpeaking(int protocol_version) {
    Stop();
    protocol_version_ = protocol_version;
    std::optional<Socket> listener = Listen(port_);
    if (!listener.has_value()) {
      return false;
    }
    Serve(std::move(*listener));
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

  // Moves the server's clock forward by `by`. Tests use this to skip the
  // three-second boss countdown; the server's other timers are minutes long,
  // so a small jump does not affect them. Safe to call while running.
  void SkipAhead(std::chrono::milliseconds by) {
    skipped_ms_ += by.count();
  }

 private:
  // Runs the server on its own thread until Stop().
  void Serve(Socket listener) {
    running_ = true;
    thread_ = std::thread([this, socket = std::move(listener)]() mutable {
      Server server(std::move(socket), bosses_, mobs_, 3, protocol_version_);
      while (running_) {
        server.Step(std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(skipped_ms_.load()),
                    std::chrono::milliseconds(5));
      }
    });
  }

  std::map<std::string, Boss> bosses_;
  std::map<std::string, Mob> mobs_;
  int protocol_version_ = kMultiplayerVersion;
  std::atomic<bool> running_{false};
  std::atomic<int64_t> skipped_ms_{0};
  int port_ = 0;
  std::thread thread_;
};

}  // namespace ms

#endif  // MS_SERVER_TEST_SERVER_H_
