#include "src/frontend/screens/boss_fight_panel.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/combat/boss_run.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {
namespace {

Mob BossMob(const std::string& name, int max_hp) {
  Mob mob;
  mob.set_name(name);
  mob.set_level(110);
  mob.set_max_hp(max_hp);
  mob.set_boss(true);
  return mob;
}

Boss Zakum() {
  Boss boss;
  boss.set_name("Zakum");
  BossDifficulty* normal = boss.add_difficulties();
  normal->set_name("Normal");
  normal->set_reset(RESET_PERIOD_DAILY);
  normal->set_time_limit_seconds(300);
  Spawn* arms = normal->add_phases()->add_spawns();
  arms->set_mob("arm");
  arms->set_count(8);
  Spawn* body = normal->add_phases()->add_spawns();
  body->set_mob("body");
  body->set_count(1);
  return boss;
}

std::unique_ptr<GameState> MakeState(int arm_hp, int body_hp) {
  std::unique_ptr<GameState> state = std::make_unique<GameState>(
      std::map<std::string, EquipPrototype>{}, std::map<std::string, Scroll>{},
      std::map<std::string, ItemPrototype>{},
      std::map<std::string, Mob>{{"arm", BossMob("Zakum's Arm", arm_hp)},
                                 {"body", BossMob("Zakum", body_hp)}},
      std::map<std::string, MapData>{});
  EquipPrototype sword;
  sword.set_name("Sword");
  sword.set_equip_type(EQUIP_TYPE_ONE_HANDED_SWORD);
  sword.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  sword.set_attack_speed(ATTACK_SPEED_AVERAGE);
  sword.mutable_base_stats()->set_attack(100);
  sword.mutable_base_stats()->set_magic_attack(100);
  state->character.PickUp(std::make_unique<EquipInstance>(sword));
  state->character.Equip(0);
  return state;
}

std::string Render(const BossRun& run) {
  // Wide enough for the whole arena: three panels and the gaps between them.
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(120),
                                               ftxui::Dimension::Fixed(30));
  ftxui::Render(screen, BossFightPanel(run));
  return screen.ToString();
}

TEST(BossFightPanelTest, TheClockCountsInMinutesAndSeconds) {
  EXPECT_EQ(FightClock(300.0), "5:00");
  EXPECT_EQ(FightClock(299.5), "5:00");
  EXPECT_EQ(FightClock(65.0), "1:05");
  EXPECT_EQ(FightClock(9.2), "0:10");
  // Only actually being out of time reads 0:00.
  EXPECT_EQ(FightClock(0.1), "0:01");
  EXPECT_EQ(FightClock(0.0), "0:00");
  EXPECT_EQ(FightClock(-5.0), "0:00");
}

TEST(BossFightPanelTest, TheHeadingNamesThePhaseAndWhatIsLeftOfIt) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1);
  Boss boss = Zakum();
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);
  EXPECT_EQ(FightHeading(run), "Normal Zakum - P1 - 100%");

  run.Abort();
  EXPECT_EQ(FightHeading(run), "Normal Zakum - Left");
}

TEST(BossFightPanelTest, EveryArmIsDrawnAndTheClockIsUnderTheHeading) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1);
  Boss boss = Zakum();
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);
  std::string out = Render(run);

  EXPECT_NE(out.find("Normal Zakum - P1 - 100%"), std::string::npos);
  EXPECT_NE(out.find("5:00"), std::string::npos);
  EXPECT_NE(out.find("You"), std::string::npos);
  // Eight arms, four to a side.
  int drawn = 0;
  for (std::size_t at = out.find("Zakum's Arm"); at != std::string::npos;
       at = out.find("Zakum's Arm", at + 1)) {
    ++drawn;
  }
  EXPECT_EQ(drawn, 8);
}

// A dead arm's bar leaves the screen, but the four rows on that side stay
// four rows: the bars beside it must not move.
TEST(BossFightPanelTest, ADeadArmLeavesItsSlotEmpty) {
  std::unique_ptr<GameState> state = MakeState(1, 1000000000);
  Boss boss = Zakum();
  BossRun run("zakum", boss, 0);
  run.Advance(*state, kBossCountdownSeconds);
  int before = 0;
  {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(90),
                                                 ftxui::Dimension::Fixed(30));
    ftxui::Render(screen, BossFightPanel(run));
    before = screen.dimy();
  }
  for (int i = 0; i < 200 && run.slots()[0].alive; ++i) {
    run.Advance(*state, 0.05);
  }
  run.Advance(*state, kBossDeathHoldSeconds);
  ASSERT_FALSE(run.slots()[0].visible);

  ftxui::Screen after = ftxui::Screen::Create(ftxui::Dimension::Fixed(90),
                                              ftxui::Dimension::Fixed(30));
  ftxui::Render(after, BossFightPanel(run));
  EXPECT_EQ(after.dimy(), before);
  // The gone arm is not drawn, and the ones still standing are.
  std::string out = after.ToString();
  EXPECT_NE(out.find("Zakum's Arm"), std::string::npos);
}

// The count-in stands where the swing name will, so the one thing about to
// change is where the eye already is.
TEST(BossFightPanelTest, TheCountdownShowsOnThePlayerPanel) {
  std::unique_ptr<GameState> state = MakeState(1000000000, 1);
  Boss boss = Zakum();
  BossRun run("zakum", boss, 0);
  EXPECT_NE(Render(run).find("3"), std::string::npos);
  run.Advance(*state, 2.5);
  EXPECT_NE(Render(run).find("1"), std::string::npos);
}

// The body stands over the player rather than off to one side.
TEST(BossFightPanelTest, TheBodyIsDrawnAboveThePlayer) {
  std::unique_ptr<GameState> state = MakeState(1, 1000000000);
  Boss boss = Zakum();
  BossRun run("zakum", boss, 0);
  for (int i = 0; i < 2000 && run.phase() == 1; ++i) {
    run.Advance(*state, 0.05);
  }
  ASSERT_EQ(run.phase(), 2);
  ASSERT_EQ(run.slots().size(), 1u);

  std::string out = Render(run);
  EXPECT_NE(out.find("Zakum"), std::string::npos);
  EXPECT_LT(out.find(" Zakum "), out.find(" You "));
}

}  // namespace
}  // namespace ms
