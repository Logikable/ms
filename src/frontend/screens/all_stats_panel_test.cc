#include "src/frontend/screens/all_stats_panel.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "src/frontend/widgets/panel_test_base.h"
#include "src/frontend/widgets/screen_text.h"
#include "src/item/equip_instance.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

class AllStatsPanelTest : public PanelTest {
 protected:
  CharacterInstance MakeWarrior() {
    Character proto;
    proto.set_level(15);
    proto.set_job(JOB_SWORDMAN);
    proto.set_job_stage(1);
    proto.mutable_allocated_stats()->set_str(40);
    proto.mutable_allocated_stats()->set_hp(500);
    return CharacterInstance(rng_, std::move(proto));
  }

  // The panel's rows as plain characters, one per column, read off the screen
  // grid -- the borders are box-drawing, so byte offsets do not line up.
  static std::vector<std::string> Rows(ftxui::Element element) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(100),
                                                 ftxui::Dimension::Fixed(24));
    ftxui::Render(screen, element);
    std::vector<std::string> rows;
    for (int y = 0; y < screen.dimy(); ++y) {
      rows.push_back(ScreenRow(screen, y, 1, AllStatsPanel::kTotalWidth - 1));
    }
    return rows;
  }

  // The row holding `label` in its left column, or "" if no row does.
  static std::string RowWith(ftxui::Element element, const std::string& label) {
    for (const std::string& row : Rows(std::move(element))) {
      if (row.compare(1, label.size(), label) == 0) {
        return row;
      }
    }
    return "";
  }
};

TEST_F(AllStatsPanelTest, FillsTheLeftColumnBeforeTheRight) {
  CharacterInstance c = MakeWarrior();
  AllStatsPanel panel(c, &account_, {});
  // The pairings the layout comes to. Reading the left label and finding the
  // right one on the same row is the whole assertion.
  //
  // This character is a 1st job, whose Character panel holds the four percent
  // rows back until the 2nd. They are here regardless: the gate is on the
  // panel, and this screen is where all of them always are.
  EXPECT_NE(RowWith(panel.Render(), "STR").find("INT"), std::string::npos);
  EXPECT_NE(RowWith(panel.Render(), "DEX").find("LUK"), std::string::npos);
  EXPECT_NE(RowWith(panel.Render(), "Attack ").find("Ignore DEF"),
            std::string::npos);
  EXPECT_NE(RowWith(panel.Render(), "Magic Attack").find("Critical Rate"),
            std::string::npos);
  EXPECT_NE(RowWith(panel.Render(), "Final Damage").find("Critical Damage"),
            std::string::npos);
  EXPECT_NE(RowWith(panel.Render(), "Damage").find("Buff Duration"),
            std::string::npos);
  EXPECT_NE(RowWith(panel.Render(), "Boss Damage").find("Attack Speed"),
            std::string::npos);
  EXPECT_NE(RowWith(panel.Render(), "Meso Drop Rate").find("Additional EXP"),
            std::string::npos);
  EXPECT_NE(RowWith(panel.Render(), "Item Drop Rate").find("Arcane Force"),
            std::string::npos);
  // Eleven combat stats, so the right column runs a row short and the gap is
  // at the bottom of it.
  EXPECT_EQ(RowWith(panel.Render(), "Normal Damage").find_last_not_of(' '),
            static_cast<size_t>(AllStatsPanel::kColumnWidth) - 2);
  // The rule breaks the columns too: Meso Drop Rate opens the group under it
  // in the left column rather than filling the gap Normal Damage left.
  EXPECT_LT(RowWith(panel.Render(), "Meso Drop Rate").find("Meso Drop Rate"),
            static_cast<size_t>(AllStatsPanel::kColumnWidth));
}

// HP and MP are drawn as gauges on the Character panel. A number for them here
// said nothing the bar does not, so this screen is the AP stats only.
TEST_F(AllStatsPanelTest, DoesNotShowThePools) {
  CharacterInstance c = MakeWarrior();
  AllStatsPanel panel(c, &account_, {});
  std::vector<std::string> rows = Rows(panel.Render());
  for (const std::string& row : rows) {
    EXPECT_EQ(row.find("HP"), std::string::npos) << row;
    EXPECT_EQ(row.find("MP"), std::string::npos) << row;
  }
}

TEST_F(AllStatsPanelTest, ShowsTheHeadingAndNothingSpendable) {
  Character proto;
  proto.set_level(60);
  proto.set_job(JOB_ICE_LIGHTNING_WIZARD);
  proto.set_job_stage(2);
  proto.set_ap(5);
  proto.set_name("Frostbite");
  CharacterInstance c(rng_, std::move(proto));

  AllStatsPanel panel(c, &account_, {});
  std::string rendered = RenderElement(panel.Render());
  EXPECT_NE(rendered.find("Frostbite"), std::string::npos);
  EXPECT_NE(rendered.find("Lv 60 I/L Wizard"), std::string::npos);
  EXPECT_NE(rendered.find("Combat Power"), std::string::npos);
  // This screen is for reading: no AP to spend and nothing to spend it on.
  EXPECT_EQ(rendered.find("AP"), std::string::npos);
  EXPECT_EQ(rendered.find("[+]"), std::string::npos);
}

