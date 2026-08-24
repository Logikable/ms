#include "src/frontend/screens/party_inspect_panel.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/frontend/screens/all_stats_panel.h"
#include "src/frontend/widgets/panel_test_base.h"
#include "src/frontend/widgets/stat_rows.h"
#include "src/item/equip_instance.h"
#include "src/protos/equip.pb.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {
namespace {

EquipPrototype Sword() {
  EquipPrototype sword;
  sword.set_name("Iron Sword");
  sword.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  sword.set_equip_type(EQUIP_TYPE_ONE_HANDED_SWORD);
  sword.mutable_base_stats()->set_attack(30);
  sword.mutable_base_stats()->set_str(12);
  sword.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  return sword;
}

EquipPrototype Hat() {
  EquipPrototype hat;
  hat.set_name("Iron Hat");
  hat.set_equip_slot(EQUIP_SLOT_HAT);
  hat.mutable_base_stats()->set_def(20);
  hat.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  return hat;
}

// The columns row `y` runs between, or {-1, -1} for a blank one.
std::pair<int, int> RowSpan(const ftxui::Screen& screen, int y) {
  std::pair<int, int> span = {-1, -1};
  for (int x = 0; x < screen.dimx(); ++x) {
    const std::string& glyph = screen.PixelAt(x, y).character;
    if (glyph.empty() || glyph == " ") {
      continue;
    }
    if (span.first < 0) {
      span.first = x;
    }
    span.second = x;
  }
  return span;
}

// A piece of armour in `slot`, for filling out a list longer than the screen.
EquipPrototype Armour(const std::string& name, EquipSlot slot) {
  EquipPrototype piece;
  piece.set_name(name);
  piece.set_equip_slot(slot);
  piece.mutable_base_stats()->set_def(10);
  piece.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  return piece;
}

// More pieces than the item list draws at once.
std::vector<EquipPrototype> FullGear() {
  return {Sword(),
          Hat(),
          Armour("Iron Top", EQUIP_SLOT_TOP),
          Armour("Iron Bottom", EQUIP_SLOT_BOTTOM),
          Armour("Iron Cape", EQUIP_SLOT_CAPE),
          Armour("Iron Belt", EQUIP_SLOT_BELT),
          Armour("Iron Ring", EQUIP_SLOT_RING),
          Armour("Iron Pendant", EQUIP_SLOT_PENDANT),
          Armour("Iron Earrings", EQUIP_SLOT_EARRINGS)};
}

class PartyInspectPanelTest : public PanelTest {
 protected:
  PartyInspectPanelTest() : state_(Catalog(), {}, {}, {}, {}) {
  }

  // Every item these tests wear, by the key a catalog holds them under.
  static std::map<std::string, EquipPrototype> Catalog() {
    std::map<std::string, EquipPrototype> equips;
    for (const EquipPrototype& item : FullGear()) {
      equips[item.name()] = item;
    }
    return equips;
  }

  // A member wearing `items`, as their sheet would arrive. `power` takes the
  // combat power they read on their own screen, for comparing against what
  // the inspect screen makes of the sheet.
  PlayerInfo Member(const std::string& name,
                    const std::vector<EquipPrototype>& items,
                    int* power = nullptr) {
    CharacterInstance them(rng_, Character());
    them.SetUsername(name);
    for (int i = 0; i < 30; ++i) {
      them.LevelUp();
    }
    // A job, so there is combat power to compare: a Beginner has none.
    them.AdvanceJob(JOB_SWORDMAN);
    them.AdvanceJob(JOB_FIGHTER);
    for (const EquipPrototype& item : items) {
      them.PickUp(std::make_unique<EquipInstance>(item));
      them.Equip(0);
    }
    them.UseEquipSets(state_.equip_sets);
    if (power != nullptr) {
      *power = CharacterCombatPower(them, state_.skills);
    }
    PlayerInfo player;
    player.set_account_id("them");
    player.set_name(name);
    player.set_level(them.proto().level());
    *player.mutable_sheet() = them.ToProto();
    return player;
  }

  // The whole screen, which is taller than the shared fixture's 20 rows.
  // Wrapped in fillers the way Tui puts a standalone screen up, so the panel
  // keeps the width and height it asks for instead of being stretched to the
  // terminal's.
  static ftxui::Screen Draw(const PartyInspectPanel& panel, int rows = 40) {
    ftxui::Screen screen =
        ftxui::Screen::Create(ftxui::Dimension::Fixed(kTestScreenWidth),
                              ftxui::Dimension::Fixed(rows));
    ftxui::Render(screen, ftxui::hbox({
                              ftxui::filler(),
                              ftxui::vbox({panel.Render(), ftxui::filler()}),
                              ftxui::filler(),
                          }));
    return screen;
  }

  static std::string Screen(const PartyInspectPanel& panel) {
    return Draw(panel).ToString();
  }

  // How many rows the screen actually takes, drawn with room to spare.
  static int Height(const PartyInspectPanel& panel) {
    ftxui::Screen screen = Draw(panel, /*rows=*/60);
    int rows = 0;
    for (int y = 0; y < screen.dimy(); ++y) {
      if (RowSpan(screen, y).first >= 0) {
        rows = y + 1;
      }
    }
    return rows;
  }

