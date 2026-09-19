#include "src/frontend/screens/character_select_panel.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/placement.h"
#include "src/frontend/testing/screen_text.h"
#include "src/game_state.h"
#include "src/protos/character.pb.h"
#include "src/protos/save.pb.h"
#include "src/roster.h"

namespace ms {
namespace {

class CharacterSelectPanelTest : public testing::Test {
 protected:
  CharacterSelectPanelTest() : state_({}, {}, {}, {}, {}) {
    state_.character.SetUsername("Played");
  }

  // Another character on the account, played `stamp` seconds into the epoch.
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
  // The played character was put in last, so they lead whatever the others
  // were stamped.
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
  // Clamped at the end of the row rather than wrapping.
  panel.MoveButton(1);
  EXPECT_EQ(panel.Chosen(), CharacterAction::kQuit);
  panel.MoveCursor(1);
  EXPECT_EQ(panel.Chosen(), CharacterAction::kMenu);
}

// Every entry stays on the menu wherever it is raised; what the row cannot do
// is dimmed. Hidden entries would make the menu a different shape per row.
TEST_F(CharacterSelectPanelTest, TheMenuDimsWhatTheRowCannotDo) {
  CharacterSelectPanel alone(state_);
  alone.OpenMenu();
  EXPECT_TRUE(alone.menu_open());
  // Play, Set Offline and Delete are all refused on the only character, who
  // is played and checked, so the cursor falls through to Close.
  EXPECT_EQ(alone.menu_selected(), kCharacterMenuClose);

  AddCharacter("Farmer", 30, JOB_FIGHTER, 100);
  state_.offline_slot = 1;
  CharacterSelectPanel panel(state_);
  panel.OpenMenu();
  // On the played row the cursor skips Play, which is dimmed, and stops on
  // Set Offline -- the check is on the other character.
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

// Both windows are one fixed height, so walking the list moves nothing and
// their borders line up. Drawn where the screen actually stands it: centred,
// which is what holds a window to the height it asked for.
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
    // Both windows' corners are on one row when the two are the same height.
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

}  // namespace
}  // namespace ms
