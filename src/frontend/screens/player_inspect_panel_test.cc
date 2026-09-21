#include "src/frontend/screens/player_inspect_panel.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "src/character/character_stats.h"
#include "src/character/progression.h"
#include "src/character/skill_placement.h"
#include "src/frontend/main_layout.h"
#include "src/frontend/screens/all_stats_panel.h"
#include "src/frontend/testing/panel_test_base.h"
#include "src/frontend/testing/screen_text.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/stat_rows.h"
#include "src/item/equip_instance.h"
#include "src/protos/character.pb.h"
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

// One skill in the book the members these tests build end up in.
std::map<std::string, Skill> SkillCatalog() {
  Skill strike;
  strike.set_name("Power Strike");
  PlaceIn(strike, JOB_ADVANCEMENT_SWORDMAN);
  strike.set_max_level(20);
  return {{"power_strike", strike}};
}

class PlayerInspectPanelTest : public PanelTest {
 protected:
  PlayerInspectPanelTest() : state_(Catalog(), {}, {}, {}, {}, SkillCatalog()) {
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

  // A member at the level Hyper Stats open at, with a different STR level in
  // each of their two allocations.
  PlayerInfo HyperMember(const std::string& name) {
    Character proto;
    proto.set_level(140);
    proto.set_job(JOB_HERO);
    proto.set_job_stage(4);
    HyperStats& hyper = *proto.mutable_hyper_stats();
    (*PresetOf(hyper, StatPreset::kFirst)
          .mutable_levels())[HYPER_STAT_FIELD_STR] = 1;
    (*PresetOf(hyper, StatPreset::kSecond)
          .mutable_levels())[HYPER_STAT_FIELD_STR] = 2;
    PlayerInfo player;
    player.set_account_id("them");
    player.set_name(name);
    player.set_level(proto.level());
    proto.set_name(name);
    player.set_autoswap_presets(true);
    *player.mutable_sheet() = std::move(proto);
    return player;
  }

  // The screen at the terminal's size, the way Tui draws it: this one takes
  // the whole thing rather than standing in the middle of it.
  static ftxui::Screen Draw(PlayerInspectPanel& panel, int rows = 40,
                            int columns = kTestScreenWidth) {
    ftxui::Screen screen = ftxui::Screen::Create(
        ftxui::Dimension::Fixed(columns), ftxui::Dimension::Fixed(rows));
    ftxui::Render(screen, panel.Render(rows, columns));
    return screen;
  }

  static std::string Screen(PlayerInspectPanel& panel) {
    return ScreenText(Draw(panel));
  }

  // The rows a panel's window covers, found by the column its left border
  // stands in -- every row of that column is border, corners included.
  // {-1, -1} when nothing is drawn there at all.
  static std::pair<int, int> PanelRows(const ftxui::Screen& screen, int x) {
    std::pair<int, int> span = {-1, -1};
    for (int y = 0; y < screen.dimy(); ++y) {
      const std::string& glyph = screen.PixelAt(x, y).character;
      if (glyph.empty() || glyph == " ") {
        continue;
      }
      if (span.first < 0) {
        span.first = y;
      }
      span.second = y;
    }
    return span;
  }

  // Sends a key the way the game does: after a frame. The Equipped list is an
  // ftxui::Menu, whose entries are filled by the render, and a key that
  // arrives before the first one finds it empty.
  static bool Send(PlayerInspectPanel& panel, const ftxui::Event& event) {
    Draw(panel);
    return panel.OnEvent(event);
  }

  // Walks the cursor onto the member's Character panel.
  static void FocusCharacterPanel(PlayerInspectPanel& panel) {
    Send(panel, ftxui::Event::Tab);
  }

  GameState state_;
};

// The sheet arrives naming its items; the panel resolves them against this
// build's catalogs, and what it draws is the member's own main screen.
TEST_F(PlayerInspectPanelTest, DrawsTheMembersOwnScreen) {
  int theirs = 0;
  PlayerInspectPanel panel(state_);
  panel.SetPlayer(Member("Bree", {Sword(), Hat()}, &theirs));

  std::string screen = Screen(panel);
  EXPECT_NE(screen.find("Bree"), std::string::npos);
  EXPECT_NE(screen.find("Combat Power"), std::string::npos);
  EXPECT_NE(screen.find("Character"), std::string::npos) << "no left panel";
  EXPECT_NE(screen.find("Equipped"), std::string::npos) << "no right panel";
  EXPECT_NE(screen.find("Iron Sword"), std::string::npos);
  EXPECT_NE(screen.find("Iron Hat"), std::string::npos);
  // Nothing of the reader's own view comes with them.
  EXPECT_EQ(screen.find("Inventory"), std::string::npos);
  EXPECT_EQ(screen.find("Mobs"), std::string::npos);
  // Rebuilt whole, not from the four figures a member's row carries: the
  // number here is the one they read on their own screen.
  EXPECT_GT(panel.character().proto().level(), 1);
  EXPECT_GT(theirs, 0);
  EXPECT_EQ(CharacterCombatPower(panel.character(), state_.skills), theirs);
}

// Nothing here spends, so nothing here has a button -- and a member's name is
// not the reader's to take.
TEST_F(PlayerInspectPanelTest, BothPanelsAreReadOnly) {
  PlayerInspectPanel panel(state_);
  panel.SetPlayer(Member("Bree", {Sword(), Hat()}));
  EXPECT_EQ(Screen(panel).find("[+]"), std::string::npos);

  FocusCharacterPanel(panel);
  Send(panel, ftxui::Event::ArrowUp);  // -> the name row
  Send(panel, ftxui::Event::Return);
  EXPECT_NE(Screen(panel).find("Bree"), std::string::npos);
}

// The columns split the way the main screen's do, and both panels run down to
// the exp bar rather than stopping where their contents do.
TEST_F(PlayerInspectPanelTest, TheColumnsAreTheMainScreensAndReachTheFoot) {
  PlayerInspectPanel panel(state_);
  panel.SetPlayer(Member("Bree", FullGear()));
  constexpr int kRows = 40;
  ftxui::Screen screen = Draw(panel, kRows);

  MainWidths widths =
      ComputeMainWidths(kTestScreenWidth, /*has_right_column=*/true);
  std::pair<int, int> left = PanelRows(screen, 0);
  std::pair<int, int> right = PanelRows(screen, widths.left);
  EXPECT_EQ(left.first, 0) << "the Character panel is not at the top";
  EXPECT_EQ(right.first, 0) << "the Equipped panel starts somewhere else";
  // The Equipped list grows into everything the exp bar leaves.
  EXPECT_EQ(right.second, kRows - 2);
  // The exp bar has the last row to itself.
  EXPECT_NE(ScreenRow(screen, kRows - 1).find("%"), std::string::npos);
}

// A short terminal is what the Character panel's extra stats give way to, and
// the View All Stats row under them is the last thing it gives up.
TEST_F(PlayerInspectPanelTest, TheCharacterPanelFitsAShortTerminal) {
  PlayerInspectPanel panel(state_);
  panel.SetPlayer(Member("Bree", FullGear()));

  ftxui::Screen tall = Draw(panel, /*rows=*/40);
  ftxui::Screen squat = Draw(panel, /*rows=*/24);
  EXPECT_EQ(PanelRows(squat, 0).second, 22) << "the panel ran past the exp bar";
  EXPECT_NE(ScreenText(tall).find("View All Stats"), std::string::npos);
  EXPECT_NE(ScreenText(squat).find("View All Stats"), std::string::npos)
      << "the row the rest of the stats are behind";
}

// Tab walks between the two panels, the way it does on the main screen.
TEST_F(PlayerInspectPanelTest, TabMovesBetweenThePanels) {
  PlayerInspectPanel panel(state_);
  panel.SetPlayer(Member("Bree", {Sword(), Hat()}));

  // It opens on the Equipped list, so the cursor walks the gear.
  ASSERT_NE(panel.selected_item(), nullptr);
  std::string first = panel.selected_item()->prototype().name();
  Send(panel, ftxui::Event::ArrowDown);
  EXPECT_NE(panel.selected_item()->prototype().name(), first);

  // Over on the Character panel the arrows belong to it, and the gear cursor
  // stays where it was left -- through the walk and the way back.
  FocusCharacterPanel(panel);
  std::string on_gear = panel.selected_item()->prototype().name();
  Send(panel, ftxui::Event::ArrowDown);
  Send(panel, ftxui::Event::ArrowDown);
  EXPECT_EQ(panel.selected_item()->prototype().name(), on_gear);
  EXPECT_NE(Screen(panel).find("Skills"), std::string::npos)
      << "the keys never reached the Character panel";
  Send(panel, ftxui::Event::Tab);
  Send(panel, ftxui::Event::ArrowUp);
  EXPECT_EQ(panel.selected_item()->prototype().name(), first);
}

// Enter on a worn item raises its card, and Enter on the Expand tab opens the
// gear over the whole screen -- the same door the player's own panel has.
TEST_F(PlayerInspectPanelTest, EnterRaisesTheCardsAndTheExpandTabOpensUp) {
  PlayerInspectPanel panel(state_);
  int items = 0;
  PlayerInspectActions actions;
  actions.item = [&items]() { ++items; };
  panel.UseActions(actions);
  panel.SetPlayer(Member("Bree", {Sword(), Hat()}));

  Send(panel, ftxui::Event::Character(' '));
  EXPECT_EQ(items, 1);

  // Up off the list is the tab bar, and Left off the first tab is Expand.
  Send(panel, ftxui::Event::ArrowUp);
  Send(panel, ftxui::Event::ArrowLeft);
  Send(panel, ftxui::Event::Return);
  EXPECT_TRUE(panel.expanded());
  ftxui::Screen wide = Draw(panel);
  EXPECT_EQ(ScreenText(wide).find("Character"), std::string::npos)
      << "the expanded panel is the whole screen";
  EXPECT_NE(ScreenText(wide).find("Iron Sword"), std::string::npos);
  EXPECT_EQ(PanelRows(wide, 0).second, 39) << "it did not fill the terminal";

  panel.CloseExpanded();
  EXPECT_NE(Screen(panel).find("Character"), std::string::npos);
}

// The Character panel's own tabs, which is most of what the screen gained.
TEST_F(PlayerInspectPanelTest, TheCharacterPanelCarriesTheirTabs) {
  PlayerInspectPanel panel(state_);
  std::vector<std::string> skills;
  PlayerInspectActions actions;
  actions.skill = [&skills](const Skill& skill) {
    skills.push_back(skill.name());
  };
  panel.UseActions(actions);
  panel.SetPlayer(Member("Bree", {Sword(), Hat()}));

  FocusCharacterPanel(panel);
  Send(panel, ftxui::Event::ArrowRight);  // Stats -> Skills
  EXPECT_NE(Screen(panel).find("Power Strike"), std::string::npos);

  Send(panel, ftxui::Event::ArrowDown);  // -> the advancement bar
  Send(panel, ftxui::Event::ArrowDown);  // -> the first skill
  Send(panel, ftxui::Event::Return);
  EXPECT_EQ(skills, std::vector<std::string>{"Power Strike"});
}

// A member in nothing at all. The stats still read, and there is no item for
// Enter to open.
TEST_F(PlayerInspectPanelTest, HoldsUpWithNothingWorn) {
  PlayerInspectPanel panel(state_);
  panel.SetPlayer(Member("Bree", {}));

  EXPECT_EQ(panel.selected_item(), nullptr);
  std::string screen = Screen(panel);
  EXPECT_NE(screen.find("Bree"), std::string::npos);
  EXPECT_NE(screen.find("empty"), std::string::npos);
}

// A second member replaces the first outright, both panels included -- the
// screen is one reused, not a pile of them.
TEST_F(PlayerInspectPanelTest, ShowingAnotherMemberForgetsTheFirst) {
  PlayerInspectPanel panel(state_);
  panel.SetPlayer(Member("Bree", {Sword(), Hat()}));
  Send(panel, ftxui::Event::ArrowDown);
  FocusCharacterPanel(panel);
  Send(panel, ftxui::Event::ArrowRight);  // onto their Skills tab

  PlayerInfo other = Member("Cass", {Hat()});
  other.set_account_id("other");
  panel.SetPlayer(other);
  std::string screen = Screen(panel);
  EXPECT_NE(screen.find("Cass"), std::string::npos);
  EXPECT_EQ(screen.find("Bree"), std::string::npos);
  EXPECT_EQ(screen.find("Iron Sword"), std::string::npos);
  EXPECT_EQ(screen.find("Power Strike"), std::string::npos)
      << "the last member's open tab came with them";
  ASSERT_NE(panel.selected_item(), nullptr);
  EXPECT_EQ(panel.selected_item()->prototype().name(), "Iron Hat");
}

// An item the sender has and this build does not is dropped the way a save
// loaded against changed catalogs drops it. Everything else still reads.
TEST_F(PlayerInspectPanelTest, DropsAnItemThisBuildDoesNotHave) {
  EquipPrototype unknown;
  unknown.set_name("Fafnir Windwing Shooter");
  unknown.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  unknown.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);

