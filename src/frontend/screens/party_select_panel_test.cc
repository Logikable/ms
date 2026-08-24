#include "src/frontend/screens/party_select_panel.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <utility>

#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/types.h"
#include "src/multiplayer/client.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {
namespace {

PlayerInfo Player(const std::string& account_id, const std::string& name,
                  int level, JobAdvancement job) {
  PlayerInfo player;
  player.set_account_id(account_id);
  player.set_name(name);
  player.set_level(level);
  player.set_job(job);
  return player;
}

void AddMember(Party* party, const PlayerInfo& player, bool ready = false) {
  PartyMember* member = party->add_members();
  *member->mutable_player() = player;
  member->set_ready(ready);
}

// A party of `count` led by "me", named Ariel, Bree and Cyd.
Party PartyOf(int count) {
  Party party;
  party.set_id("p1");
  party.set_leader_account_id("me");
  const char* accounts[] = {"me", "two", "three"};
  const char* names[] = {"Ariel", "Bree", "Cyd"};
  for (int i = 0; i < count; ++i) {
    AddMember(&party, Player(accounts[i], names[i], 140 - i,
                             JOB_ADVANCEMENT_DARK_KNIGHT));
  }
  return party;
}

// A snapshot of a connected client playing under "me".
MultiplayerSnapshot Connected() {
  MultiplayerSnapshot snapshot;
  snapshot.state = ConnectionState::kConnected;
  snapshot.account_id = "me";
  return snapshot;
}

std::string Render(const PartySelectPanel& panel) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(100),
                                               ftxui::Dimension::Fixed(20));
  // Centred, as the Tui draws it: a window fills its box, and center is what
  // holds it to its content.
  ftxui::Element element = ftxui::center(panel.Render());
  ftxui::Render(screen, element);
  std::string out;
  for (int y = 0; y < screen.dimy(); ++y) {
    for (int x = 0; x < screen.dimx(); ++x) {
      const std::string& cell = screen.PixelAt(x, y).character;
      out += cell.empty() ? " " : cell;
    }
    out += '\n';
  }
  return out;
}

// The box of everything the panel drew, as width by height.
std::pair<int, int> BoxOf(const PartySelectPanel& panel) {
  std::string screen = Render(panel);
  int width = 0;
  int height = 0;
  std::stringstream lines(screen);
  std::string line;
  for (int y = 0; std::getline(lines, line); ++y) {
    std::size_t last = line.find_last_not_of(' ');
    if (last == std::string::npos) {
      continue;
    }
    width = std::max(width, static_cast<int>(last) + 1);
    height = y + 1;
  }
  return {width, height};
}

class PartySelectPanelTest : public ::testing::Test {
 protected:
  // Puts `snapshot` in front of the panel and starts the cursor at the top.
  void Show(const MultiplayerSnapshot& snapshot) {
    panel_.SetSnapshot(snapshot);
    panel_.Reset();
  }

  PartySelectPanel panel_;
};

TEST_F(PartySelectPanelTest, ListsTheOpenParties) {
  MultiplayerSnapshot snapshot = Connected();
  *snapshot.parties.add_parties() = PartyOf(2);
  Show(snapshot);

  std::string screen = Render(panel_);
  EXPECT_NE(screen.find("Leader"), std::string::npos);
  EXPECT_NE(screen.find("Capacity"), std::string::npos);
  EXPECT_NE(screen.find("Ariel"), std::string::npos);
  EXPECT_NE(screen.find("2/3"), std::string::npos);
  EXPECT_NE(screen.find("[Create a Party]"), std::string::npos);
  EXPECT_FALSE(panel_.in_party());
}

TEST_F(PartySelectPanelTest, HidesAFullParty) {
  MultiplayerSnapshot snapshot = Connected();
  *snapshot.parties.add_parties() = PartyOf(3);
  Show(snapshot);

  // Nothing to join, so the list says so and the cursor has only the buttons.
  EXPECT_NE(Render(panel_).find("(empty)"), std::string::npos);
  EXPECT_EQ(panel_.Chosen(), PartyAction::kCreate);
  EXPECT_TRUE(panel_.selected_party_id().empty());
}

