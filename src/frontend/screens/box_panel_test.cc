#include "src/frontend/screens/box_panel.h"

#include <gtest/gtest.h>

#include <map>
#include <random>
#include <string>
#include <utility>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/character/character.h"
#include "src/frontend/testing/screen_text.h"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

EquipPrototype Piece(const std::string& name, EquipSlot slot,
                     EquipJobCategory job) {
  EquipPrototype e;
  e.set_name(name);
  e.set_required_level(160);
  e.set_equip_slot(slot);
  e.add_equip_job_categories(job);
  TokenPrice* price = e.add_token_prices();
  price->set_token_item("absolab_coin");
  price->set_count(2);
  return e;
}

class BoxPanelTest : public testing::Test {
 protected:
  void SetUp() override {
    box_.set_name("AbsoLab Armor Box");
    box_.mutable_box()->add_token_items("absolab_coin");
    box_.mutable_box()->add_slots(EQUIP_SLOT_HAT);
    box_.mutable_box()->add_slots(EQUIP_SLOT_CAPE);
    panel_.Reset(box_);
  }

  ftxui::Screen Draw(ftxui::Element element) {
    element->ComputeRequirement();
    ftxui::Screen screen = ftxui::Screen::Create(
        ftxui::Dimension::Fixed(element->requirement().min_x),
        ftxui::Dimension::Fixed(element->requirement().min_y));
    ftxui::Render(screen, element);
    return screen;
  }

  static CharacterInstance Warrior(std::mt19937& rng) {
    Character proto;
    proto.set_level(150);
    proto.set_job(JOB_SWORDMAN);
    proto.set_job_stage(1);
    return CharacterInstance(rng, std::move(proto));
  }

  std::mt19937 rng_{1};
  CharacterInstance c_ = Warrior(rng_);
  std::map<std::string, EquipPrototype> equips_{
      {"cape", Piece("AbsoLab Knight Cape", EQUIP_SLOT_CAPE,
                     EQUIP_JOB_CATEGORY_WARRIOR)},
      {"hat", Piece("AbsoLab Knight Helm", EQUIP_SLOT_HAT,
                    EQUIP_JOB_CATEGORY_WARRIOR)},
      {"mage_hat",
       Piece("AbsoLab Sage Hat", EQUIP_SLOT_HAT, EQUIP_JOB_CATEGORY_MAGICIAN)},
  };
  std::map<std::string, ItemPrototype> items_;
  ItemPrototype box_;
  BoxPanel panel_{c_, equips_, items_};
};

// Titled with the box, a Name / Type / Level header over this class's pieces
// in the box's slot order, and nothing for another class.
TEST_F(BoxPanelTest, ListsThisClasssPiecesUnderAHeader) {
  ftxui::Screen screen = Draw(panel_.Render());
  EXPECT_GE(RowIndexOf(screen, "AbsoLab Armor Box"), 0);
  int header = RowIndexOf(screen, "Name");
  EXPECT_GE(header, 0);
  EXPECT_NE(ScreenRow(screen, header).find("Level"), std::string::npos);
  EXPECT_EQ(RowIndexOf(screen, "> AbsoLab Knight Helm"), header + 2)
      << "the hat first, as the box lists it, under a divider";
  EXPECT_EQ(RowIndexOf(screen, "AbsoLab Knight Cape"), header + 3);
  EXPECT_EQ(RowIndexOf(screen, "Sage"), -1) << "not for a warrior";
  EXPECT_NE(ScreenRow(screen, header + 3).find("Cape"), std::string::npos)
      << "armour shows its slot as its type";
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel_.Render()).empty());
}

// The cursor wraps, and the question names the piece under it.
TEST_F(BoxPanelTest, TheQuestionNamesThePick) {
  ASSERT_EQ(panel_.selected()->name, "AbsoLab Knight Helm");
  panel_.OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(panel_.selected()->name, "AbsoLab Knight Cape");
  panel_.OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(panel_.selected()->name, "AbsoLab Knight Helm");
  panel_.OnEvent(ftxui::Event::ArrowUp);
  panel_.OpenConfirm();
  ftxui::Screen screen = Draw(panel_.RenderConfirm());
  EXPECT_GE(RowIndexOf(screen, "Pick AbsoLab Knight Cape?"), 0);
  EXPECT_EQ(panel_.OnConfirmEvent(ftxui::Event::Return),
            ConfirmChoice::kConfirmed)
      << "the prompt opens on Confirm";
}