  PlayerInspectPanel panel(state_);
  panel.SetPlayer(Member("Bree", {unknown, Hat()}));

  std::string screen = Screen(panel);
  EXPECT_EQ(screen.find("Fafnir"), std::string::npos);
  EXPECT_NE(screen.find("Iron Hat"), std::string::npos);
  EXPECT_NE(screen.find("Bree"), std::string::npos);
}

// A member past level 140 carries the Farm/Boss row their own screen carries,
// and Left/Right read between their two allocations.
TEST_F(PlayerInspectPanelTest, TheFarmBossRowReadsBothAllocations) {
  PlayerInspectPanel panel(state_);
  panel.SetPlayer(HyperMember("Bree"));
  FocusCharacterPanel(panel);

  EXPECT_NE(Screen(panel).find("Farm"), std::string::npos);
  EXPECT_EQ(panel.preset(), Activity::kFarming);

  Send(panel, ftxui::Event::ArrowDown);  // the tab bar -> the Farm/Boss row
  Send(panel, ftxui::Event::ArrowRight);
  EXPECT_EQ(panel.preset(), Activity::kBossing);
  Send(panel, ftxui::Event::ArrowLeft);
  EXPECT_EQ(panel.preset(), Activity::kFarming);
}

// The All Stats screen the View All Stats row opens is the member's, and it
// opens on the allocation their Character panel is reading.
TEST_F(PlayerInspectPanelTest, TheirAllStatsScreenOpensOnTheirAllocation) {
  PlayerInspectPanel panel(state_);
  int opened = 0;
  PlayerInspectActions actions;
  actions.all_stats = [&opened]() { ++opened; };
  panel.UseActions(actions);
  panel.SetPlayer(HyperMember("Bree"));
  FocusCharacterPanel(panel);

  Send(panel, ftxui::Event::ArrowDown);  // -> the Farm/Boss row
  Send(panel, ftxui::Event::ArrowRight);
  Send(panel, ftxui::Event::ArrowDown);  // -> View All Stats
  Send(panel, ftxui::Event::Return);
  ASSERT_EQ(opened, 1);

  panel.SyncAllStats();
  ftxui::Screen screen = ftxui::Screen::Create(
      ftxui::Dimension::Fixed(kTestScreenWidth), ftxui::Dimension::Fixed(40));
  ftxui::Render(screen, panel.RenderAllStats());
  EXPECT_NE(ScreenText(screen).find("(0+60) 60"), std::string::npos)
      << "the screen opened on the other allocation";
  // And Left/Right read between them here too.
  EXPECT_TRUE(panel.OnAllStatsEvent(ftxui::Event::ArrowLeft));
  ftxui::Render(screen, panel.RenderAllStats());
  EXPECT_NE(ScreenText(screen).find("(0+30) 30"), std::string::npos);
}

// The member's screen is the main view's own layout, and it is measured here
// at the size the Tui hands it.
TEST_F(PlayerInspectPanelTest, TheMembersScreenKeepsOffItsRightBorder) {
  PlayerInspectPanel panel(state_);
  panel.SetPlayer(Member("Bree", {Sword(), Hat()}));
  EXPECT_TRUE(
      RowsTouchingTheRightBorder(panel.Render(/*rows=*/40, /*columns=*/120))
          .empty());
}
}  // namespace
}  // namespace ms
