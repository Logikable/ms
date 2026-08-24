/* A shared fight a test writes by hand, standing in for the server.
 *
 * It keeps whatever the run reported so a test can read it back, and hands
 * back whatever the test wrote into `fight_`.
 */
#ifndef MS_SRC_COMBAT_TEST_AUTHORITY_H_
#define MS_SRC_COMBAT_TEST_AUTHORITY_H_

#include <cstdint>
#include <string>
#include <vector>

#include "src/combat/fight_authority.h"

namespace ms {

class TestAuthority : public FightAuthority {
 public:
  explicit TestAuthority(int slots) {
    fight_.hp_fractions.assign(slots, 1.0);
    fight_.state = BossRunState::kFighting;
    fight_.self = 0;
    fight_.players.resize(2);
    fight_.players[0].name = "Dagger";
    fight_.players[0].spot = 0;
    fight_.players[1].name = "Wand";
    fight_.players[1].spot = 1;
  }

  void Report(int phase, const std::vector<SharedLine>& lines, int spot,
              const std::string& attack_name, double attack_fraction) override {
    reported_phase_ = phase;
    reported_spot_ = spot;
    reported_attack_ = attack_name;
    reported_fraction_ = attack_fraction;
    for (const SharedLine& line : lines) {
      reported_.push_back(line);
    }
  }

  bool Fetch(SharedFight& fight) override {
    if (!open_) {
      return false;
    }
    fight = fight_;
    fight_.lines.clear();
    return true;
  }

  // What the party's other player just landed on the monster in `slot`.
  void OtherLanded(int slot, int64_t damage) {
    SharedLine line;
    line.owner = 1;
    line.slot = slot;
    line.event = ++event_;
    line.damage = damage;
    fight_.lines.push_back(line);
  }

  SharedFight fight_;
  bool open_ = true;
  int reported_phase_ = -1;
  int reported_spot_ = -1;
  std::string reported_attack_;
  double reported_fraction_ = 0.0;
  std::vector<SharedLine> reported_;

 private:
  int event_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_COMBAT_TEST_AUTHORITY_H_
