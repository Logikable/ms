#include "src/frontend/widgets/equipped_list.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/frontend/panel_widths.h"
#include "src/frontend/widgets/item_row.h"
#include "src/frontend/widgets/panel_test_base.h"
#include "src/frontend/widgets/text_columns.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
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

// The order the window lists what is worn: down the body, then the
// accessories, then what is carried rather than worn. It is a fixed order,
// not the enum's -- the rings and the second pendant are numbered at the
// bottom of the enum and still stand with their families here.
TEST_F(EquippedListTest, ListsWhatIsWornInTheWindowsOrder) {
  const std::vector<EquipSlot> kExpected = {
      EQUIP_SLOT_PRIMARY_WEAPON, EQUIP_SLOT_HAT,           EQUIP_SLOT_TOP,
      EQUIP_SLOT_BOTTOM,         EQUIP_SLOT_SHOES,         EQUIP_SLOT_GLOVES,
      EQUIP_SLOT_CAPE,           EQUIP_SLOT_SHOULDER,      EQUIP_SLOT_BELT,
      EQUIP_SLOT_FACE_ACCESSORY, EQUIP_SLOT_EYE_ACCESSORY, EQUIP_SLOT_EARRINGS,
      EQUIP_SLOT_PENDANT,        EQUIP_SLOT_PENDANT_2,     EQUIP_SLOT_RING,
      EQUIP_SLOT_RING_2,         EQUIP_SLOT_RING_3,        EQUIP_SLOT_RING_4,
      EQUIP_SLOT_EMBLEM,         EQUIP_SLOT_BADGE,         EQUIP_SLOT_MEDAL,
      EQUIP_SLOT_POCKET,         EQUIP_SLOT_PROJECTILE,    EQUIP_SLOT_SECONDARY,
      EQUIP_SLOT_HEART,
  };

  Character proto;
  proto.set_level(60);
  proto.set_job(JOB_FIGHTER);
  proto.set_job_stage(2);
  CharacterInstance c(rng_, std::move(proto));
  // Worn in the reverse of the order expected, so what comes out is the
  // list's own doing rather than the order they went on.
  for (std::vector<EquipSlot>::const_reverse_iterator it = kExpected.rbegin();
       it != kExpected.rend(); ++it) {
    EquipPrototype item;
    item.set_name(EquipSlot_Name(*it));
    item.set_equip_slot(BaseSlot(*it));
    c.PickUp(std::make_unique<EquipInstance>(item));
    ASSERT_TRUE(c.Equip(0)) << EquipSlot_Name(*it);
  }

  std::vector<EquippedRow> rows =
      EquippedRows(c, -1, std::chrono::steady_clock::duration::zero());
  std::vector<EquipSlot> worn;
  for (const EquippedRow& row : rows) {
    worn.push_back(row.slot);
  }
  EXPECT_EQ(worn, kExpected);
  // A family's rows say which of its slots they are; every other row does not.
  EXPECT_NE(rows[16].text.find("Ring 3"), std::string::npos) << rows[16].text;
  EXPECT_NE(rows[13].text.find("Pendant 2"), std::string::npos)
      << rows[13].text;
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
