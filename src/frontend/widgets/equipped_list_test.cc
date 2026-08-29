#include "src/frontend/widgets/equipped_list.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/frontend/panel_widths.h"
#include "src/frontend/widgets/panel_test_base.h"
#include "src/frontend/widgets/text_columns.h"
#include "src/item/equip_instance.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

class EquippedListTest : public PanelTest {
 protected:
  // The rows for a character of `job` wearing `item`, in a name column
  // `name_width` wide.
  std::vector<EquippedRow> RowsWearing(Job job, const EquipPrototype& item,
                                       int name_width = kItemNameWidth) {
    Character proto;
    proto.set_level(60);
    proto.set_job(job);
    proto.set_job_stage(2);
    CharacterInstance c(rng_, std::move(proto));
    c.PickUp(std::make_unique<EquipInstance>(item));
    c.Equip(0);
    return EquippedRows(c, /*selected=*/-1,
                        std::chrono::steady_clock::duration::zero(),
                        name_width);
  }

  EquipPrototype Weapon(EquipType type) {
    EquipPrototype item;
    item.set_name("Test Weapon");
    item.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    item.set_equip_type(type);
    item.set_required_level(10);
    item.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
    return item;
  }
};

// A staff carries both attack figures. Which one the row shows is the one the
// wearer's damage is built on, not whichever the item lists first.
TEST_F(EquippedListTest, ShowsTheAttackTheJobSwingsWith) {
  EquipPrototype staff = Weapon(EQUIP_TYPE_STAFF);
  staff.mutable_base_stats()->set_attack(20);
  staff.mutable_base_stats()->set_magic_attack(75);
  staff.mutable_base_stats()->set_int_(30);

  std::vector<EquippedRow> mage = RowsWearing(JOB_ICE_LIGHTNING_WIZARD, staff);
  ASSERT_EQ(mage.size(), 1u);
  EXPECT_NE(mage[0].text.find("+75 MATT"), std::string::npos) << mage[0].text;
  EXPECT_NE(mage[0].text.find("+30 INT"), std::string::npos) << mage[0].text;
  EXPECT_EQ(mage[0].slot, EQUIP_SLOT_PRIMARY_WEAPON);
  EXPECT_FALSE(mage[0].inactive);
}

// Arrows with no bow drawn are worn and doing nothing, which the list draws
// dimmed. The row still says what the item is.
TEST_F(EquippedListTest, MarksAmmunitionWithNothingToDrawIt) {
  EquipPrototype arrows;
  arrows.set_name("Bronze Arrow");
  arrows.set_equip_slot(EQUIP_SLOT_PROJECTILE);
  arrows.set_equip_type(EQUIP_TYPE_ARROW_FOR_BOW);
  arrows.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  arrows.mutable_base_stats()->set_attack(10);

  std::vector<EquippedRow> rows = RowsWearing(JOB_HUNTER, arrows);
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_TRUE(rows[0].inactive);
  EXPECT_NE(rows[0].text.find("Bronze Arrow"), std::string::npos);
}

// The name cell is what a caller splits the row on to colour the name apart
// from the columns after it. A name too long for the column is cut to it, and
// the split still has to land where the columns begin.
TEST_F(EquippedListTest, ReportsWhereTheNameCellEnds) {
  EquipPrototype sword = Weapon(EQUIP_TYPE_ONE_HANDED_SWORD);
  sword.set_name("Frozen Blade of the Frigid North");
  sword.mutable_base_stats()->set_attack(50);

  std::vector<EquippedRow> rows = RowsWearing(JOB_FIGHTER, sword);
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0].text.substr(0, rows[0].name_bytes),
            "Frozen Blade of the Frigid");
  // What follows the cell is the columns, starting with the slot.
  EXPECT_NE(rows[0].text.substr(rows[0].name_bytes).find("Weapon"),
            std::string::npos);
}

TEST_F(EquippedListTest, IsEmptyWithNothingWorn) {
  CharacterInstance c = MakeCharacter();
  EXPECT_TRUE(
      EquippedRows(c, -1, std::chrono::steady_clock::duration::zero()).empty());
}

