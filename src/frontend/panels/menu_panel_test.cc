#include "src/frontend/panels/menu_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/types.h"
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

TEST(MenuPanelTest, SettingsHoldsTheCornerUntilBossArrives) {
  GameState state = EmptyState();
  int focus = kMenuPanel;
  MenuPanel panel(state, focus);
  std::string early = Render(panel);
  EXPECT_NE(early.find("Settings"), std::string::npos);
  EXPECT_EQ(early.find("Boss"), std::string::npos);
  EXPECT_EQ(panel.selected(), MenuEntry::kSettings);

  LevelTo(state, kBossLevel);
  std::string later = Render(panel);
  EXPECT_NE(later.find("Boss"), std::string::npos);
  // Boss sits to the left of Settings.
  EXPECT_LT(later.find("Boss"), later.find("Settings"));
}

// The row is the entries and nothing else: no brackets, two columns between
// them, and a column of clearance inside each border.
TEST(MenuPanelTest, TheEntriesSitTwoColumnsApart) {
  GameState state = EmptyState();
  LevelTo(state, kBossLevel);
  int focus = kMenuPanel;
  MenuPanel panel(state, focus);
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(40),
                                               ftxui::Dimension::Fixed(3));
  ftxui::Render(screen, ftxui::hbox({panel.Render(), ftxui::filler()}));
  std::string row;
  for (int x = 0; x < screen.dimx(); ++x) {
    const std::string& cell = screen.PixelAt(x, 1).character;
    row += cell.empty() ? " " : cell;
  }
  EXPECT_NE(row.find("│ Boss  Settings │"), std::string::npos);
}

TEST(MenuPanelTest, TheCursorWrapsAndPicksAnEntry) {
  GameState state = EmptyState();
  LevelTo(state, kBossLevel);
  int focus = kMenuPanel;
  MenuPanel panel(state, focus);
  EXPECT_EQ(panel.selected(), MenuEntry::kBoss);
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
  int focus = kMenuPanel;
  MenuPanel panel(state, focus);
  EXPECT_EQ(panel.selected(), MenuEntry::kSettings);
  LevelTo(state, kBossLevel);
  EXPECT_EQ(panel.selected(), MenuEntry::kBoss);
}

// Boss is gold until the player has opened the screen behind it, the same way
// a tab handed over but never opened is.
TEST(MenuPanelTest, BossIsGoldUntilItHasBeenOpened) {
  GameState state = EmptyState();
  LevelTo(state, kBossLevel);
  int focus = kCharPanel;  // unfocused, so nothing is inverted
  MenuPanel panel(state, focus);

  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(40),
                                               ftxui::Dimension::Fixed(3));
  ftxui::Render(screen, panel.Render());
  ftxui::Color gold = screen.PixelAt(2, 1).foreground_color;

  state.character.MarkTabSeen(MenuPanel::boss_seen_key());
  ftxui::Screen after = ftxui::Screen::Create(ftxui::Dimension::Fixed(40),
                                              ftxui::Dimension::Fixed(3));
  ftxui::Render(after, panel.Render());
  EXPECT_NE(gold, after.PixelAt(2, 1).foreground_color);
}

}  // namespace
}  // namespace ms
