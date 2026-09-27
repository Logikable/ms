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
  e.set_token_item("absolab_coin");
  e.set_token_price(2);
  return e;
}

class BoxPanelTest : public testing::Test {
 protected:
  void SetUp() override {
    box_.set_name("AbsoLab Armor Box");
    box_.mutable_box()->set_token_item("absolab_coin");
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
  ItemPrototype box_;
  BoxPanel panel_{c_, equips_};
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
  ASSERT_EQ(panel_.selected()->name(), "AbsoLab Knight Helm");
  panel_.OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(panel_.selected()->name(), "AbsoLab Knight Cape");
  panel_.OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(panel_.selected()->name(), "AbsoLab Knight Helm");
  panel_.OnEvent(ftxui::Event::ArrowUp);
  panel_.OpenConfirm();
  ftxui::Screen screen = Draw(panel_.RenderConfirm());
  EXPECT_GE(RowIndexOf(screen, "Pick AbsoLab Knight Cape?"), 0);
  EXPECT_EQ(panel_.OnConfirmEvent(ftxui::Event::Return),
            ConfirmChoice::kConfirmed)
      << "the prompt opens on Confirm";
}

// A class the box holds nothing for sees an empty list and no pick.
TEST_F(BoxPanelTest, NothingForThisClassIsEmpty) {
  equips_.erase("hat");
  equips_.erase("cape");
  panel_.Reset(box_);
  EXPECT_EQ(panel_.selected(), nullptr);
  EXPECT_TRUE(panel_.OnEvent(ftxui::Event::ArrowDown));
  EXPECT_GE(RowIndexOf(Draw(panel_.Render()), "(empty)"), 0);
}

}  // namespace
}  // namespace ms