// The name column grows with the panel, and the header over it with the rows.
TEST_F(EquippedListTest, AWideNameColumnHoldsTheWholeName) {
  EquipPrototype wordy = Weapon(EQUIP_TYPE_ONE_HANDED_SWORD);
  wordy.set_name("Metallic Blue Book (Antistrophe)");

  std::vector<EquippedRow> narrow = RowsWearing(JOB_FIGHTER, wordy);
  std::vector<EquippedRow> wide = RowsWearing(JOB_FIGHTER, wordy, kItemNameMax);
  ASSERT_EQ(narrow.size(), 1u);
  ASSERT_EQ(wide.size(), 1u);
  EXPECT_EQ(narrow[0].text.find("Metallic Blue Book (Antistrophe)"),
            std::string::npos)
      << "the narrow column cuts it, which is what this compares against";
  EXPECT_NE(wide[0].text.find("Metallic Blue Book (Antistrophe)"),
            std::string::npos);
  EXPECT_EQ(wide[0].name_bytes - narrow[0].name_bytes,
            kItemNameMax - kItemNameWidth);
  // The header moves over with them, so the columns still name themselves.
  EXPECT_EQ(
      TextColumns(EquippedHeader(kItemNameMax)) - TextColumns(EquippedHeader()),
      kItemNameMax - kItemNameWidth);
}

// Four rings are four rows, each naming its own slot -- and they stand
// together between the eye accessory and the belt, though the slots the last
// three of them use are numbered at the bottom of the enum.
TEST_F(EquippedListTest, TheRingsStandTogetherAndNameTheirSlots) {
  Character proto;
  proto.set_level(60);
  proto.set_job(JOB_FIGHTER);
  proto.set_job_stage(2);
  CharacterInstance c(rng_, std::move(proto));
  // Put the rings on first, so the order below is the list's own doing.
  for (const char* name : {"Ring A", "Ring B", "Ring C"}) {
    EquipPrototype ring;
    ring.set_name(name);
    ring.set_equip_slot(EQUIP_SLOT_RING);
    c.PickUp(std::make_unique<EquipInstance>(ring));
    ASSERT_TRUE(c.Equip(0));
  }
  EquipPrototype belt;
  belt.set_name("Test Belt");
  belt.set_equip_slot(EQUIP_SLOT_BELT);
  c.PickUp(std::make_unique<EquipInstance>(belt));
  ASSERT_TRUE(c.Equip(0));
  c.PickUp(
      std::make_unique<EquipInstance>(Weapon(EQUIP_TYPE_ONE_HANDED_SWORD)));
  ASSERT_TRUE(c.Equip(0));

  std::vector<EquippedRow> rows =
      EquippedRows(c, -1, std::chrono::steady_clock::duration::zero());
  ASSERT_EQ(rows.size(), 5u);
  EXPECT_EQ(rows[0].slot, EQUIP_SLOT_PRIMARY_WEAPON);
  EXPECT_EQ(rows[1].slot, EQUIP_SLOT_RING);
  EXPECT_EQ(rows[2].slot, EQUIP_SLOT_RING_2);
  EXPECT_EQ(rows[3].slot, EQUIP_SLOT_RING_3);
  EXPECT_EQ(rows[4].slot, EQUIP_SLOT_BELT) << "the belt comes after all four";
  EXPECT_NE(rows[1].text.find("Ring 1"), std::string::npos) << rows[1].text;
  EXPECT_NE(rows[3].text.find("Ring 3"), std::string::npos) << rows[3].text;
}

// The right column's minimum width in panel_widths.h is this header, the
// gutter inside its right border and the border itself, so a column added
// here has to move that number rather than quietly run off the edge of a
// narrow terminal.
TEST_F(EquippedListTest, TheHeadersFitTheRightColumnMinimum) {
  EXPECT_EQ(TextColumns(EquippedHeader()) + kItemListGutter + 2,
            kRightColumnMin);
  EXPECT_LE(TextColumns(kSymbolHeader) + kItemListGutter + 2, kRightColumnMin);
}

}  // namespace
}  // namespace ms
