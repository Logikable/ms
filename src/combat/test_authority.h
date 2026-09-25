/* A fake server for tests: a shared fight the test writes by hand.
 *
 * It stores whatever the run reports so the test can check it, and returns
 * whatever the test put in `fight_`.
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

  void Report(const FightReport& report) override {
    ++reports_;
    reported_phase_ = report.phase;
    reported_spot_ = report.spot;
    reported_attack_ = report.attack_name;
    reported_drop_pct_ = report.item_drop_pct;
    for (const SharedLine& line : report.lines) {
      reported_.push_back(line);
    }
    reported_breakdown_ = report.breakdown;
  }

  bool Fetch(SharedFight& fight) override {
    if (!open_) {
      return false;
    }
    fight = fight_;
    fight_.lines.clear();
    return true;
  }

  // Adds a hit from the party's other player on the monster in `slot`.
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
  // Number of reports received, for tests about report timing rather than
  // content.
  int reports_ = 0;
  int reported_phase_ = -1;
  int reported_spot_ = -1;
  std::string reported_attack_;
  double reported_drop_pct_ = 0.0;
  std::vector<SharedLine> reported_;
  std::vector<BreakdownRow> reported_breakdown_;

 private:
  int event_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_COMBAT_TEST_AUTHORITY_H_
