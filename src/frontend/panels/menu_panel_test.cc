#include "src/frontend/panels/menu_panel.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/combat/battle_analysis.h"
#include "src/frontend/types.h"
#include "src/frontend/widgets/screen_text.h"
#include "src/game_state.h"

namespace ms {
namespace {

// The level the Boss entry arrives at. Written out rather than read off the
// progression table, so moving the gate is a decision the test notices.
constexpr int kBossLevel = 110;

GameState EmptyState() {
  return GameState({}, {}, {}, {}, {});
}

void LevelTo(GameState& state, int level) {
  while (state.character.proto().level() < level) {
    state.character.LevelUp();
  }
}

std::string Render(const MenuPanel& panel) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(40),
                                               ftxui::Dimension::Fixed(3));
  ftxui::Render(screen, panel.Render());
  return screen.ToString();
}

std::string RenderBox(const MenuPanel& panel) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(20),
                                               ftxui::Dimension::Fixed(5));
  ftxui::Render(screen, panel.RenderBox());
  return screen.ToString();
}

// Puts the cursor on `entry` and opens the box it raises, as pressing Enter
// on that entry does.
void OpenBoxOn(MenuPanel& panel, MenuEntry entry) {
  while (panel.selected() != entry) {
    panel.MoveCursor(1);
  }
  panel.OpenBox(entry);
}

TEST(MenuPanelTest, BossArrivesLeftOfTheEntriesThatWereAlreadyThere) {
  GameState state = EmptyState();
  BattleAnalysis analysis;
  int focus = kMenuPanel;
  MenuPanel panel(state, analysis, focus);
  std::string early = Render(panel);
  EXPECT_NE(early.find("Analysis"), std::string::npos);
  EXPECT_NE(early.find("Settings"), std::string::npos);
  EXPECT_EQ(early.find("Boss"), std::string::npos);

  LevelTo(state, kBossLevel);
  std::string later = Render(panel);
  EXPECT_LT(later.find("Boss"), later.find("Analysis"));
  EXPECT_LT(later.find("Analysis"), later.find("Settings"));
}

// The row is the entries and nothing else: no brackets, two columns between
// them, and a column of clearance inside each border.
TEST(MenuPanelTest, TheEntriesSitTwoColumnsApart) {
  GameState state = EmptyState();
  LevelTo(state, kBossLevel);
  BattleAnalysis analysis;
  int focus = kMenuPanel;
  MenuPanel panel(state, analysis, focus);
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(40),
                                               ftxui::Dimension::Fixed(3));
  ftxui::Render(screen, ftxui::hbox({panel.Render(), ftxui::filler()}));
  EXPECT_NE(ScreenRow(screen, 1).find("│ Boss  Party  Analysis  Settings │"),
            std::string::npos);
}

TEST(MenuPanelTest, TheCursorWrapsAndPicksAnEntry) {
  GameState state = EmptyState();
  LevelTo(state, kBossLevel);
  BattleAnalysis analysis;
  int focus = kMenuPanel;
  MenuPanel panel(state, analysis, focus);
  EXPECT_EQ(panel.selected(), MenuEntry::kBoss);
  panel.MoveCursor(1);
  EXPECT_EQ(panel.selected(), MenuEntry::kParty);
  panel.MoveCursor(1);
  EXPECT_EQ(panel.selected(), MenuEntry::kAnalysis);
  panel.MoveCursor(1);
  EXPECT_EQ(panel.selected(), MenuEntry::kSettings);
  // Off the end and back to the start.
  panel.MoveCursor(1);
  EXPECT_EQ(panel.selected(), MenuEntry::kBoss);
  panel.MoveCursor(-1);
  EXPECT_EQ(panel.selected(), MenuEntry::kSettings);
}

// The cursor is a row rather than an entry, so an arrival to its left slides
// it onto the new one -- which is the gold one, and the reason the corner is
// worth a look that minute.
TEST(MenuPanelTest, AnArrivingEntrySlidesTheCursorOntoIt) {
  GameState state = EmptyState();
  BattleAnalysis analysis;
  int focus = kMenuPanel;
  MenuPanel panel(state, analysis, focus);
  EXPECT_EQ(panel.selected(), MenuEntry::kAnalysis);
  LevelTo(state, kBossLevel);
  EXPECT_EQ(panel.selected(), MenuEntry::kBoss);
}