  GameState state_;
};

// The sheet arrives naming its items; the panel resolves them against this
// build's catalogs, and what it draws is the member as they see themselves.
TEST_F(PartyInspectPanelTest, DrawsTheMemberFromTheirSheet) {
  int theirs = 0;
  PartyInspectPanel panel(state_);
  panel.SetPlayer(Member("Bree", {Sword(), Hat()}, &theirs));

  std::string screen = Screen(panel);
  EXPECT_NE(screen.find("Bree"), std::string::npos);
  EXPECT_NE(screen.find("Combat Power"), std::string::npos);
  EXPECT_NE(screen.find("Iron Sword"), std::string::npos);
  EXPECT_NE(screen.find("Iron Hat"), std::string::npos);
  // Rebuilt whole, not from the four figures a member's row carries: the
  // number here is the one they read on their own screen.
  EXPECT_GT(panel.character().proto().level(), 1);
  EXPECT_GT(theirs, 0);
  EXPECT_EQ(CharacterCombatPower(panel.character(), state_.skills), theirs);
}

// The stat window is the width it is everywhere else in the game, not the
// width of the item list under it, and it sits centred over that list.
TEST_F(PartyInspectPanelTest, TheStatWindowKeepsItsOwnWidth) {
  PartyInspectPanel panel(state_);
  panel.SetPlayer(Member("Bree", {Sword(), Hat()}));

  ftxui::Screen screen = Draw(panel);
  std::pair<int, int> stats = RowSpan(screen, 0);
  EXPECT_EQ(stats.second - stats.first + 1, AllStatsPanel::kTotalWidth);

  // The widest row on the screen is the item list's border, two columns out
  // from the width its rows are held to.
  std::pair<int, int> worn = {screen.dimx(), -1};
  for (int y = 1; y < screen.dimy(); ++y) {
    std::pair<int, int> row = RowSpan(screen, y);
    if (row.first < 0) {
      continue;
    }
    worn.first = std::min(worn.first, row.first);
    worn.second = std::max(worn.second, row.second);
  }
  EXPECT_EQ(worn.second - worn.first + 1, PartyInspectPanel::kContentWidth + 2);
  EXPECT_EQ(stats.first - worn.first, worn.second - stats.second);
}

TEST_F(PartyInspectPanelTest, TheCursorWalksTheWornItemsAndWraps) {
  PartyInspectPanel panel(state_);
  panel.SetPlayer(Member("Bree", {Sword(), Hat()}));

  ASSERT_NE(panel.selected_item(), nullptr);
  std::string first = panel.selected_item()->prototype().name();
  panel.MoveCursor(1);
  EXPECT_NE(panel.selected_item()->prototype().name(), first);
  // Two items, so Down again comes back to the first.
  panel.MoveCursor(1);
  EXPECT_EQ(panel.selected_item()->prototype().name(), first);
  panel.MoveCursor(-1);
  EXPECT_NE(panel.selected_item()->prototype().name(), first);
}

// A member in nothing at all. The stats still read, and there is no item for
// Enter to open.
TEST_F(PartyInspectPanelTest, HoldsUpWithNothingWorn) {
  PartyInspectPanel panel(state_);
  panel.SetPlayer(Member("Bree", {}));

  EXPECT_EQ(panel.selected_item(), nullptr);
  panel.MoveCursor(1);
  EXPECT_EQ(panel.selected_item(), nullptr);
  std::string screen = Screen(panel);
  EXPECT_NE(screen.find("Bree"), std::string::npos);
  EXPECT_NE(screen.find("empty"), std::string::npos);
}

// A second member replaces the first outright, cursor included -- the panel
// is one screen reused, not a pile of them.
TEST_F(PartyInspectPanelTest, ShowingAnotherMemberForgetsTheFirst) {
  PartyInspectPanel panel(state_);
  panel.SetPlayer(Member("Bree", {Sword(), Hat()}));
  panel.MoveCursor(1);

  panel.SetPlayer(Member("Cass", {Hat()}));
  std::string screen = Screen(panel);
  EXPECT_NE(screen.find("Cass"), std::string::npos);
  EXPECT_EQ(screen.find("Bree"), std::string::npos);
  EXPECT_EQ(screen.find("Iron Sword"), std::string::npos);
  ASSERT_NE(panel.selected_item(), nullptr);
  EXPECT_EQ(panel.selected_item()->prototype().name(), "Iron Hat");
}

// An item the sender has and this build does not is dropped the way a save
// loaded against changed catalogs drops it. Everything else still reads.
TEST_F(PartyInspectPanelTest, DropsAnItemThisBuildDoesNotHave) {
  EquipPrototype unknown;
  unknown.set_name("Fafnir Mistilteinn");
  unknown.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  unknown.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);

  PartyInspectPanel panel(state_);
  panel.SetPlayer(Member("Bree", {unknown, Hat()}));

  std::string screen = Screen(panel);
  EXPECT_EQ(screen.find("Fafnir"), std::string::npos);
  EXPECT_NE(screen.find("Iron Hat"), std::string::npos);
  EXPECT_NE(screen.find("Bree"), std::string::npos);
}

// The stat blocks are the point of the screen, so it is the item list that
// gives way to a short terminal -- and it never gives way so far that it stops
// being a list.
TEST_F(PartyInspectPanelTest, TheItemListGivesWayToAShortTerminal) {
  PartyInspectPanel panel(state_);
  panel.SetPlayer(Member("Bree", FullGear()));

  panel.SetMaxRows(0);
  int roomy = Height(panel);
  panel.SetMaxRows(PartyInspectPanel::kFixedRows +
                   PartyInspectPanel::kLeastListRows);
  int squeezed = Height(panel);
  EXPECT_LE(squeezed,
            PartyInspectPanel::kFixedRows + PartyInspectPanel::kLeastListRows);

  // Squeezed further, the screen is clipped rather than the list vanishing.
  panel.SetMaxRows(PartyInspectPanel::kFixedRows);
  EXPECT_EQ(Height(panel), squeezed);
}

}  // namespace
}  // namespace ms
