#include "analysis/sim_boss.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>

#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/mob.pb.h"
#include "src/testing/prototypes.h"

namespace ms {
namespace {

Mob MakeMob(const std::string& name, int64_t max_hp) {
  Mob mob;
  mob.set_name(name);
  mob.set_level(110);
  mob.set_max_hp(max_hp);
  mob.set_boss(true);
  return mob;
}

BossPhase* AddPhase(BossDifficulty* difficulty, const std::string& mob) {
  BossPhase* phase = difficulty->add_phases();
  Spawn* spawn = phase->add_spawns();
  spawn->set_mob(mob);
  spawn->add_spots()->set_x(2);
  ArenaSpot* stand = phase->add_player_spots();
  stand->set_x(2);
  stand->set_y(1);
  return phase;
}

// A one-HP opener, then a body a plain sword can't dent in the time limit.
std::unique_ptr<GameState> WallState() {
  std::unique_ptr<GameState> state = std::make_unique<GameState>(
      std::map<std::string, EquipPrototype>{}, std::map<std::string, Scroll>{},
      std::map<std::string, ItemPrototype>{},
      std::map<std::string, Mob>{{"opener", MakeMob("Opener", 1)},
                                 {"wall", MakeMob("Wall", 1000000000000)}},
      std::map<std::string, MapData>{});
  Boss boss;
  boss.set_name("Wall");
  BossDifficulty* normal = boss.add_difficulties();
  normal->set_name("Normal");
  normal->set_reset(RESET_PERIOD_DAILY);
  normal->set_time_limit_seconds(1800);
  AddPhase(normal, "opener");
  AddPhase(normal, "wall");
  state->bosses["wall"] = boss;
  state->character.PickUp(std::make_unique<EquipInstance>(PlainSword()));
  state->character.Equip(0);
  return state;
}

// Clearing the opener is half the phases but none of the HP, so the fight is
// nearly all left. The player walks out early, and the projection carries the
// pace they left at to the time limit.
TEST(FightBossTest, AWalkOutReadsByHpAndAtItsPace) {
  std::unique_ptr<GameState> state = WallState();
  BossOutcome outcome = FightBoss(*state, "wall", 0);
  EXPECT_FALSE(outcome.won);
  EXPECT_LT(outcome.seconds, 1800.0);
  EXPECT_GT(outcome.left, 0.999);
  EXPECT_LT(outcome.left, 1.0);
  EXPECT_NEAR(1.0 - outcome.left_at_clock,
              (1.0 - outcome.left) * 1800.0 / outcome.seconds, 1e-9);
}

}  // namespace
}  // namespace ms
