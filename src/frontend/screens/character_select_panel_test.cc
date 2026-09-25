#include "src/frontend/screens/character_select_panel.h"

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/character/character_stats.h"
#include "src/character/hyper_stats.h"
#include "src/character/stat_preset.h"
#include "src/frontend/placement.h"
#include "src/frontend/testing/screen_text.h"
#include "src/frontend/widgets/stat_rows.h"
#include "src/game_state.h"
#include "src/protos/character.pb.h"
#include "src/protos/save.pb.h"
#include "src/protos/skill.pb.h"
#include "src/roster.h"

namespace ms {
namespace {

// Blessing of the Fairy's level comes from the account, not the character, so
// it shows whether a card reads the account.
std::map<std::string, Skill> FairyCatalog() {
  Skill fairy;
  fairy.set_name("Blessing of the Fairy");
  fairy.set_kind(SKILL_KIND_PASSIVE);
  SkillPlacement* placement = fairy.add_placement();
  placement->set_job_advancement(JOB_ADVANCEMENT_BEGINNER);
  fairy.set_account_levels_per_level(10);
  fairy.mutable_base()->set_attack(1);
  fairy.mutable_per_level()->set_attack(1);
  return {{"blessing_of_the_fairy", fairy}};
}

class CharacterSelectPanelTest : public testing::Test {
 protected:
  CharacterSelectPanelTest() : state_({}, {}, {}, {}, {}, FairyCatalog()) {
    state_.character.SetUsername("Played");
  }

  // Another character on the account, played `stamp` seconds after the epoch.
  void AddCharacter(const std::string& name, int level, Job job,
                    int64_t stamp) {
    CharacterSave slot;
    slot.mutable_character()->set_name(name);
    slot.mutable_character()->set_level(level);
    slot.mutable_character()->set_job(job);
    slot.set_last_played_unix_seconds(stamp);
    state_.inactive_characters.push_back(slot);
  }

  ftxui::Screen Draw(const CharacterSelectPanel& panel) {
    ftxui::Screen screen =
        ftxui::Screen::Create(ftxui::Dimension::Fixed(120),
                              ftxui::Dimension::Fixed(kCharacterPanelHeight));
    ftxui::Render(screen, panel.Render());
    return screen;
  }

