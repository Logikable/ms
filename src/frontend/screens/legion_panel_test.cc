#include "src/frontend/screens/legion_panel.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/character/legion.h"
#include "src/character/stat_preset.h"
#include "src/frontend/testing/screen_text.h"
#include "src/game_state.h"
#include "src/protos/character.pb.h"
#include "src/protos/legion.pb.h"
#include "src/protos/save.pb.h"

namespace ms {
namespace {

class LegionPanelTest : public testing::Test {
 protected:
  LegionPanelTest() : state_({}, {}, {}, {}, {}, {}) {
    state_.character.SetUsername("Played");
  }

  void AddCharacter(const std::string& name, int level, Job job) {
    CharacterSave slot;
    slot.mutable_character()->set_name(name);
    slot.mutable_character()->set_level(level);
    slot.mutable_character()->set_job(job);
    state_.inactive_characters.push_back(slot);
    state_.account.RecordProgress(level, 4);
    state_.MirrorAccount();
  }

  // `count` level-250 Heroes, 5 points each. Sixteen make Legion level 4,000,
  // rank 8, where the expanded stats cap at 21.
  void AddHeroes(int count) {
    for (int i = 0; i < count; ++i) {
      AddCharacter("Hero" + std::to_string(i), 250, JOB_HERO);
    }
  }

  ftxui::Screen Draw(const LegionPanel& panel) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(120),
                                                 ftxui::Dimension::Fixed(30));
    ftxui::Render(screen, panel.Render());
    return screen;
  }

  // Down from the tab row to `stat`, through the preset row.
  void CursorTo(LegionPanel& panel, LegionStat stat) {
    for (int guard = 0; guard < 20 && !(panel.zone() == LegionZone::kStat &&
                                        panel.stat() == stat);
         ++guard) {
      panel.MoveRow(1);
    }
    ASSERT_EQ(panel.stat(), stat);
  }

  int StoredPoints(StatPreset slot, LegionStat stat) const {
    const LegionPreset& preset = PresetOf(state_.account.legion(), slot);
    return preset.points().contains(stat) ? preset.points().at(stat) : 0;
  }

  GameState state_;
};

// The base eight, a rule, the other eight, a rule, and [Reset].
TEST_F(LegionPanelTest, TheGridListsSixteenStatsInTwoHalves) {
  AddHeroes(16);
  LegionPanel panel(state_);
  panel.Reset();
  ftxui::Screen screen = Draw(panel);
  const int magic = RowIndexOf(screen, "Magic Attack");
  const int resist = RowIndexOf(screen, "Status Resistance");
  ASSERT_GE(magic, 0);
  EXPECT_EQ(resist, magic + 2) << "a rule between the halves";
  const int crit = RowIndexOf(screen, "Critical Damage");
  EXPECT_EQ(RowIndexOf(screen, "[Reset]"), crit + 2) << "a rule above Reset";
  EXPECT_EQ(RowIndexOf(screen, "STR"), RowIndexOf(screen, "80 Points") + 2);
}

// An expanded stat shows its cap beside its points; a base stat's never moves,
// so it shows the points alone.
TEST_F(LegionPanelTest, ExpandedStatsShowTheirCap) {
  AddHeroes(16);
  LegionPanel panel(state_);
  panel.Reset();
  ftxui::Screen screen = Draw(panel);
  const std::string boss = ScreenRow(screen, RowIndexOf(screen, "Boss Damage"));
  EXPECT_NE(boss.find("0/21"), std::string::npos) << boss;
  const std::string str = ScreenRow(screen, RowIndexOf(screen, "STR"));
  EXPECT_EQ(str.find("/"), std::string::npos) << str;
  EXPECT_NE(str.find("+0"), std::string::npos) << str;
}

// Below Nameless IV the expanded stats have no cap: their rows grey and the
// cursor goes from Magic Attack straight to [Reset].
TEST_F(LegionPanelTest, ClosedStatsAreGreyAndSkipped) {
  AddHeroes(2);
  LegionPanel panel(state_);
  panel.Reset();
  CursorTo(panel, LEGION_STAT_MAGIC_ATTACK);
  panel.MoveRow(1);
  EXPECT_EQ(panel.zone(), LegionZone::kReset);
  ftxui::Screen screen = Draw(panel);
  EXPECT_TRUE(PixelOf(screen, "Boss Damage").dim);
  EXPECT_FALSE(PixelOf(screen, "Magic Attack").dim);
}