TEST_F(AllStatsPanelTest, AnAddedToStatCarriesItsBreakdown) {
  CharacterInstance c = MakeWarrior();
  sword_.mutable_base_stats()->set_str(5);
  c.PickUp(std::make_unique<EquipInstance>(sword_));
  c.Equip(0);

  AllStatsPanel panel(c, &account_, {});
  EXPECT_NE(RenderElement(panel.Render()).find("(40+5) 45"), std::string::npos);
}

// Defense is the one stat written "(base+bonus) total", so it is the only one
// that can outgrow a value column -- and when it did, it ran into the gutter
// and a column past every other value on the screen. The gap before a value
// gives way now, not the column.
TEST_F(AllStatsPanelTest, ALongValueKeepsTheColumn) {
  Character proto;
  proto.set_level(15);
  proto.set_job(JOB_SWORDMAN);
  proto.set_job_stage(1);
  (*proto.mutable_sp_by_stage())[1] = 1;
  CharacterInstance c(rng_, std::move(proto));
  Skill marks;
  marks.set_name("Marksmanship");
  marks.set_kind(SKILL_KIND_PASSIVE);
  marks.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  marks.set_max_level(1);
  marks.mutable_base()->set_attack_pct(0.25);
  std::map<std::string, Skill> skills = {{"marksmanship", marks}};
  ASSERT_TRUE(c.LearnSkill(marks, 1));

  EquipPrototype wand;
  wand.set_name("Wand");
  wand.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  // Chosen to write "(800+200) 1000", which fills the column to its edge.
  wand.mutable_base_stats()->set_magic_attack(800);
  c.PickUp(std::make_unique<EquipInstance>(wand));
  c.Equip(0);

  AllStatsPanel panel(c, &account_, skills);
  std::string magic = RowWith(panel.Render(), "Magic Attack");
  ASSERT_FALSE(magic.empty());
  ASSERT_NE(magic.find("(800+200) 1000"), std::string::npos)
      << "the value the case is built on changed: " << magic;

  // The long value sits in the left column and stops at its edge: the stat it
  // pairs with still starts the second column, and the row ends where every
  // other row does.
  EXPECT_EQ(magic.find("Critical Rate"),
            static_cast<size_t>(AllStatsPanel::kColumnWidth) + 1)
      << "[" << magic << "]";
  EXPECT_EQ(magic.find_last_not_of(' '), 2 * AllStatsPanel::kColumnWidth - 2)
      << "[" << magic << "]";
}

// A 4th job at the level Hyper Stats open at, with a different STR level in
// each allocation.
TEST_F(AllStatsPanelTest, TheFarmBossRowPicksWhoseNumbersTheseAre) {
  Character proto;
  proto.set_level(140);
  proto.set_job(JOB_HERO);
  proto.set_job_stage(4);
  (*proto.mutable_hyper_stats()
        ->mutable_farming()
        ->mutable_levels())[HYPER_STAT_FIELD_STR] = 1;
  (*proto.mutable_hyper_stats()
        ->mutable_bossing()
        ->mutable_levels())[HYPER_STAT_FIELD_STR] = 2;
  CharacterInstance c(rng_, std::move(proto));
  AllStatsPanel panel(c, &account_, {});

  EXPECT_NE(RowWith(panel.Render(), "Farm").find("Boss"), std::string::npos);
  EXPECT_NE(RowWith(panel.Render(), "STR").find("(0+30) 30"),
            std::string::npos);

  EXPECT_TRUE(panel.OnEvent(ftxui::Event::ArrowRight));
  EXPECT_EQ(panel.preset(), StatPreset::kBossing);
  EXPECT_NE(RowWith(panel.Render(), "STR").find("(0+60) 60"),
            std::string::npos);

  // Clamped at both ends, like every tab bar in the game.
  EXPECT_TRUE(panel.OnEvent(ftxui::Event::ArrowRight));
  EXPECT_EQ(panel.preset(), StatPreset::kBossing);
  EXPECT_TRUE(panel.OnEvent(ftxui::Event::ArrowLeft));
  EXPECT_EQ(panel.preset(), StatPreset::kFarming);
}

// Below the level there is nothing to pick between, and no row.
TEST_F(AllStatsPanelTest, NoFarmBossRowBeforeHyperStats) {
  CharacterInstance c = MakeWarrior();
  AllStatsPanel panel(c, &account_, {});
  EXPECT_EQ(RowWith(panel.Render(), "Farm"), "");
  EXPECT_FALSE(panel.OnEvent(ftxui::Event::ArrowRight));
}

// A party member's sheet is not the player's, so it never carries the row.
TEST_F(AllStatsPanelTest, SomebodyElsesSheetHasNoFarmBossRow) {
  Character proto;
  proto.set_level(140);
  proto.set_job(JOB_HERO);
  proto.set_job_stage(4);
  CharacterInstance c(rng_, std::move(proto));
  AllStatsPanel panel(c, /*account=*/nullptr, {});
  EXPECT_EQ(RowWith(panel.Render(), "Farm"), "");
}

// A card that measures its own width has to ask for its right margin.
TEST_F(AllStatsPanelTest, EveryRowKeepsAColumnClearOfTheRightBorder) {
  CharacterInstance c = MakeWarrior();
  AllStatsPanel panel(c, /*account=*/nullptr, {});
  std::vector<std::string> touching =
      RowsTouchingTheRightBorder(panel.Render());
  EXPECT_TRUE(touching.empty()) << (touching.empty() ? "" : touching[0]);
}

}  // namespace
}  // namespace ms