  GameState state_;
};

TEST_F(CharacterSelectPanelTest, TheListIsHeadedAndSortedNewestFirst) {
  AddCharacter("Older", 30, JOB_FIGHTER, 100);
  AddCharacter("Newer", 40, JOB_PAGE, 200);
  CharacterSelectPanel panel(state_);
  ftxui::Screen screen = Draw(panel);

  int header = RowIndexOf(screen, "Name");
  ASSERT_GE(header, 0);
  std::string row = ScreenRow(screen, header);
  EXPECT_NE(row.find("Job"), std::string::npos);
  EXPECT_LT(row.find("Job"), row.find("Level"));
  EXPECT_LT(row.find("Level"), row.find("Offline"));
  // The played character was added last, so they come first whatever the
  // others' timestamps.
  EXPECT_LT(RowIndexOf(screen, "Played"), RowIndexOf(screen, "Newer"));
  EXPECT_LT(RowIndexOf(screen, "Newer"), RowIndexOf(screen, "Older"));
}

TEST_F(CharacterSelectPanelTest, TheCheckSitsOnTheOfflineCharacter) {
  AddCharacter("Farmer", 30, JOB_FIGHTER, 100);
  state_.played_slot = 1;
  state_.offline_slot = 0;
  CharacterSelectPanel panel(state_);
  ftxui::Screen screen = Draw(panel);
  EXPECT_EQ(FindOnScreen(screen, "✓").y, RowIndexOf(screen, "Farmer"));
}

TEST_F(CharacterSelectPanelTest, TheCursorOpensOnTheCharacterBeingPlayed) {
  AddCharacter("Farmer", 30, JOB_FIGHTER, 100);
  state_.played_slot = 1;
  CharacterSelectPanel panel(state_);
  EXPECT_EQ(panel.selected_name(), "Played");
  EXPECT_EQ(panel.selected_slot(), 1);
  EXPECT_EQ(panel.Chosen(), CharacterAction::kMenu);
}

TEST_F(CharacterSelectPanelTest, TheButtonsAreTheLastStopOfTheRing) {
  AddCharacter("Farmer", 30, JOB_FIGHTER, 100);
  CharacterSelectPanel panel(state_);
  panel.MoveCursor(1);
  panel.MoveCursor(1);
  EXPECT_EQ(panel.Chosen(), CharacterAction::kCreate);
  EXPECT_EQ(panel.selected_slot(), -1);
  panel.MoveButton(1);
  EXPECT_EQ(panel.Chosen(), CharacterAction::kQuit);
  // Stops at the end of the row instead of wrapping.
  panel.MoveButton(1);
  EXPECT_EQ(panel.Chosen(), CharacterAction::kQuit);
  panel.MoveCursor(1);
  EXPECT_EQ(panel.Chosen(), CharacterAction::kMenu);
}

// Every entry stays on the menu wherever it opens, and what the row can't do is
// dimmed. Hiding entries would change the menu's shape from row to row.
TEST_F(CharacterSelectPanelTest, TheMenuDimsWhatTheRowCannotDo) {
  CharacterSelectPanel alone(state_);
  alone.OpenMenu();
  EXPECT_TRUE(alone.menu_open());
  // Set Offline and Delete are both refused on the only character, who is
  // checked and can't be deleted. Play isn't: on the only character it resumes
  // play, and it is the only way back into the game.
  EXPECT_EQ(alone.menu_selected(), kCharacterMenuPlay);

  AddCharacter("Farmer", 30, JOB_FIGHTER, 100);
  state_.offline_slot = 1;
  CharacterSelectPanel panel(state_);
  panel.OpenMenu();
  EXPECT_EQ(panel.menu_selected(), kCharacterMenuPlay);
  panel.MoveMenuCursor(1);
  // Set Offline, since the check is on the other character.
  EXPECT_EQ(panel.menu_selected(), kCharacterMenuSetOffline);
  panel.CloseMenu();
  EXPECT_FALSE(panel.menu_open());

  panel.MoveCursor(1);
  panel.OpenMenu();
  EXPECT_EQ(panel.menu_selected(), kCharacterMenuPlay);
  panel.MoveMenuCursor(1);
  EXPECT_EQ(panel.menu_selected(), kCharacterMenuDelete)
      << "Set Offline is dimmed on the character already checked";
}

TEST_F(CharacterSelectPanelTest, TheCardFollowsTheCursor) {
  AddCharacter("Farmer", 33, JOB_FIGHTER, 100);
  CharacterSelectPanel panel(state_);
  ftxui::Screen first = Draw(panel);
  EXPECT_GE(RowIndexOf(first, "Lv  1 Beginner"), 0);

  panel.MoveCursor(1);
  ftxui::Screen second = Draw(panel);
  EXPECT_GE(RowIndexOf(second, "Lv 33 Fighter"), 0);
  EXPECT_GE(RowIndexOf(second, "HP"), 0);
  EXPECT_GE(RowIndexOf(second, "STR"), 0);
}

// The card must not keep showing someone who is gone, so a cursor that was on a
// character comes back onto one.
TEST_F(CharacterSelectPanelTest, RefreshKeepsTheCursorOnACharacter) {
  AddCharacter("Farmer", 30, JOB_FIGHTER, 100);
  CharacterSelectPanel panel(state_);
  panel.MoveCursor(1);
  ASSERT_EQ(panel.selected_name(), "Farmer");

  state_.inactive_characters.clear();
  panel.Refresh();
  EXPECT_EQ(panel.selected_name(), "Played");
  EXPECT_EQ(RowIndexOf(Draw(panel), "Farmer"), -1);
}

// Both windows are the same fixed height, so moving through the list changes
// nothing and their borders line up. Drawn centred, as on the real screen,
// which is what keeps a window at the height it asked for.
TEST_F(CharacterSelectPanelTest, TheTwoWindowsAreTheSameHeight) {
  CharacterSelectPanel panel(state_);
  ftxui::Screen screen =
      ftxui::Screen::Create(ftxui::Dimension::Fixed(kMinTerminalColumns),
                            ftxui::Dimension::Fixed(kMinTerminalRows));
  ftxui::Render(screen, Centred(panel.Render()));
  std::vector<std::string> rows = ScreenRows(screen);
  int top = -1;
  int bottom = -1;
  for (int y = 0; y < static_cast<int>(rows.size()); ++y) {
    // When the two windows are the same height, their corners are on the same
    // row.
    if (rows[y].find("╭") != std::string::npos) {
      EXPECT_EQ(top, -1) << "one row of top borders, not two";
      top = y;
    }
    if (rows[y].find("╰") != std::string::npos) {
      bottom = y;
    }
  }
  ASSERT_GE(top, 0);
  EXPECT_EQ(bottom - top + 1, kCharacterPanelHeight);
}

// The card shows the character as playing them would. A saved sheet carries
// none of the account behind it, so reading it alone would leave out Link
// Skills, the account's progress and the autoswap setting.
TEST_F(CharacterSelectPanelTest, TheCardReadsTheAccountBehindThem) {
  AddCharacter("Farmer", 30, JOB_FIGHTER, 100);
  state_.account.RecordProgress(/*level=*/137, /*job_stage=*/4);
  CharacterSelectPanel panel(state_);
  panel.MoveCursor(1);
  ASSERT_EQ(panel.selected_name(), "Farmer");
  int slot = panel.selected_slot();
  ftxui::Screen screen = Draw(panel);
  int row = RowIndexOf(screen, "Combat Power");
  ASSERT_GE(row, 0);
  std::string card = ScreenRow(screen, row);

  // The same character put into play, where the account's fields really are
  // applied.
  ASSERT_TRUE(PlayCharacter(state_, slot));
  const std::string played = CombatPowerText(CharacterCombatPower(
      state_.character, state_.skills, Activity::kFarming));
  EXPECT_NE(card.find(played), std::string::npos)
      << "the card reads " << card << ", playing them reads " << played;
}

// Two chips, the same pair as the Character panel's Stats tab, reached with the
// arrows from the list. Only with the autoswap on, since one allocation for
// everything leaves nothing to pick.
TEST_F(CharacterSelectPanelTest, LeftAndRightMoveTheCardsActivity) {
  AddCharacter("Ranger", kHyperStatUnlockLevel, JOB_FIGHTER, 100);
  state_.account.RecordProgress(kHyperStatUnlockLevel, /*job_stage=*/4);
  CharacterSelectPanel panel(state_);
  panel.MoveCursor(1);
  EXPECT_EQ(RowIndexOf(Draw(panel), "Farm"), -1) << "the switch is off";

  // The card holds the character it drew last, so the new setting reaches it
  // the next time the screen reads the roster.
  state_.account.SetAutoswapPresets(true);
  panel.Refresh();
  ASSERT_EQ(panel.selected_name(), "Ranger");
  ASSERT_GE(RowIndexOf(Draw(panel), "Farm"), 0);
  EXPECT_EQ(panel.activity(), Activity::kFarming);
  panel.SwitchActivity(1);
  EXPECT_EQ(panel.activity(), Activity::kBossing);
  panel.SwitchActivity(-1);
  EXPECT_EQ(panel.activity(), Activity::kFarming);

  // The button row's own arrows can't reach the chips.
  panel.MoveCursor(1);
  ASSERT_EQ(panel.selected_slot(), -1);
  panel.SwitchActivity(1);
  EXPECT_EQ(panel.activity(), Activity::kFarming);
}

// The list and the card beside it are fitted to their own rows, and the menu
// opens over both.
TEST_F(CharacterSelectPanelTest, NothingWeldsARowToTheRightBorder) {
  AddCharacter("Older", 30, JOB_FIGHTER, 100);
  AddCharacter("Newer", 200, JOB_DARK_KNIGHT, 200);
  CharacterSelectPanel panel(state_);
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel.Render()).empty());
  panel.OpenMenu();
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel.Render()).empty());
}
}  // namespace
}  // namespace ms