// Boss is gold until the player has opened the screen behind it, the same way
// a tab handed over but never opened is.
TEST(MenuPanelTest, BossIsGoldUntilItHasBeenOpened) {
  GameState state = EmptyState();
  LevelTo(state, kBossLevel);
  BattleAnalysis analysis;
  int focus = kCharPanel;  // unfocused, so nothing is inverted
  MenuPanel panel(state, analysis, focus);

  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(40),
                                               ftxui::Dimension::Fixed(3));
  ftxui::Render(screen, panel.Render());
  ftxui::Color gold = screen.PixelAt(2, 1).foreground_color;

  state.account.MarkSeen(MenuPanel::boss_seen_key());
  ftxui::Screen after = ftxui::Screen::Create(ftxui::Dimension::Fixed(40),
                                              ftxui::Dimension::Fixed(3));
  ftxui::Render(after, panel.Render());
  EXPECT_NE(gold, after.PixelAt(2, 1).foreground_color);
}

// The box takes the name of the entry it hangs from, and lists what that entry
// leads to.
TEST(MenuPanelTest, TheBoxIsTitledByTheEntryThatRaisedIt) {
  GameState state = EmptyState();
  BattleAnalysis analysis;
  int focus = kMenuPanel;
  MenuPanel panel(state, analysis, focus);
  EXPECT_FALSE(panel.box_open());

  OpenBoxOn(panel, MenuEntry::kSettings);
  EXPECT_TRUE(panel.box_open());
  EXPECT_EQ(panel.box_entry(), MenuEntry::kSettings);
  EXPECT_EQ(panel.box_cursor(), -1);
  std::string settings = RenderBox(panel);
  EXPECT_NE(settings.find("Settings"), std::string::npos);
  EXPECT_NE(settings.find("Keybinds"), std::string::npos);

  OpenBoxOn(panel, MenuEntry::kAnalysis);
  EXPECT_EQ(panel.box_entry(), MenuEntry::kAnalysis);
  std::string box = RenderBox(panel);
  EXPECT_NE(box.find("Analysis"), std::string::npos);
  EXPECT_NE(box.find("Start"), std::string::npos);
  EXPECT_NE(box.find("View"), std::string::npos);
}

// The entry reads Stop while the tool is measuring, so one row is both ways of
// working it.
TEST(MenuPanelTest, TheAnalysisEntryReadsStopWhileItRuns) {
  GameState state = EmptyState();
  BattleAnalysis analysis;
  int focus = kMenuPanel;
  MenuPanel panel(state, analysis, focus);
  OpenBoxOn(panel, MenuEntry::kAnalysis);
  ASSERT_NE(RenderBox(panel).find("Start"), std::string::npos);

  analysis.Start();
  std::string running = RenderBox(panel);
  EXPECT_NE(running.find("Stop"), std::string::npos);
  EXPECT_EQ(running.find("Start"), std::string::npos);

  // With a stop pending it reads Start again: one more press takes it back.
  analysis.Stop();
  EXPECT_NE(RenderBox(panel).find("Start"), std::string::npos);
}

// The box stands above the menu row, so Up walks into it and Down comes back
// out. The entry the box was opened from gives up the cursor while it holds it.
TEST(MenuPanelTest, TheBoxAndTheMenuRowShareOneCursor) {
  GameState state = EmptyState();
  BattleAnalysis analysis;
  int focus = kMenuPanel;
  MenuPanel panel(state, analysis, focus);
  OpenBoxOn(panel, MenuEntry::kSettings);
  // Up walks the box from the bottom, so the entry nearest the row is the one
  // the cursor meets first.
  panel.MoveBoxCursor(1);
  EXPECT_EQ(panel.box_cursor(), 1);
  EXPECT_EQ(panel.selected_settings_entry(), SettingsEntry::kOptions);
  panel.MoveBoxCursor(1);
  EXPECT_EQ(panel.box_cursor(), 0);
  EXPECT_EQ(panel.selected_settings_entry(), SettingsEntry::kKeybinds);
  panel.MoveBoxCursor(1);
  EXPECT_EQ(panel.box_cursor(), -1);

  panel.MoveBoxCursor(-1);
  EXPECT_EQ(panel.box_cursor(), 0);
  panel.CloseBox();
  EXPECT_FALSE(panel.box_open());
  EXPECT_EQ(panel.box_cursor(), -1);
}