// Enter's menu sits on the cursor's row over the Type column, offers Inspect,
// Select and Close, and leads to the card, the question or back to the list.
TEST_F(BoxPanelTest, TheRowMenuLeadsToTheCardOrTheQuestion) {
  panel_.OnEvent(ftxui::Event::ArrowDown);
  panel_.OpenMenu();
  ASSERT_TRUE(panel_.menu_open());
  // Taller than the window: the menu hangs past its bottom border.
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                               ftxui::Dimension::Fixed(20));
  ftxui::Render(screen, panel_.Render());
  int cape = RowIndexOf(screen, "> AbsoLab Knight Cape");
  ASSERT_GE(cape, 0);
  int inspect = RowIndexOf(screen, "Inspect");
  EXPECT_EQ(inspect, cape + 1) << "the menu's top border on the cursor's row";
  EXPECT_GT(static_cast<int>(ScreenRow(screen, inspect).find("Inspect")),
            static_cast<int>(ScreenRow(screen, cape).find("Cape")))
      << "over the Type column, clear of the name";
  EXPECT_GE(RowIndexOf(screen, "Select"), 0);
  EXPECT_GE(RowIndexOf(screen, "Close"), 0);
  EXPECT_EQ(panel_.OnMenuEvent(ftxui::Event::Character('x')), kBoxMenu);
  EXPECT_EQ(panel_.OnMenuEvent(ftxui::Event::Return), kBoxInspect);
  EXPECT_FALSE(panel_.menu_open());
  EXPECT_EQ(panel_.selected_equip()->name(), "AbsoLab Knight Cape");
  EXPECT_EQ(panel_.selected_item(), nullptr);

  panel_.OpenMenu();
  EXPECT_EQ(panel_.OnMenuEvent(ftxui::Event::ArrowDown), kBoxMenu);
  EXPECT_EQ(panel_.OnMenuEvent(ftxui::Event::Return), kBoxConfirm);
  panel_.OpenMenu();
  EXPECT_EQ(panel_.OnMenuEvent(ftxui::Event::Escape), kBoxOpen);
  EXPECT_EQ(RowIndexOf(Draw(panel_.Render()), "Inspect"), -1);
}

// A class the box holds nothing for sees an empty list and no pick.
TEST_F(BoxPanelTest, NothingForThisClassIsEmpty) {
  equips_.erase("hat");
  equips_.erase("cape");
  panel_.Reset(box_);
  EXPECT_EQ(panel_.selected(), nullptr);
  EXPECT_TRUE(panel_.OnEvent(ftxui::Event::ArrowDown));
  EXPECT_GE(RowIndexOf(Draw(panel_.Render()), "(empty)"), 0);
  panel_.OpenMenu();
  EXPECT_FALSE(panel_.menu_open()) << "no row to open a menu on";
}

// A ring box lists each ring once, by the levels it can roll, then the Etc
// item offered instead; a ring the catalog lacks is left out.
TEST_F(BoxPanelTest, ARingBoxListsRingsByLevelRangeThenItems) {
  for (int level : {3, 4}) {
    EquipPrototype ring;
    ring.set_name("Ring of Restraint Lv. " + std::to_string(level));
    ring.set_required_level(110);
    ring.set_equip_slot(EQUIP_SLOT_RING);
    ring.mutable_equipment_skill()->set_skill("Ring of Restraint");
    ring.mutable_equipment_skill()->set_level(level);
    equips_["restraint_" + std::to_string(level)] = ring;
  }
  ItemPrototype grindstone;
  grindstone.set_name("Grindstone of Life");
  items_["grindstone_of_life"] = grindstone;
  ItemPrototype life;
  life.set_name("Life Boss Ring Box");
  RingBox* rings = life.mutable_ring_box();
  rings->add_skills("Ring of Restraint");
  rings->add_skills("Continuous Ring");
  for (int level : {3, 4}) {
    RingBox::LevelChance* chance = rings->add_levels();
    chance->set_level(level);
    chance->set_chance(0.5);
  }
  rings->add_items("grindstone_of_life");
  panel_.Reset(life);

  ftxui::Screen screen = Draw(panel_.Render());
  int header = RowIndexOf(screen, "Name");
  int ring = RowIndexOf(screen, "> Ring of Restraint Lv. 3-4");
  EXPECT_EQ(ring, header + 2);
  EXPECT_NE(ScreenRow(screen, ring).find("Lv110"), std::string::npos);
  EXPECT_EQ(RowIndexOf(screen, "Continuous"), -1) << "no ring in the catalog";
  int stone = RowIndexOf(screen, "Grindstone of Life");
  EXPECT_EQ(stone, header + 3);
  EXPECT_NE(ScreenRow(screen, stone).find("Etc"), std::string::npos);
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel_.Render()).empty());
  EXPECT_EQ(panel_.selected()->ring_skill, "Ring of Restraint");
  EXPECT_EQ(panel_.selected_equip()->name(), "Ring of Restraint Lv. 3")
      << "Inspect shows the lowest level it rolls";
  panel_.OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(panel_.selected()->item_key, "grindstone_of_life");
  EXPECT_EQ(panel_.selected_equip(), nullptr);
  EXPECT_EQ(panel_.selected_item()->name(), "Grindstone of Life");
}

}  // namespace
}  // namespace ms
