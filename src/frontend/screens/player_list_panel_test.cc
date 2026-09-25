#include "src/frontend/screens/player_list_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/testing/screen_text.h"
#include "src/frontend/types.h"
#include "src/multiplayer/client.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {
namespace {

// A connected client playing as "me", with `count` players online: Ariel, Bree
// and Cyd, in that order.
MultiplayerSnapshot Online(int count) {
  MultiplayerSnapshot snapshot;
  snapshot.state = ConnectionState::kConnected;
  snapshot.account_id = "me";
  const char* accounts[] = {"me", "two", "three"};
  const char* names[] = {"Ariel", "Bree", "Cyd"};
  for (int i = 0; i < count; ++i) {
    PlayerInfo* player = snapshot.online.add_players();
    player->set_account_id(accounts[i]);
    player->set_name(names[i]);
    player->set_level(200 - i);
  }
  return snapshot;
}

std::string Render(const PlayerListPanel& panel) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(100),
                                               ftxui::Dimension::Fixed(20));
  ftxui::Element element = ftxui::center(panel.Render());
  ftxui::Render(screen, element);
  return ScreenText(screen);
}

class PlayerListPanelTest : public ::testing::Test {
 protected:
  void Show(const MultiplayerSnapshot& snapshot) {
    panel_.SetSnapshot(snapshot);
    panel_.Reset();
  }

  PlayerListPanel panel_;
};

TEST_F(PlayerListPanelTest, RaisesAMenuOnAPlayer) {
  Show(Online(3));
  panel_.MoveCursor(1);
  ASSERT_EQ(panel_.selected_name(), "Bree");

  panel_.OpenMenu();
  EXPECT_TRUE(panel_.menu_open());
  EXPECT_EQ(panel_.menu_selected(), kPlayerMenuInspect);
  std::string screen = Render(panel_);
  EXPECT_NE(screen.find("Inspect"), std::string::npos);
  EXPECT_NE(screen.find("Trade"), std::string::npos);

  // Three entries, so Down reaches Close and Down again wraps back.
  panel_.MoveMenuCursor(1);
  EXPECT_EQ(panel_.menu_selected(), kPlayerMenuTrade);
  panel_.MoveMenuCursor(1);
  EXPECT_EQ(panel_.menu_selected(), kPlayerMenuClose);
  panel_.MoveMenuCursor(1);
  EXPECT_EQ(panel_.menu_selected(), kPlayerMenuInspect);

  panel_.CloseMenu();
  EXPECT_FALSE(panel_.menu_open());
  EXPECT_EQ(Render(panel_).find("Inspect"), std::string::npos);
}

TEST_F(PlayerListPanelTest, YourOwnRowDoesNotOfferTrade) {
  Show(Online(3));
  ASSERT_EQ(panel_.selected_account(), "me");

  panel_.OpenMenu();
  std::string screen = Render(panel_);
  // Inspecting yourself is fine, but trading with yourself isn't possible, so
  // that entry isn't there at all.
  EXPECT_NE(screen.find("Inspect"), std::string::npos);
  EXPECT_EQ(screen.find("Trade"), std::string::npos);
  panel_.MoveMenuCursor(1);
  EXPECT_EQ(panel_.menu_selected(), kPlayerMenuClose);
}

TEST_F(PlayerListPanelTest, ListsEveryoneOnline) {
  Show(Online(3));

  std::string screen = Render(panel_);
  EXPECT_NE(screen.find("Online Players"), std::string::npos);
  EXPECT_NE(screen.find("Name"), std::string::npos);
  EXPECT_NE(screen.find("Level"), std::string::npos);
  EXPECT_NE(screen.find("Ariel"), std::string::npos);
  EXPECT_NE(screen.find("200"), std::string::npos);
  EXPECT_NE(screen.find("Cyd"), std::string::npos);
  EXPECT_NE(screen.find("[Close]"), std::string::npos);
  // The reader is in their own list.
  EXPECT_EQ(panel_.selected_account(), "me");
}

TEST_F(PlayerListPanelTest, SaysSoWhenNobodyIsOnline) {
  Show(Online(0));

  EXPECT_NE(Render(panel_).find("empty"), std::string::npos);
  // Close is the only stop left, so the cursor is there.
  EXPECT_TRUE(panel_.on_close());
  EXPECT_EQ(panel_.selected_account(), "");
  EXPECT_EQ(panel_.selected_name(), "");
}

TEST_F(PlayerListPanelTest, WalksThePlayersAndThenClose) {
  Show(Online(3));

  EXPECT_EQ(panel_.selected_name(), "Ariel");
  panel_.MoveCursor(1);
  EXPECT_EQ(panel_.selected_name(), "Bree");
  panel_.MoveCursor(2);
  EXPECT_TRUE(panel_.on_close());
  // Close is the last stop in the ring, so Down again wraps to the top.
  panel_.MoveCursor(1);
  EXPECT_EQ(panel_.selected_account(), "me");
  // Up from the top lands on Close.
  panel_.MoveCursor(-1);
  EXPECT_TRUE(panel_.on_close());
}

TEST_F(PlayerListPanelTest, HoldsTheCursorWhenThePlayerItWasOnLeaves) {
  Show(Online(3));
  panel_.MoveCursor(2);
  EXPECT_EQ(panel_.selected_name(), "Cyd");

  // Two players leave while the cursor is on the last one.
  panel_.SetSnapshot(Online(1));
  EXPECT_TRUE(panel_.on_close());
  // The keypress still moves the cursor; a cursor past the end would waste it
  // on getting back into range.
  panel_.MoveCursor(1);
  EXPECT_EQ(panel_.selected_name(), "Ariel");
}

// The list is as wide as the longest name online, and the menu opens over it.
TEST_F(PlayerListPanelTest, TheListKeepsOffTheRightBorder) {
  Show(Online(3));
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel_.Render()).empty());
  panel_.MoveCursor(1);
  panel_.OpenMenu();
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel_.Render()).empty());
}
}  // namespace
}  // namespace ms
