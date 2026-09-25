#include "src/frontend/panels/menu_panel.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/combat/battle_analysis.h"
#include "src/frontend/testing/screen_text.h"
#include "src/frontend/types.h"
#include "src/game_state.h"

namespace ms {
namespace {

// The levels the gated entries appear at. Written out rather than read from the
// progression table, so moving a gate is something the test notices.
constexpr int kMultiplayerLevel = 10;
constexpr int kBossLevel = 110;
constexpr int kDailiesLevel = 200;
constexpr int kCharactersLevel = 210;

GameState EmptyState() {
  return GameState({}, {}, {}, {}, {});
}

void LevelTo(GameState& state, int level) {
  while (state.character.proto().level() < level) {
    state.character.LevelUp();
  }
}

std::string Render(const MenuPanel& panel) {
  // Wide enough for the whole row. A narrower screen would cut off its end.
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(70),
                                               ftxui::Dimension::Fixed(3));
  ftxui::Render(screen, panel.Render());
  return screen.ToString();
}

// The box as plain characters. The tests check its text and where its border
// lands, and ToString puts a style escape between the two.
std::string RenderBox(const MenuPanel& panel) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(20),
                                               ftxui::Dimension::Fixed(5));
  ftxui::Render(screen, panel.RenderBox());
  return ScreenText(screen);
}

// Puts the cursor on `entry` and opens its box, as pressing Enter on it does.
void OpenBoxOn(MenuPanel& panel, MenuEntry entry) {
  while (panel.selected() != entry) {
    panel.MoveCursor(1);
  }
  panel.OpenBox(entry);
}

// Analysis is at the left end from the start, and everything else appears
// between it and Settings as its gate opens.
TEST(MenuPanelTest, EntriesArriveBetweenAnalysisAndSettings) {
  GameState state = EmptyState();
  BattleAnalysis analysis;
  int focus = kMenuPanel;
  MenuPanel panel(state, analysis, focus);
  std::string early = Render(panel);
  EXPECT_NE(early.find("Analysis"), std::string::npos);
  EXPECT_NE(early.find("Settings"), std::string::npos);
  EXPECT_EQ(early.find("Boss"), std::string::npos);
  EXPECT_EQ(early.find("Dailies"), std::string::npos);

  LevelTo(state, kBossLevel);
  std::string later = Render(panel);
  EXPECT_LT(later.find("Analysis"), later.find("Boss"));
  EXPECT_LT(later.find("Boss"), later.find("Settings"));
  EXPECT_EQ(later.find("Dailies"), std::string::npos);

  // The dailies are for the symbols, so the entry waits for them and appears
  // left of Boss.
  LevelTo(state, kDailiesLevel);
  std::string last = Render(panel);
  EXPECT_LT(last.find("Analysis"), last.find("Dailies"));
  EXPECT_LT(last.find("Dailies"), last.find("Boss"));
}

// The two entries not about this character's progress: the lobby, which opens
// with skills, and character select, which opens last.
TEST(MenuPanelTest, MultiplayerOpensLongBeforeCharacters) {
  GameState state = EmptyState();
  BattleAnalysis analysis;
  int focus = kMenuPanel;
  MenuPanel panel(state, analysis, focus);
  LevelTo(state, kMultiplayerLevel);
  std::string lobby = Render(panel);
  EXPECT_NE(lobby.find("Multiplayer"), std::string::npos)
      << "the lobby does not wait for bossing";
  EXPECT_EQ(lobby.find("Boss"), std::string::npos);
  EXPECT_EQ(lobby.find("Characters"), std::string::npos);

  LevelTo(state, kCharactersLevel);
  std::string all = Render(panel);
  EXPECT_LT(all.find("Multiplayer"), all.find("Characters"));
  EXPECT_LT(all.find("Characters"), all.find("Settings"));
}

// The row holds only the entries: no brackets, two columns between them, and a
// blank column inside each border.
TEST(MenuPanelTest, TheEntriesSitTwoColumnsApart) {
  GameState state = EmptyState();
  LevelTo(state, kDailiesLevel);
  BattleAnalysis analysis;
  int focus = kMenuPanel;
  MenuPanel panel(state, analysis, focus);
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(56),
                                               ftxui::Dimension::Fixed(3));
  ftxui::Render(screen, ftxui::hbox({panel.Render(), ftxui::filler()}));
  EXPECT_NE(ScreenRow(screen, 1).find(
                "│ Analysis  Dailies  Boss  Multiplayer  Settings │"),
            std::string::npos);
}