// The Analysis box has two rows, so the ring the cursor walks is three stops
// long and Start is the bottom one.
TEST(MenuPanelTest, TheAnalysisBoxWalksBothOfItsRows) {
  GameState state = EmptyState();
  BattleAnalysis analysis;
  int focus = kMenuPanel;
  MenuPanel panel(state, analysis, focus);
  OpenBoxOn(panel, MenuEntry::kAnalysis);
  panel.MoveBoxCursor(1);
  EXPECT_EQ(panel.box_cursor(), 1);
  EXPECT_EQ(panel.selected_analysis_entry(), AnalysisEntry::kView);
  panel.MoveBoxCursor(1);
  EXPECT_EQ(panel.box_cursor(), 0);
  EXPECT_EQ(panel.selected_analysis_entry(), AnalysisEntry::kStartStop);
  panel.MoveBoxCursor(1);
  EXPECT_EQ(panel.box_cursor(), -1);
}

// The column `text` first appears in, drawn flush right the way the corner
// lays the menu and its box out, or -1.
int ColumnOf(ftxui::Element element, const std::string& text) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(60),
                                               ftxui::Dimension::Fixed(6));
  ftxui::Render(screen, ftxui::hbox({ftxui::filler(), std::move(element)}));
  return FindOnScreen(screen, text).x;
}

// The columns the open box's two top corners stand in.
int BoxColumn(const MenuPanel& panel) {
  return ColumnOf(panel.RenderBox(), "╭");
}

int BoxRightColumn(const MenuPanel& panel) {
  return ColumnOf(panel.RenderBox(), "╮");
}

// The column `word` starts at on the menu row.
int WordColumn(const MenuPanel& panel, const std::string& word) {
  return ColumnOf(panel.Render(), word);
}

// The box drops from the word that opened it rather than from the corner of
// the screen, and being wider than the word it hangs off both sides of it
// evenly.
TEST(MenuPanelTest, TheBoxIsCentredOnTheWordThatOpenedIt) {
  GameState state = EmptyState();
  LevelTo(state, kBossLevel);
  BattleAnalysis analysis;
  int focus = kMenuPanel;
  MenuPanel panel(state, analysis, focus);

  OpenBoxOn(panel, MenuEntry::kAnalysis);
  int word = WordColumn(panel, "Analysis");
  ASSERT_GE(word, 0);
  int word_end = word + static_cast<int>(std::string("Analysis").size()) - 1;
  int box = BoxColumn(panel);
  int box_end = BoxRightColumn(panel);
  ASSERT_GT(box_end, box);
  EXPECT_LT(box, word);
  EXPECT_GT(box_end, word_end);
  // Same centre, to the column: the two overhangs match.
  EXPECT_EQ(box + box_end, word + word_end);
}

// A box on the last entry would hang off the right of the screen, so it is
// pulled back to the edge rather than being cut in half.
TEST(MenuPanelTest, TheLastEntrysBoxStopsAtTheEdge) {
  GameState state = EmptyState();
  LevelTo(state, kBossLevel);
  BattleAnalysis analysis;
  int focus = kMenuPanel;
  MenuPanel panel(state, analysis, focus);

  OpenBoxOn(panel, MenuEntry::kSettings);
  EXPECT_EQ(panel.BoxRightMargin(), 0);
  int word = WordColumn(panel, "Settings");
  ASSERT_GE(word, 0);
  // Still over the word, only pushed left of centre by the edge.
  EXPECT_LT(BoxColumn(panel), word);
  EXPECT_GT(BoxColumn(panel), 0);
}

}  // namespace
}  // namespace ms