TEST_F(PartySelectPanelTest, JoinsThePartyUnderTheCursor) {
  MultiplayerSnapshot snapshot = Connected();
  *snapshot.parties.add_parties() = PartyOf(1);
  snapshot.parties.add_parties()->set_id("p2");
  snapshot.parties.mutable_parties(1)->set_leader_account_id("other");
  AddMember(snapshot.parties.mutable_parties(1),
            Player("other", "Bree", 120, JOB_ADVANCEMENT_BISHOP));
  Show(snapshot);

  EXPECT_EQ(panel_.Chosen(), PartyAction::kJoin);
  EXPECT_EQ(panel_.selected_party_id(), "p1");
  panel_.MoveCursor(1);
  EXPECT_EQ(panel_.selected_party_id(), "p2");
}

TEST_F(PartySelectPanelTest, TheListAndTheButtonsAreOneRing) {
  MultiplayerSnapshot snapshot = Connected();
  *snapshot.parties.add_parties() = PartyOf(1);
  Show(snapshot);

  // One party, then the buttons, then round to the party again.
  EXPECT_EQ(panel_.Chosen(), PartyAction::kJoin);
  panel_.MoveCursor(1);
  EXPECT_EQ(panel_.Chosen(), PartyAction::kCreate);
  panel_.MoveCursor(1);
  EXPECT_EQ(panel_.Chosen(), PartyAction::kJoin);
  // And back the other way.
  panel_.MoveCursor(-1);
  EXPECT_EQ(panel_.Chosen(), PartyAction::kCreate);
}

TEST_F(PartySelectPanelTest, LeftAndRightBelongToTheButtons) {
  MultiplayerSnapshot snapshot = Connected();
  *snapshot.parties.add_parties() = PartyOf(1);
  Show(snapshot);

  // Nothing to move between while the cursor is still in the list.
  panel_.MoveButton(1);
  EXPECT_EQ(panel_.Chosen(), PartyAction::kJoin);

  panel_.MoveCursor(1);
  panel_.MoveButton(1);
  EXPECT_EQ(panel_.Chosen(), PartyAction::kClose);
  // Clamped at the ends rather than wrapping: a button row has two sides.
  panel_.MoveButton(1);
  EXPECT_EQ(panel_.Chosen(), PartyAction::kClose);
  panel_.MoveButton(-5);
  EXPECT_EQ(panel_.Chosen(), PartyAction::kCreate);
}

TEST_F(PartySelectPanelTest, ShowsWhoIsInTheParty) {
  MultiplayerSnapshot snapshot = Connected();
  snapshot.party = PartyOf(2);
  snapshot.party.mutable_members(1)->set_ready(true);
  Show(snapshot);

  std::string screen = Render(panel_);
  EXPECT_NE(screen.find("Name"), std::string::npos);
  EXPECT_NE(screen.find("Level"), std::string::npos);
  EXPECT_NE(screen.find("Ready"), std::string::npos);
  EXPECT_NE(screen.find("Ariel"), std::string::npos);
  EXPECT_NE(screen.find("Dark Knight"), std::string::npos);
  EXPECT_NE(screen.find("139"), std::string::npos);
  // A crown on the leader, and a mark on both: the leader by leading, the
  // other by saying so.
  EXPECT_NE(screen.find("♛ Ariel"), std::string::npos);
  EXPECT_EQ(screen.find("♛ Bree"), std::string::npos);
  EXPECT_NE(screen.find("✓"), std::string::npos);
  EXPECT_TRUE(panel_.in_party());
  EXPECT_TRUE(panel_.is_leader());
}

TEST_F(PartySelectPanelTest, TheLeaderHasNoReadyButton) {
  MultiplayerSnapshot snapshot = Connected();
  snapshot.party = PartyOf(2);
  Show(snapshot);

  std::string screen = Render(panel_);
  EXPECT_EQ(screen.find("[Ready]"), std::string::npos);
  EXPECT_NE(screen.find("[Leave Party]"), std::string::npos);
  // Leading is what says they are ready.
  EXPECT_TRUE(panel_.ready());
}

TEST_F(PartySelectPanelTest, AMemberReadiesAndUnreadies) {
  MultiplayerSnapshot snapshot = Connected();
  snapshot.party = PartyOf(2);
  snapshot.party.set_leader_account_id("two");
  Show(snapshot);
  ASSERT_FALSE(panel_.is_leader());

  panel_.MoveCursor(-1);
  EXPECT_EQ(panel_.Chosen(), PartyAction::kReady);
  EXPECT_NE(Render(panel_).find("[Ready]"), std::string::npos);

  snapshot.party.mutable_members(0)->set_ready(true);
  panel_.SetSnapshot(snapshot);
  EXPECT_TRUE(panel_.ready());
  EXPECT_EQ(panel_.Chosen(), PartyAction::kUnready);
  EXPECT_NE(Render(panel_).find("[Unready]"), std::string::npos);
}