TEST(MenuPanelTest, TheCursorWrapsAndPicksAnEntry) {
  GameState state = EmptyState();
  LevelTo(state, kBossLevel);
  BattleAnalysis analysis;
  int focus = kMenuPanel;
  MenuPanel panel(state, analysis, focus);
  EXPECT_EQ(panel.selected(), MenuEntry::kAnalysis);
  panel.MoveCursor(1);
  EXPECT_EQ(panel.selected(), MenuEntry::kBoss);
  panel.MoveCursor(1);
  EXPECT_EQ(panel.selected(), MenuEntry::kMultiplayer);
  panel.MoveCursor(1);
  EXPECT_EQ(panel.selected(), MenuEntry::kSettings);
  // Off the end and back to the start.
  panel.MoveCursor(1);
  EXPECT_EQ(panel.selected(), MenuEntry::kAnalysis);
  panel.MoveCursor(-1);
  EXPECT_EQ(panel.selected(), MenuEntry::kSettings);
}

// The cursor is an index, and new entries appear to the right of Analysis, so
// the cursor stays on the entry the player left it on.
TEST(MenuPanelTest, AnArrivingEntryLeavesTheCursorWhereItWas) {
  GameState state = EmptyState();
  BattleAnalysis analysis;
  int focus = kMenuPanel;
  MenuPanel panel(state, analysis, focus);
  EXPECT_EQ(panel.selected(), MenuEntry::kAnalysis);
  LevelTo(state, kBossLevel);
  EXPECT_EQ(panel.selected(), MenuEntry::kAnalysis);
}

// Boss is gold until the player has opened its screen, like a new tab that
// hasn't been opened.
TEST(MenuPanelTest, BossIsGoldUntilItHasBeenOpened) {
  GameState state = EmptyState();
  LevelTo(state, kBossLevel);
  BattleAnalysis analysis;
  int focus = kCharPanel;  // unfocused, so nothing is inverted
  MenuPanel panel(state, analysis, focus);

  // After the border, the blank column, Analysis and the gap after it.
  constexpr int kBossColumn = 12;
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(40),
                                               ftxui::Dimension::Fixed(3));
  ftxui::Render(screen, panel.Render());
  ASSERT_EQ(screen.PixelAt(kBossColumn, 1).character, "B");
  ftxui::Color gold = screen.PixelAt(kBossColumn, 1).foreground_color;

  state.account.MarkSeen(MenuPanel::boss_seen_key());
  ftxui::Screen after = ftxui::Screen::Create(ftxui::Dimension::Fixed(40),
                                              ftxui::Dimension::Fixed(3));
  ftxui::Render(after, panel.Render());
  EXPECT_NE(gold, after.PixelAt(kBossColumn, 1).foreground_color);
}

// The box is titled with the entry that opened it and lists where it leads.
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
  EXPECT_NE(settings.find("Jukebox"), std::string::npos);
  EXPECT_NE(settings.find("Keybinds"), std::string::npos);

  OpenBoxOn(panel, MenuEntry::kAnalysis);
  EXPECT_EQ(panel.box_entry(), MenuEntry::kAnalysis);
  std::string box = RenderBox(panel);
  EXPECT_NE(box.find("Analysis"), std::string::npos);
  EXPECT_NE(box.find("Start"), std::string::npos);
  EXPECT_NE(box.find("View"), std::string::npos);
}

// The box marks its cursor with a caret, as every other dropdown does, and
// leaves room for it. A box measured from the label alone loses its right
// border, which the panel is laid out from.
TEST(MenuPanelTest, TheBoxCaretsItsCursorAndIsWideEnoughForIt) {
  GameState state = EmptyState();
  BattleAnalysis analysis;
  int focus = kMenuPanel;
  MenuPanel panel(state, analysis, focus);
  OpenBoxOn(panel, MenuEntry::kSettings);
  EXPECT_EQ(RenderBox(panel).find("> "), std::string::npos)
      << "the cursor is still out on the entry the box came from";

  panel.MoveBoxCursor(1);
  while (panel.box_cursor() != 0) {
    panel.MoveBoxCursor(1);
  }
  std::string box = RenderBox(panel);
  // The border after the row, which a box measured a column short would lose:
  // the caret pushes the widest row out and the panel is laid out from that
  // edge. Keybinds is the widest entry, so every row is padded to it.
  EXPECT_NE(box.find("│> Jukebox  │"), std::string::npos);
  EXPECT_NE(box.find("│  Keybinds │"), std::string::npos);
  EXPECT_NE(box.find("│  Options  │"), std::string::npos);
}

// The entry reads Stop while the tool is measuring, so one row both starts and
// stops it.
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

  // With a stop pending it reads Start again, so one more press undoes it.
  analysis.Stop();
  EXPECT_NE(RenderBox(panel).find("Start"), std::string::npos);
}