// [+] spends into the account's preset and the character reads it at once; the
// amount column follows.
TEST_F(LegionPanelTest, PlusSpendsIntoTheAccount) {
  AddHeroes(2);
  LegionPanel panel(state_);
  panel.Reset();
  CursorTo(panel, LEGION_STAT_STR);
  panel.MoveColumn(1);
  panel.Activate();
  panel.Activate();
  EXPECT_EQ(StoredPoints(StatPreset::kFirst, LEGION_STAT_STR), 2);
  EXPECT_EQ(state_.character.legion().SerializeAsString(),
            state_.account.legion().SerializeAsString());
  ftxui::Screen screen = Draw(panel);
  const std::string str = ScreenRow(screen, RowIndexOf(screen, "STR"));
  EXPECT_NE(str.find("+10"), std::string::npos) << str;
  EXPECT_NE(ScreenText(screen).find("8 Points"), std::string::npos);

  panel.MoveColumn(-1);
  panel.Activate();
  EXPECT_EQ(StoredPoints(StatPreset::kFirst, LEGION_STAT_STR), 1);
}

// A preset holding more than its cap (the rank fell) shows what counts, and
// [-] moves from that number rather than the stored one.
TEST_F(LegionPanelTest, MinusMovesFromWhatCounts) {
  AddHeroes(16);
  (*PresetOf(*state_.account.mutable_legion(), StatPreset::kFirst)
        .mutable_points())[LEGION_STAT_BOSS_DAMAGE] = 30;
  state_.MirrorAccount();
  LegionPanel panel(state_);
  panel.Reset();
  ftxui::Screen screen = Draw(panel);
  EXPECT_NE(ScreenRow(screen, RowIndexOf(screen, "Boss Damage")).find("21/21"),
            std::string::npos);

  CursorTo(panel, LEGION_STAT_BOSS_DAMAGE);
  panel.MoveColumn(-1);
  panel.Activate();
  EXPECT_EQ(StoredPoints(StatPreset::kFirst, LEGION_STAT_BOSS_DAMAGE), 20);
}

// Points past the Legion's total count for nothing, and the first edit drops
// them, so the grid never holds more than it shows.
TEST_F(LegionPanelTest, AnEditDropsWhatNoLongerCounts) {
  AddHeroes(8);  // 40 points
  LegionPreset& preset =
      PresetOf(*state_.account.mutable_legion(), StatPreset::kFirst);
  (*preset.mutable_points())[LEGION_STAT_STR] = 15;
  (*preset.mutable_points())[LEGION_STAT_DEX] = 15;
  (*preset.mutable_points())[LEGION_STAT_INT] = 15;
  (*preset.mutable_points())[LEGION_STAT_LUK] = 5;
  state_.MirrorAccount();
  LegionPanel panel(state_);
  panel.Reset();
  ftxui::Screen screen = Draw(panel);
  EXPECT_NE(ScreenRow(screen, RowIndexOf(screen, "INT")).find(" 10 "),
            std::string::npos);
  EXPECT_NE(ScreenText(screen).find("0 Points"), std::string::npos);

  CursorTo(panel, LEGION_STAT_STR);
  panel.MoveColumn(-1);
  panel.Activate();
  EXPECT_EQ(StoredPoints(StatPreset::kFirst, LEGION_STAT_STR), 14);
  EXPECT_EQ(StoredPoints(StatPreset::kFirst, LEGION_STAT_INT), 10);
  EXPECT_EQ(StoredPoints(StatPreset::kFirst, LEGION_STAT_LUK), 0);
}

// [Reset] is a question the controller asks; the panel only empties the shown
// preset once it is answered.
TEST_F(LegionPanelTest, ResetEmptiesOnlyTheShownPreset) {
  AddHeroes(2);
  LegionPanel panel(state_);
  panel.Reset();
  CursorTo(panel, LEGION_STAT_DEX);
  panel.Activate();
  panel.MoveRow(-1);
  panel.MoveRow(-1);
  ASSERT_EQ(panel.zone(), LegionZone::kPresets);
  panel.MoveColumn(1);
  CursorTo(panel, LEGION_STAT_DEX);
  panel.Activate();

  while (panel.zone() != LegionZone::kReset) {
    panel.MoveRow(1);
  }
  EXPECT_TRUE(panel.Activate());
  panel.ResetPreset();
  EXPECT_EQ(StoredPoints(StatPreset::kSecond, LEGION_STAT_DEX), 0);
  EXPECT_EQ(StoredPoints(StatPreset::kFirst, LEGION_STAT_DEX), 1);
}

