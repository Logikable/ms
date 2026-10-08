#include "src/frontend/screens/familiar_card.h"

#include <gtest/gtest.h>

#include <string>

#include "src/character/familiar.h"
#include "src/frontend/testing/panel_test_base.h"

namespace ms {
namespace {

class FamiliarCardTest : public PanelTest {};

// A familiar never levelled names itself and its mob, and has no potential
// to show.
TEST_F(FamiliarCardTest, ALevelZeroFamiliarHasNoPotential) {
  FamiliarBook book;
  FamiliarCard card;
  card.SetFamiliar(&book, "Blue Snail");
  const std::string rendered = RenderElement(card.Render());
  EXPECT_NE(rendered.find("Mob: Blue Snail"), std::string::npos) << rendered;
  EXPECT_NE(rendered.find("Level: 0 / 4"), std::string::npos);
  EXPECT_NE(rendered.find("EXP: 1,000 to Lv 1"), std::string::npos);
  EXPECT_EQ(rendered.find("Potential"), std::string::npos);
}

// A levelled one names its rank once and lists both lines with their values.
TEST_F(FamiliarCardTest, ALevelledFamiliarListsItsLines) {
  FamiliarBook book;
  Familiar* snail = book.add_familiars();
  snail->set_name("Snail");
  snail->set_nickname("Gary");
  snail->set_level(kFamiliarMaxLevel);
  FamiliarLine* boss = snail->add_lines();
  boss->set_type(FAMILIAR_LINE_TYPE_BOSS_DAMAGE_40);
  boss->set_rank(POTENTIAL_RANK_LEGENDARY);
  FamiliarLine* drop = snail->add_lines();
  drop->set_type(FAMILIAR_LINE_TYPE_BOSS_DROP_RATE);
  drop->set_rank(POTENTIAL_RANK_LEGENDARY);
  FamiliarCard card;
  card.SetFamiliar(&book, "Snail");
  const std::string rendered = RenderElement(card.Render());
  EXPECT_NE(rendered.find("Gary"), std::string::npos) << rendered;
  EXPECT_NE(rendered.find("Mob: Snail"), std::string::npos);
  EXPECT_NE(rendered.find("EXP: MAX"), std::string::npos);
  EXPECT_NE(rendered.find("Legendary Potential"), std::string::npos);
  EXPECT_NE(rendered.find("Boss Damage  +40%"), std::string::npos);
  EXPECT_NE(rendered.find("Boss Drop Rate  +100%"), std::string::npos);
}

// The card has a row budget on screen, which a card with no lines must
// still fit.
TEST_F(FamiliarCardTest, FitsARowBudget) {
  FamiliarBook book;
  FamiliarCard card;
  card.SetFamiliar(&book, "Snail");
  card.SetMaxRows(30);
  EXPECT_NE(RenderElement(card.Render()).find("Mob: Snail"), std::string::npos);
}

}  // namespace
}  // namespace ms