TEST_F(PartySelectPanelTest, OnlyTheLeaderActsOnAMember) {
  MultiplayerSnapshot snapshot = Connected();
  snapshot.party = PartyOf(2);
  snapshot.party.set_leader_account_id("two");
  Show(snapshot);

  // A member may move the cursor down the list; Enter does nothing there.
  EXPECT_EQ(panel_.Chosen(), PartyAction::kNone);
  EXPECT_EQ(panel_.selected_member(), "me");
  panel_.MoveCursor(1);
  EXPECT_EQ(panel_.selected_member(), "two");
  EXPECT_EQ(panel_.Chosen(), PartyAction::kNone);
}

TEST_F(PartySelectPanelTest, TheLeaderRaisesAMenuOnAMember) {
  MultiplayerSnapshot snapshot = Connected();
  snapshot.party = PartyOf(2);
  Show(snapshot);
  panel_.MoveCursor(1);

  ASSERT_EQ(panel_.Chosen(), PartyAction::kMemberMenu);
  EXPECT_EQ(panel_.selected_member(), "two");
  EXPECT_EQ(panel_.selected_member_name(), "Bree");

  panel_.OpenMenu();
  EXPECT_TRUE(panel_.menu_open());
  EXPECT_EQ(panel_.menu_selected(), kPartyMenuKick);
  std::string screen = Render(panel_);
  EXPECT_NE(screen.find("Kick"), std::string::npos);
  EXPECT_NE(screen.find("Promote"), std::string::npos);

  panel_.MoveMenuCursor(1);
  EXPECT_EQ(panel_.menu_selected(), kPartyMenuPromote);
  panel_.CloseMenu();
  EXPECT_FALSE(panel_.menu_open());
}

TEST_F(PartySelectPanelTest, TheLeadersOwnRowOffersNeither) {
  MultiplayerSnapshot snapshot = Connected();
  snapshot.party = PartyOf(2);
  Show(snapshot);

  ASSERT_EQ(panel_.selected_member(), "me");
  panel_.OpenMenu();
  // Both dimmed, so the cursor skips past them onto Close and stays there.
  EXPECT_EQ(panel_.menu_selected(), kPartyMenuClose);
  panel_.MoveMenuCursor(1);
  EXPECT_EQ(panel_.menu_selected(), kPartyMenuClose);
}

TEST_F(PartySelectPanelTest, TheWindowIsOneSizeInEveryState) {
  MultiplayerSnapshot snapshot = Connected();
  Show(snapshot);
  std::pair<int, int> empty = BoxOf(panel_);

  // More parties than the list can show, so the frame scrolls rather than the
  // window growing.
  for (int i = 0; i < 12; ++i) {
    snapshot.parties.add_parties()->set_id("p" + std::to_string(i));
  }
  Show(snapshot);
  EXPECT_EQ(BoxOf(panel_), empty);
  EXPECT_NE(Render(panel_).find("Party List"), std::string::npos);

  snapshot.party = PartyOf(3);
  Show(snapshot);
  EXPECT_EQ(BoxOf(panel_), empty);
  EXPECT_EQ(Render(panel_).find("Party List"), std::string::npos);
}

TEST_F(PartySelectPanelTest, TheListShrinkingUnderTheCursorMovesIt) {
  MultiplayerSnapshot snapshot = Connected();
  snapshot.party = PartyOf(3);
  Show(snapshot);
  // Down past all three members onto the buttons.
  for (int i = 0; i < 3; ++i) {
    panel_.MoveCursor(1);
  }
  ASSERT_EQ(panel_.Chosen(), PartyAction::kLeave);

  // Two of them leave while the cursor is down there. Up has to reach the one
  // member left rather than being swallowed by a cursor past the end.
  snapshot.party = PartyOf(1);
  panel_.SetSnapshot(snapshot);
  EXPECT_EQ(panel_.Chosen(), PartyAction::kLeave);
  panel_.MoveCursor(-1);
  EXPECT_EQ(panel_.Chosen(), PartyAction::kMemberMenu);
  EXPECT_EQ(panel_.selected_member(), "me");
}

}  // namespace
}  // namespace ms