// The preset row's Enter opens the Use/Move menu, with Use shut while the
// autoswap chooses.
TEST_F(LegionPanelTest, ThePresetRowOpensItsMenu) {
  AddHeroes(2);
  LegionPanel panel(state_);
  panel.Reset();
  panel.MoveRow(1);
  ASSERT_EQ(panel.zone(), LegionZone::kPresets);
  panel.Activate();
  EXPECT_TRUE(panel.preset_menu_open());
  EXPECT_NE(ScreenText(Draw(panel)).find("Move"), std::string::npos);
}

// Ranked characters only, highest first, the played one among them. Those past
// the member count dim who they are, but not their effect, which still counts.
TEST_F(LegionPanelTest, MembersListTheRankedFromTheTop) {
  // Legion level 10 x 60 = 600: Nameless I, nine members.
  for (int i = 0; i < 10; ++i) {
    AddCharacter("Bandit" + std::to_string(i), 60, JOB_BANDIT);
  }
  AddCharacter("Low", 59, JOB_HERO);
  LegionPanel panel(state_);
  panel.Reset();
  panel.MoveColumn(1);
  ASSERT_EQ(panel.tab(), LegionTab::kMembers);

  const std::vector<LegionMemberRow> members = panel.Members();
  ASSERT_EQ(members.size(), 10u) << "the played Beginner and Low are unranked";
  EXPECT_TRUE(members[8].gives_points);
  EXPECT_FALSE(members[9].gives_points);

  ftxui::Screen screen = Draw(panel);
  const int header = RowIndexOf(screen, "Name");
  const std::string head = ScreenRow(screen, header);
  EXPECT_LT(head.find("Name"), head.find("Level"));
  EXPECT_LT(head.find("Level"), head.find("Rank"));
  EXPECT_LT(head.find("Rank"), head.find("Effect"));
  EXPECT_EQ(RowIndexOf(screen, "Low"), -1);
  EXPECT_NE(ScreenRow(screen, RowIndexOf(screen, "Bandit0")).find("LUK +10"),
            std::string::npos);
  EXPECT_FALSE(PixelOf(screen, "Bandit8").dim);
  EXPECT_TRUE(PixelOf(screen, "Bandit9").dim);
  const int last = RowIndexOf(screen, "Bandit9");
  EXPECT_FALSE(screen.PixelAt(FindOnScreen(screen, "LUK +10").x, last).dim);
}

TEST_F(LegionPanelTest, MembersAreOrderedByLevel) {
  AddCharacter("Middle", 150, JOB_HERO);
  AddCharacter("Top", 250, JOB_HERO);
  AddCharacter("Bottom", 70, JOB_HERO);
  LegionPanel panel(state_);
  panel.Reset();
  panel.MoveColumn(1);
  ftxui::Screen screen = Draw(panel);
  EXPECT_LT(RowIndexOf(screen, "Top"), RowIndexOf(screen, "Middle"));
  EXPECT_LT(RowIndexOf(screen, "Middle"), RowIndexOf(screen, "Bottom"));
  EXPECT_NE(ScreenRow(screen, RowIndexOf(screen, "Top")).find("SSS"),
            std::string::npos);
}

// Up and Down scroll a list taller than the window, and stop at its ends.
TEST_F(LegionPanelTest, UpAndDownScrollTheMembers) {
  for (int i = 0; i < LegionPanel::kMemberRows + 3; ++i) {
    AddCharacter("M" + std::to_string(100 + i), 200 - i, JOB_HERO);
  }
  LegionPanel panel(state_);
  panel.Reset();
  panel.MoveColumn(1);
  EXPECT_NE(RowIndexOf(Draw(panel), "M100"), -1);
  panel.MoveRow(1);
  EXPECT_EQ(RowIndexOf(Draw(panel), "M100"), -1);
  for (int i = 0; i < 10; ++i) {
    panel.MoveRow(1);
  }
  EXPECT_EQ(panel.first_member(), 3);
  EXPECT_NE(RowIndexOf(Draw(panel), "M121"), -1);
  panel.MoveRow(-5);
  EXPECT_EQ(panel.first_member(), 0);
}

}  // namespace
}  // namespace ms