// The box sits above the menu row, so Up moves into it and Down comes back out.
// The entry the box opened from loses the highlight while the box has the
// cursor.
TEST(MenuPanelTest, TheBoxAndTheMenuRowShareOneCursor) {
  GameState state = EmptyState();
  BattleAnalysis analysis;
  int focus = kMenuPanel;
  MenuPanel panel(state, analysis, focus);
  OpenBoxOn(panel, MenuEntry::kSettings);
  // Up enters the box from the bottom, so the entry nearest the row comes
  // first.
  panel.MoveBoxCursor(1);
  EXPECT_EQ(panel.box_cursor(), 2);
  EXPECT_EQ(panel.selected_settings_entry(), SettingsEntry::kOptions);
  panel.MoveBoxCursor(1);
  EXPECT_EQ(panel.box_cursor(), 1);
  EXPECT_EQ(panel.selected_settings_entry(), SettingsEntry::kKeybinds);
  panel.MoveBoxCursor(1);
  EXPECT_EQ(panel.box_cursor(), 0);
  EXPECT_EQ(panel.selected_settings_entry(), SettingsEntry::kJukebox);
  panel.MoveBoxCursor(1);
  EXPECT_EQ(panel.box_cursor(), -1);

  // Down from the row wraps to the top of the box.
  panel.MoveBoxCursor(-1);
  EXPECT_EQ(panel.box_cursor(), 0);
  panel.CloseBox();
  EXPECT_FALSE(panel.box_open());
  EXPECT_EQ(panel.box_cursor(), -1);
}

// The Multiplayer box lists Players above Party, so Up from the row reaches
// Party first.
TEST(MenuPanelTest, TheMultiplayerBoxWalksBothOfItsRows) {
  GameState state = EmptyState();
  LevelTo(state, kBossLevel);
  BattleAnalysis analysis;
  int focus = kMenuPanel;
  MenuPanel panel(state, analysis, focus);
  OpenBoxOn(panel, MenuEntry::kMultiplayer);
  panel.MoveBoxCursor(1);
  EXPECT_EQ(panel.box_cursor(), 1);
  EXPECT_EQ(panel.selected_multiplayer_entry(), MultiplayerEntry::kParty);
  panel.MoveBoxCursor(1);
  EXPECT_EQ(panel.box_cursor(), 0);
  EXPECT_EQ(panel.selected_multiplayer_entry(), MultiplayerEntry::kPlayers);
  panel.MoveBoxCursor(1);
  EXPECT_EQ(panel.box_cursor(), -1);
}

// The Analysis box has two rows, so the cursor's ring has three stops and Start
// is the bottom one.
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

// The first column `text` appears in when drawn flush right, as the corner lays
// out the menu and its box, or -1.
int ColumnOf(ftxui::Element element, const std::string& text) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(60),
                                               ftxui::Dimension::Fixed(6));
  ftxui::Render(screen, ftxui::hbox({ftxui::filler(), std::move(element)}));
  return FindOnScreen(screen, text).x;
}

// The columns of the open box's top-left and top-right corners.
int BoxColumn(const MenuPanel& panel) {
  return ColumnOf(panel.RenderBox(), "╭");
}

int BoxRightColumn(const MenuPanel& panel) {
  return ColumnOf(panel.RenderBox(), "╮");
}

// The column where `word` starts on the menu row.
int WordColumn(const MenuPanel& panel, const std::string& word) {
  return ColumnOf(panel.Render(), word);
}

// The box hangs from the word that opened it, not from the screen corner, and
// since it is wider than the word it overhangs both sides evenly.
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
  // Same centre to the column: the two overhangs match.
  EXPECT_EQ(box + box_end, word + word_end);
}

// The last entry's box would hang off the right of the screen, so it is pulled
// back to the edge instead of being cut.
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
  // Still over the word, just pushed left of centre by the edge.
  EXPECT_LT(BoxColumn(panel), word);
  EXPECT_GT(BoxColumn(panel), 0);

  // BoxWidth must match the drawn box. The margin is computed from it, so a box
  // measured a column narrow would be placed a column too far right and lose
  // its border off the panel's edge.
  EXPECT_EQ(BoxRightColumn(panel) - BoxColumn(panel) + 1, panel.BoxWidth());
}

// The list and the box are measured separately, so both are checked for the
// margin.
TEST(MenuPanelTest, NeitherTheListNorABoxWeldsARowToItsBorder) {
  GameState state = EmptyState();
  LevelTo(state, kDailiesLevel);
  BattleAnalysis analysis;
  int focus = kMenuPanel;
  MenuPanel panel(state, analysis, focus);
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel.Render()).empty());
  OpenBoxOn(panel, MenuEntry::kBoss);
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel.RenderBox()).empty());
}
}  // namespace
}  // namespace ms
